"""Execute the assembled PIO words with two SMs and two-stage GPIO inputs.

This is a digital protocol model, not RP2350 firmware execution or electrical
qualification. It checks actual instruction encodings, clock phase guards,
port routing and finite FIFO behavior without assuming the assembly is right.
"""
from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import re
import unittest


class StateMachine:
    def __init__(self, model: Model, clock: bool):
        self.model, self.clock = model, clock
        self.pc = 0 if clock else 4
        self.first, self.last = (0, 3) if clock else (4, 30)
        self.x = self.y = self.osr = self.delay = 0
        self.irq_wait = False

    def step(self, inputs: int) -> None:
        if self.delay:
            self.delay -= 1
            return
        m = self.model
        word = m.words[self.pc]
        opcode, arg = word >> 13, word & 255
        if not self.clock and word & 0x1000:
            m.output(9, bool(word & 0x0800))
        following = self.first if self.pc == self.last else self.pc + 1
        if opcode == 0:  # JMP
            condition = (arg >> 5) & 7
            take = {0: True, 5: self.x != self.y, 6: bool(inputs & 1)}[condition]
            if take:
                following = arg & 31
        elif opcode == 1:  # WAIT
            source, index, polarity = (arg >> 5) & 3, arg & 31, bool(arg & 128)
            if source == 0:
                value = bool(inputs & (1 << index))
            elif source == 2:
                value = m.flags[index & 7]
            else:
                raise AssertionError("unexpected WAIT source")
            if value != polarity:
                return
        elif opcode == 3:  # OUT, right shifting
            count = (arg & 31) or 32
            value = self.osr & ((1 << count) - 1)
            self.osr >>= count
            destination = arg >> 5
            if destination == 0:
                assert count == 1
                assert not m.levels[2] and not m.levels[7]
                assert not m.iorq and not m.wr and m.rd
                assert value == (m.data & 1), "sampled stale/unsettled data"
                m.bank_events.append((m.cycle, value))
                m.output(8, value)
            elif destination == 1:
                self.x = value
            else:
                assert destination == 3  # NULL
        elif opcode == 4:  # PUSH NOBLOCK
            assert arg == 0
            if len(m.fifo) < 4:
                m.fifo.append(0)
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
        elif opcode == 6:  # IRQ SET / CLEAR / WAIT
            index = arg & 7
            if arg & 0x40:
                m.flags[index] = False
            elif arg & 0x20:
                if not self.irq_wait:
                    m.flags[index] = True
                    self.irq_wait = True
                if m.flags[index]:
                    return
                self.irq_wait = False
            else:
                m.flags[index] = True
        elif opcode == 7:  # SET
            destination, value = arg >> 5, arg & 31
            if destination == 0:
                m.output(2 if self.clock else 7, value & 1)
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
    def __init__(self, words: list[int], io_first: bool = False):
        self.words = words
        self.io_first = io_first
        self.cycle = 0
        self.flags = [False] * 8
        self.levels = {2: False, 6: True, 7: True, 8: False, 9: False}
        self.iorq = self.rd = self.wr = self.busack = True
        self.port = self.data = 0
        self.fifo: deque[int] = deque()
        self.bank_events: list[tuple[int, int]] = []
        self.edges: list[tuple[int, bool]] = [(-3, False)]
        self.release = None
        self.receive_since = -100
        self.clock = StateMachine(self, True)
        self.io = StateMachine(self, False)
        self.inputs = deque([self.pins(), self.pins()])

    def pins(self) -> int:
        value = sum(int(level) << pin for pin, level in self.levels.items())
        for pin, level in ((0, self.busack), (1, self.iorq), (27, self.rd), (28, self.wr)):
            value |= int(level) << pin
        for bit, port_bit in enumerate((0, 1, 2, 4)):
            value |= ((self.port >> port_bit) & 1) << (18 + bit)
        # Model one system-clock propagation through LVC245 plus two GPIO
        # synchronizer cycles. Isolated inputs deliberately contain stale data.
        received = self.data if not self.levels[7] and self.cycle > self.receive_since else 0xAA
        return value | (received << 10)

    def output(self, pin: int, value: bool) -> None:
        old = self.levels[pin]
        self.levels[pin] = bool(value)
        if pin == 7 and old and not value:
            self.receive_since = self.cycle
        if pin == 9 and not old and value:
            assert not self.levels[2], "WAIT released while CLK HIGH"
            self.release = self.cycle
        if pin == 2 and old != bool(value):
            assert self.cycle - self.edges[-1][0] >= 3, "short clock pulse"
            self.edges.append((self.cycle, bool(value)))
            if value and self.release is not None:
                assert self.cycle - self.release >= 8, "WAIT/A16 settling guard lost"
                self.release = None

    def step(self, count: int = 1) -> None:
        for _ in range(count):
            inputs = self.inputs.popleft()
            self.inputs.append(self.pins())
            machines = (self.io, self.clock) if self.io_first else (self.clock, self.io)
            for machine in machines:
                machine.step(inputs)
            self.cycle += 1

    def until(self, predicate, limit: int = 1000) -> None:
        for _ in range(limit):
            if predicate():
                return
            self.step()
        raise AssertionError(f"PIO model timed out at PCs {self.clock.pc}/{self.io.pc}")

    def out_bank(self, data: int, phase: int = 0, port: int = 7) -> None:
        self.step(18 + phase)
        self.data, self.port = data, port
        self.rd, self.wr, self.iorq = True, False, False
        self.until(lambda: self.levels[9])
        self.until(lambda: self.release is None)  # first settled completion clock
        if phase & 1:
            self.wr = True
            self.step(3)
            self.iorq = True
        else:
            self.iorq = True
            self.step(3)
            self.wr = True
        self.until(lambda: not self.levels[9])
        self.step(12)


class BankedPioCycles(unittest.TestCase):
    words: list[int]

    def test_all_data_values_at_every_clock_phase(self) -> None:
        for io_first in (False, True):
            for phase in range(6):
                for value in range(256):
                    model = Model(self.words, io_first=io_first)
                    model.out_bank(value, phase, port=7 if phase < 3 else 0xEF)
                    self.assertEqual([bank for _, bank in model.bank_events], [value & 1])
                    self.assertEqual(model.levels[8], bool(value & 1))
                    self.assertTrue(model.levels[7])
                    self.assertFalse(model.levels[9] or model.flags[0] or model.flags[1])

    def test_other_ports_inputs_and_invalid_strobes_route_to_arm(self) -> None:
        for port in (*range(8), *range(0x10, 0x18)):
            for rd, wr in ((False, True), (True, False), (True, True), (False, False)):
                if port == 7 and rd and not wr:
                    continue
                model = Model(self.words)
                model.step(20)
                model.port, model.rd, model.wr, model.iorq = port, rd, wr, False
                model.until(lambda: model.flags[0])
                self.assertEqual(model.bank_events, [])
                self.assertTrue(model.levels[7])
                self.assertFalse(model.levels[9] or model.flags[1])
                model.flags[2] = True
                model.step(8)
                self.assertFalse(model.levels[2])
                model.rd = model.wr = model.iorq = True
                model.flags[0] = model.flags[2] = False
                model.step(20)
                self.assertFalse(model.flags[0])

    def test_fifo_full_never_stalls_and_clock_is_20mhz(self) -> None:
        model = Model(self.words)
        model.step(120)
        self.assertTrue(all(b - a == 3 for (a, _), (b, _) in zip(model.edges, model.edges[1:])))
        for value in range(32):
            model.out_bank(value)
        self.assertEqual(len(model.bank_events), 32)
        self.assertEqual(len(model.fifo), 4)

    def test_bus_grant_ignores_floating_iorq(self) -> None:
        model = Model(self.words)
        model.step(20)
        model.busack = False
        model.step(10)
        model.port, model.data, model.rd, model.wr, model.iorq = 7, 1, True, False, False
        model.step(200)
        self.assertEqual(model.bank_events, [])
        self.assertFalse(model.flags[0])
        model.rd = model.wr = model.iorq = True
        model.busack = True
        model.out_bank(1)
        self.assertEqual(len(model.bank_events), 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, required=True)
    args = parser.parse_args()
    array = args.program.read_text().split("banked_io_program_instructions[] = {", 1)[1].split("};", 1)[0]
    BankedPioCycles.words = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{4}),", array)]
    if len(BankedPioCycles.words) != 31:
        raise AssertionError("expected clock+I/O program to fit 31 instruction words")
    unittest.main(argv=[__file__], verbosity=2)
