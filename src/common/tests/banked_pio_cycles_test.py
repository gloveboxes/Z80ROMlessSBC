"""Execute the assembled PIO words: the PIO1 clock SM and the PIO2 I/O SM.

This is a digital protocol model, not RP2350 firmware execution or electrical
qualification. A Z80 bus model reacts to CLK edges (T2 asserts IORQ#, TW
samples WAIT#, T3's falling edge samples IN data and ends the cycle, the next
opcode fetch lowers RD#). Inputs pass a two-stage synchronizer and PIO2's
IRQ PREV reaches PIO1 after a configurable latency. It checks instruction
encodings, clock phase guards, port routing, OUT snapshots, IN replies, bus
turnaround and FIFO backpressure without assuming the assembly is right.
"""
from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import re
import unittest

RX_DEPTH = TX_DEPTH = 4
UP, DOWN, A16, RELEASE, CLK = 6, 7, 8, 9, 2
DRIVE_DIRS, IDLE_DIRS = 0x3FF, 0x003


class StateMachine:
    def __init__(self, model: Model, words: list[int], layout: dict[str, int], clock: bool):
        self.model, self.words, self.clock = model, words, clock
        self.first, self.last = layout["wrap_target"], layout["wrap"]
        self.pc = layout["clock_end"] if clock else layout["io_start"]
        self.x = self.y = self.osr = self.delay = 0

    def step(self, inputs: int) -> None:
        if self.delay:
            self.delay -= 1
            return
        m = self.model
        word = self.words[self.pc]
        opcode, arg = word >> 13, word & 255
        if not self.clock and word & 0x1000:
            m.output(RELEASE, bool(word & 0x0800))
        following = self.first if self.pc == self.last else self.pc + 1
        if opcode == 0:  # JMP
            condition = (arg >> 5) & 7
            take = {0: True, 5: self.x != self.y,
                    6: bool(inputs & (1 << 27))}[condition]
            if take:
                following = arg & 31
        elif opcode == 1:  # WAIT
            source, index, polarity = (arg >> 5) & 3, arg & 31, bool(arg & 128)
            if source == 0:
                value = bool(inputs & (1 << index))
            elif source == 2:
                assert self.clock and index & 0x18 == 0
                value = m.clock_flags[index & 7]
            else:
                raise AssertionError("unexpected WAIT source")
            if value != polarity:
                return
        elif opcode == 2:  # IN pins, 32 with autopush
            assert not self.clock and arg == 0
            if len(m.rx) >= RX_DEPTH:
                return  # autopush stall: WAIT stays asserted
            m.capture(((inputs >> 10) | (inputs << 22)) & 0xFFFFFFFF)
        elif opcode == 3:  # OUT, right shifting
            count = (arg & 31) or 32
            value = self.osr & ((1 << count) - 1)
            self.osr >>= count
            destination = arg >> 5
            if destination == 0:  # PINS, OUT base GP8, count 10
                m.out_pins(value, count)
            elif destination == 1:
                self.x = value
            elif destination == 4:  # PINDIRS
                assert count == 10
                m.out_pindirs(value)
            else:
                assert destination == 3  # NULL
        elif opcode == 4:  # PULL block
            assert word == 0x80A0
            if not m.tx:
                return
            self.osr = m.tx.popleft()
        elif opcode == 5:  # MOV / NOP
            source, destination = arg & 7, arg >> 5
            if source == 0:
                value = ((inputs >> 10) | (inputs << 22)) & 0xFFFFFFFF
            elif source == 2:
                value = self.y
            else:
                raise AssertionError("unexpected MOV source")
            if destination == 7:
                self.osr = value
            else:
                assert destination == 2
                self.y = value
        elif opcode == 6:  # IRQ PREV SET / CLEAR from PIO2 to PIO1
            assert not self.clock and arg & 0x38 == 0x08 and not arg & 0x20
            m.raise_clock_flag(arg & 7, not arg & 0x40)
        elif opcode == 7:  # SET
            destination, value = arg >> 5, arg & 31
            if self.clock:
                assert destination == 0
                m.output(CLK, value & 1)
            elif destination == 0:  # SET base GP6, count 2
                m.set_enables(value)
            elif destination == 1:
                self.x = value
            else:
                assert destination == 2
                self.y = value
        else:
            raise AssertionError(f"unsupported PIO word {word:04x}")
        self.pc = following
        self.delay = (word >> 8) & (31 if self.clock else 7)


class Model:
    clock_words: list[int]
    io_words: list[int]
    clock_layout: dict[str, int]
    io_layout: dict[str, int]

    def __init__(self, latency: int = 0, io_first: bool = False, z80_delay: int = 3):
        self.latency, self.io_first, self.z80_delay = latency, io_first, z80_delay
        self.cycle = 0
        self.clock_flags = [False] * 8
        self.flag_events: list[tuple[int, int, bool]] = []
        self.levels = {CLK: False, UP: True, DOWN: True, A16: False, RELEASE: False}
        self.pico_data = 0
        self.dirs = 0x003  # GP8, GP9 outputs; GP10-GP17 inputs
        self.iorq = self.rd = self.wr = self.busack = True
        self.port = self.data = 0
        self.rx: deque[int] = deque()
        self.tx: deque[int] = deque()
        self.snapshots: list[int] = []
        self.bank_events: list[int] = []
        self.sampled: list[int] = []
        self.edges: list[tuple[int, bool]] = [(-3, False)]
        self.release = None
        self.counting = False
        self.release_falls = self.last_release_falls = 0
        self.receive_since = -100
        self.cycle_kind: str | None = None
        self.falls = 0
        self.pending: list[tuple[int, str, bool]] = []
        self.reply_after: int | None = None
        self.next_fetch_rd_low: int | None = None
        self.clock = StateMachine(self, self.clock_words, self.clock_layout, True)
        self.io = StateMachine(self, self.io_words, self.io_layout, False)
        self.inputs = deque([self.pins(), self.pins()])

    # ---- Pico outputs -------------------------------------------------
    def pins(self) -> int:
        value = sum(int(level) << pin for pin, level in self.levels.items())
        for pin, level in ((0, self.busack), (1, self.iorq), (27, self.rd), (28, self.wr)):
            value |= int(level) << pin
        for bit, port_bit in enumerate((0, 1, 2, 4)):
            value |= ((self.port >> port_bit) & 1) << (18 + bit)
        if self.dirs & ~IDLE_DIRS:
            received = self.pico_data
        elif not self.levels[DOWN] and self.cycle > self.receive_since:
            received = self.data
        else:
            received = 0xAA  # isolated inputs deliberately read stale data
        return value | (received << 10)

    def output(self, pin: int, value: bool) -> None:
        old = self.levels[pin]
        self.levels[pin] = bool(value)
        if pin == DOWN and old and not value:
            assert not self.dirs & ~IDLE_DIRS, "U10 enabled while Pico drives D0-D7"
            self.receive_since = self.cycle
        if pin == UP and old and not value:
            assert self.levels[DOWN], "U9 and U10 enabled together"
            assert self.dirs == DRIVE_DIRS, "U9 enabled before D0-D7 are driven"
        if pin == RELEASE and not old and value:
            assert not self.levels[CLK], "WAIT released while CLK HIGH"
            self.release = self.cycle
            self.counting, self.release_falls = True, 0
        if pin == RELEASE and old and not value:
            self.counting, self.last_release_falls = False, self.release_falls
        if pin == CLK and old != bool(value):
            assert self.cycle - self.edges[-1][0] >= 3, "short clock pulse"
            self.edges.append((self.cycle, bool(value)))
            if value and self.release is not None:
                assert self.cycle - self.release >= 8, "WAIT/A16 settling guard lost"
                self.release = None
            if not value:
                self.falling_edge()
            else:
                self.rising_edge()

    def set_enables(self, value: int) -> None:
        # Apply a disable before an enable so one SET never overlaps U9/U10.
        for pin, bit in sorted(((UP, value & 1), (DOWN, value >> 1 & 1)), key=lambda p: -p[1]):
            self.output(pin, bool(bit))

    def out_pins(self, value: int, count: int) -> None:
        assert count in (1, 10)
        value &= (1 << count) - 1  # OUT writes zero-extended data to all 10 pins
        if count == 1:
            self.bank_events.append(value & 1)
            assert not self.levels[CLK] and not self.levels[DOWN]
            assert not self.iorq and not self.wr and self.rd
            assert value == self.data & 1, "sampled stale/unsettled data"
        self.output(A16, bool(value & 1))
        assert not value & 2 and not self.levels[RELEASE], "OUT changed IO_RELEASE"
        self.pico_data = value >> 2

    def out_pindirs(self, value: int) -> None:
        assert value in (DRIVE_DIRS, IDLE_DIRS)
        if value == DRIVE_DIRS:
            assert self.levels[DOWN] and self.levels[UP], "drove D0-D7 into an enabled buffer"
        else:
            assert self.levels[UP], "released D0-D7 while U9 enabled"
        self.dirs = value

    def raise_clock_flag(self, index: int, value: bool) -> None:
        self.flag_events.append((self.cycle + self.latency, index, value))

    def capture(self, snapshot: int) -> None:
        assert not self.levels[RELEASE] and not self.iorq
        if self.rd:  # OUT: CLK has been held since decode
            assert not self.levels[CLK] and not self.levels[DOWN] and not self.wr
            assert snapshot & 0xFF == self.data, "sampled stale/unsettled data"
        self.snapshots.append(snapshot)
        self.rx.append(snapshot)

    # ---- Z80 bus model ------------------------------------------------
    def wait_active(self) -> bool:
        return not self.iorq and not self.levels[RELEASE]  # HCT32: IORQ# OR GP9

    def rising_edge(self) -> None:
        if self.cycle_kind == "T2":
            self.cycle_kind = "IO"
            self.schedule("iorq", False)
            self.schedule("rd" if self.direction == "in" else "wr", False)

    def falling_edge(self) -> None:
        if self.counting:
            self.release_falls += 1
        if self.cycle_kind == "IO":
            self.falls += 1
            if self.falls >= 2:  # TW* and later TWs sample WAIT#
                if not self.wait_active():
                    self.cycle_kind = "T3"
        elif self.cycle_kind == "T3":
            if self.direction == "in":
                assert not self.levels[UP] and self.dirs == DRIVE_DIRS, "IN data not driven"
                self.sampled.append(self.pico_data)
            self.schedule("iorq", True)
            self.schedule("rd" if self.direction == "in" else "wr", True)
            self.cycle_kind = "fetch"
        elif self.cycle_kind == "fetch":
            self.next_fetch_rd_low = self.cycle + self.z80_delay
            self.cycle_kind = None

    def schedule(self, signal: str, value: bool) -> None:
        self.pending.append((self.cycle + self.z80_delay, signal, value))

    def start(self, direction: str, port: int, data: int = 0) -> None:
        self.direction, self.port, self.data = direction, port, data
        self.cycle_kind, self.falls = "T2", 0

    # ---- simulation ---------------------------------------------------
    def step(self, count: int = 1) -> None:
        for _ in range(count):
            for event in [e for e in self.flag_events if e[0] <= self.cycle]:
                self.clock_flags[event[1]] = event[2]
                self.flag_events.remove(event)
            for event in [e for e in self.pending if e[0] <= self.cycle]:
                setattr(self, event[1], event[2])
                self.pending.remove(event)
            if self.next_fetch_rd_low is not None and self.cycle >= self.next_fetch_rd_low:
                assert self.levels[UP] and not self.dirs & ~IDLE_DIRS, \
                    "opcode fetch RD# while the Pico drives D0-D7"
                self.next_fetch_rd_low = None
            inputs = self.inputs.popleft()
            self.inputs.append(self.pins())
            machines = (self.io, self.clock) if self.io_first else (self.clock, self.io)
            for machine in machines:
                machine.step(inputs)
            self.cycle += 1

    def until(self, predicate, limit: int = 2000) -> None:
        for _ in range(limit):
            if predicate():
                return
            self.step()
        raise AssertionError(f"PIO model timed out at PCs {self.clock.pc}/{self.io.pc}")

    def idle(self) -> bool:
        return (self.cycle_kind is None and not self.pending and self.iorq
                and self.levels[UP] and self.levels[DOWN] and not self.levels[RELEASE])

    def run_cycle(self, direction: str, port: int, data: int = 0, phase: int = 0,
                  reply: int | None = None, reply_delay: int = 40) -> None:
        self.step(18 + phase)
        self.start(direction, port, data)
        if direction == "in":
            self.until(lambda: any(not item & (1 << 17) for item in self.rx))
            while self.rx:  # core 0 applies queued OUTs, then answers the IN
                request = self.rx.popleft()
            assert not request & (1 << 17) and request & (1 << 18), "IN request strobes"
            self.step(reply_delay)
            value = data if reply is None else reply
            self.tx.append((value << 2) | int(self.levels[A16]) | DRIVE_DIRS << 10 | IDLE_DIRS << 20)
        self.until(lambda: self.cycle_kind is None and self.next_fetch_rd_low is None
                   and not self.pending)
        self.until(self.idle)
        self.step(12)


def decode(snapshot: int) -> tuple[int, int]:
    nibble = (snapshot >> 8) & 15
    return (nibble & 7) | ((nibble & 8) << 1), snapshot & 0xFF


GENERIC_PORTS = (0x00, 0x02, 0x05, 0x06, 0x10, 0x14, 0x15, 0x17)
IN_PORTS = (0x00, 0x01, 0x04, 0x07, 0x10, 0x14, 0x16, 0x17)


class BankedPioCycles(unittest.TestCase):
    def test_bank_select_all_values_phases_and_latencies(self) -> None:
        for latency in (0, 1, 2):
            for io_first in (False, True):
                for phase in range(6):
                    for value in range(256):
                        model = Model(latency, io_first)
                        model.run_cycle("out", 7 if phase < 3 else 0xEF, value, phase)
                        self.assertEqual(model.bank_events, [value & 1])
                        self.assertEqual(model.levels[A16], bool(value & 1))
                        self.assertEqual(model.snapshots, [])
                        self.assertFalse(model.clock_flags[1])

    def test_generic_out_all_values_at_every_phase(self) -> None:
        for latency in (0, 2):
            for phase in range(6):
                for port in GENERIC_PORTS:
                    for value in range(256):
                        model = Model(latency, phase & 1 == 1)
                        model.run_cycle("out", port | (0xE8 if phase >= 3 else 0), value, phase)
                        self.assertEqual([decode(item) for item in model.rx], [(port, value)])
                        self.assertEqual(model.bank_events, [])
                        self.assertFalse(model.levels[A16])

    def test_in_all_values_phases_latencies_and_delays(self) -> None:
        for latency in (0, 1, 2):
            for z80_delay in (1, 3, 5):
                for phase in range(6):
                    for value in range(256):
                        for bank in (False, True):
                            model = Model(latency, phase & 1 == 1, z80_delay)
                            model.levels[A16] = bank
                            port = IN_PORTS[value % len(IN_PORTS)]
                            model.run_cycle("in", port, value, phase)
                            self.assertEqual(model.sampled, [value])
                            self.assertEqual(decode(model.snapshots[0])[0], port)
                            self.assertEqual(model.levels[A16], bank)
                            self.assertEqual(model.dirs, IDLE_DIRS)
                            self.assertFalse(model.clock_flags[1])

    def test_in_runs_exactly_two_clocks_then_holds_until_isolated(self) -> None:
        for latency in (0, 1, 2):
            model = Model(latency)
            model.step(18)
            model.start("in", 0x01)
            model.until(lambda: len(model.rx) > 0)
            model.rx.popleft()
            model.step(200)
            edges = len(model.edges)
            model.step(200)
            self.assertEqual(len(model.edges), edges, "CLK ran while awaiting the reply")
            model.tx.append(0x42 << 2 | DRIVE_DIRS << 10 | IDLE_DIRS << 20)
            model.until(lambda: model.cycle_kind == "fetch")
            self.assertFalse(model.levels[CLK])
            # WAIT stays released for exactly TW and T3; CLK is then held LOW
            # until U9 is isolated and WAIT rearmed.
            model.until(lambda: not model.levels[RELEASE])
            self.assertEqual(model.last_release_falls, 2)
            self.assertTrue(model.levels[UP])
            model.until(lambda: model.cycle_kind is None)
            self.assertEqual(model.sampled, [0x42])

    def test_queued_outs_precede_the_in_request(self) -> None:
        model = Model()
        model.run_cycle("out", 0x00, ord("A"))
        model.run_cycle("out", 0x14, 0x5A)
        model.run_cycle("in", 0x01, 0x82)
        self.assertEqual([decode(item) for item in model.snapshots[:2]],
                         [(0x00, ord("A")), (0x14, 0x5A)])
        self.assertFalse(model.snapshots[2] & (1 << 17))
        self.assertEqual(model.sampled, [0x82])

    def test_full_fifo_holds_wait_until_core0_drains(self) -> None:
        model = Model()
        model.step(120)
        edges = model.edges[1:]  # skip the model's start marker
        self.assertTrue(all(b - a == 3 for (a, _), (b, _) in zip(edges, edges[1:])))
        for value in range(RX_DEPTH):
            model.run_cycle("out", 0x00, value)
        model.step(18)
        model.start("out", 0x10, 0x5A)
        model.step(400)
        self.assertEqual(len(model.rx), RX_DEPTH)
        self.assertFalse(model.levels[RELEASE], "WAIT released without a FIFO entry")
        self.assertTrue(model.clock_flags[1] and not model.levels[CLK], "CLK not held LOW")
        edges = len(model.edges)
        model.step(100)
        self.assertEqual(len(model.edges), edges)
        self.assertEqual(decode(model.rx.popleft()), (0, 0))
        model.until(model.idle)
        self.assertEqual([decode(item)[1] for item in model.rx], [*range(1, RX_DEPTH), 0x5A])

    def test_bus_grant_reads_pulled_up_controls(self) -> None:
        # During a grant the Z80 tri-states IORQ#/RD#/WR#; the board's 10 kOhm
        # pull-ups hold them HIGH, so the engine parks without decoding.
        model = Model()
        model.step(20)
        model.busack = False
        model.port, model.data = 7, 1
        model.step(200)
        self.assertEqual(model.snapshots, [])
        self.assertEqual(model.bank_events, [])
        self.assertFalse(model.clock_flags[1])
        model.busack = True
        model.run_cycle("out", 7, 1)
        self.assertEqual(model.bank_events, [1])


def load(header: str, name: str) -> tuple[list[int], dict[str, int]]:
    array = header.split(f"{name}_program_instructions[] = {{", 1)[1].split("};", 1)[0]
    words = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{4}),", array)]
    layout = {key: int(re.search(rf"#define {name}_{key} (\d+)", header)[1])
              for key in ("wrap_target", "wrap")}
    for key, value in re.findall(rf"{name}_offset_(\w+) (\d+)u", header):
        layout[key] = int(value)
    return words, layout


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, required=True)
    args = parser.parse_args()
    text = args.program.read_text()
    Model.clock_words, Model.clock_layout = load(text, "banked_clock")
    Model.io_words, Model.io_layout = load(text, "banked_io")
    if len(Model.clock_words) > 32 or len(Model.io_words) > 32:
        raise AssertionError("PIO programs exceed a 32-word instruction store")
    unittest.main(argv=[__file__], verbosity=2)
