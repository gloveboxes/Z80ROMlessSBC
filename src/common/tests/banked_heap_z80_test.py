"""Run the compiled dcc BANKED.COM through real CP/M/BIOS and banked SRAM."""
from pathlib import Path
import argparse
import unittest

from extent_model import ExtentModel
import cpm_boot_z80_test
from banked_memory_z80_test import initialize_bank, transfer_code


class BankedHeapExecution(unittest.TestCase):
    program: Path

    @classmethod
    def setUpClass(cls) -> None:
        cpm_boot_z80_test.CpmBootExecution.setUpClass()

    def execute_program(self, available: bool, capability: int = 0xB4,
                        native_bios: bool = False, abi_minor: int = 0, abi_major: int = 2,
                        signature: bytes | None = None, fault: str | None = None,
                        heap_signature: bytes | None = None,
                        allocator_installed: bool = True,
                        timer_dirty: str | None = None,
                        seconds_per_poll: int = 1,
                        stopwatch_ms: tuple[int, ...] | None = None) -> tuple[bytes, list[bytearray], int, int]:
        case = cpm_boot_z80_test.CpmBootExecution()
        case.setUp()
        machine = case.machine
        banks = [bytearray(65536), initialize_bank()]
        if heap_signature is not None:
            self.assertEqual(len(heap_signature), 4)
            banks[1][:4] = heap_signature
        if not allocator_installed:
            banks[1][4:8] = bytes(4)
            banks[1][0x60:0x100] = bytes(0xA0)
        extent_model = ExtentModel()
        active = 0
        switches = 0
        tracking = False
        dma_writes = []
        capability_reads = 0
        timer = {"seconds": 7200, "position": 4, "reads": 0, "started": False,
                 "command": 0xFF if timer_dirty == "error" else 0x30,
                 "pending": 2 if timer_dirty == "parameters" else 0}
        timing = False
        payload_calls = []
        measurements = []

        def memory_write(address: int, value: int) -> None:
            if tracking and active == 0 and 0x80 <= address < 0x100:
                dma_writes.append(address)
            machine.memory[address] = value

        def read_port(port: int) -> int:
            nonlocal tracking, capability_reads
            if port & 0x17 == 7:
                self.assertFalse(native_bios, "client bypassed the portable BIOS interface")
                capability_reads += 1
                tracking = True
                return (capability | active) if available else 0
            self.assertEqual(active, 0, "CP/M I/O executed from extra bank")
            if port & 0x17 == 0x17:
                if timer["pending"]:
                    timer["pending"] = 0
                    timer["command"] = 0xFF
                if timer["command"] == 0xFF:
                    return 0xFF
                if timer["command"] == 0x30:
                    return 0
                position = timer["position"]
                if position >= 4:
                    timer["command"] = 0xFF
                    return 0xFF
                timer["position"] = position + 1
                return (timer["seconds"] >> (8 * position)) & 0xFF
            return case.read_port(port)

        def write_port(port: int, value: int) -> None:
            nonlocal active, switches, tracking, timing, payload_calls
            if port & 0x17 == 7:
                self.assertFalse(native_bios, "client selected hardware banks directly")
                banks[active][:] = machine.memory
                active = value & 1
                machine.memory[:] = banks[active]
                switches += 1
            else:
                if port & 0x17 == 5 and value == 0xC0:
                    tracking = False
                self.assertEqual(active, 0, "CP/M I/O executed from extra bank")
                if port & 0x17 == 0x17:
                    if timer["pending"]:
                        timer["pending"] -= 1
                        return
                    if value == 0:
                        timer["command"] = 0x30
                    elif timer["command"] == 0xFF:
                        return
                    elif value == 0x40:
                        timer["started"] = True
                        timer["seconds"] = 0
                        timer["command"] = 0x30
                        if stopwatch_ms is not None:
                            self.assertFalse(timing, "stopwatch restarted before latching")
                            timing = True
                            payload_calls = []
                    elif value in (0x50, 0x60):
                        self.assertTrue(timer["started"], "stopwatch latched before start")
                        if stopwatch_ms is None:
                            self.assertEqual(value, 0x50)
                            timer["seconds"] += seconds_per_poll
                        else:
                            self.assertEqual(value, 0x60, "MEMPERF must read milliseconds")
                            self.assertTrue(timing)
                            self.assertLess(timer["reads"], len(stopwatch_ms))
                            timer["seconds"] = stopwatch_ms[timer["reads"]]
                            measurements.append(payload_calls)
                            timing = False
                        timer["position"] = 0
                        timer["reads"] += 1
                        timer["command"] = 0x50
                    else:
                        self.fail(f"Unexpected timer command {value:02x}")
                else:
                    if stopwatch_ms is not None and port & 0x17 == 0:
                        self.assertFalse(timing, "console output inside timed interval")
                    case.write_port(port, value)

        machine.set_input_callback(read_port)
        machine.set_output_callback(write_port)
        machine.set_write_callback(memory_write)
        machine.mark_addrs(0x80, 0x80, machine.WRITE_MARK)
        case.until_prompt()
        machine.memory[0xFF0E] = abi_minor
        machine.memory[0xFF0D] = abi_major
        if signature is not None:
            machine.memory[0xFF09:0xFF0D] = signature
        code = transfer_code()
        if native_bios:
            # Model an ESP-style BIOS backend that copies host arrays directly.
            # Its private workspace deliberately differs from the SBC's.
            machine.memory[0xFFF0:] = bytes([0xA5]) * 16
            for entry in (0xFF00, 0xFF03, 0xFF06, 0xFF10):
                machine.set_breakpoint(entry)
            cpu_run = machine.run

            def run_bios_backend() -> int:
                nonlocal capability_reads, tracking
                while True:
                    events = cpu_run()
                    if not events & machine._BREAKPOINT_HIT:
                        return events
                    entry = machine.pc
                    self.assertIn(entry, (0xFF00, 0xFF03, 0xFF06, 0xFF10))
                    error = False
                    if entry == 0xFF06:
                        capability_reads += 1
                        tracking = True
                        machine.hl, machine.de, machine.bc = 0x200, 0x800, 63232
                    elif entry == 0xFF10:
                        machine.hl = extent_model.call(machine.e, machine.hl)
                        extent_model.write_tables(banks[1])
                    else:
                        base, bank, length = machine.hl, machine.de, machine.bc
                        error = bool(length and (length % 16 or base < 0x100 or
                                     base + length > 0xFF00 or bank + length > 0xFF00))
                        if not error and stopwatch_ms is not None and timing and length >= 256:
                            payload_calls.append((entry, length))
                        if fault == "alias" and bank >= 0x800:
                            bank = 0x800 + ((bank - 0x800) & 0x3FFF)
                        if error or not length:
                            pass  # ABI: invalid ranges fail; zero count succeeds.
                        elif entry == 0xFF00:
                            if not (fault == "no_read" and length >= 256):
                                copied = length - 1 if fault == "partial_read" and length >= 256 else length
                                machine.memory[base:base + copied] = banks[1][bank:bank + copied]
                        else:
                            banks[1][bank:bank + length] = machine.memory[base:base + length]
                    machine.a, machine.f = (2, 1) if error else (0, 0)
                    machine.pc = int.from_bytes(machine.memory[machine.sp:machine.sp + 2], "little")
                    machine.sp += 2
                    if not machine.ticks_to_stop:
                        return events & ~machine._BREAKPOINT_HIT

            machine.run = run_bios_backend
        base_code = bytes(machine.memory[0xFF00:0xFF00 + len(code)])
        program = self.program.read_bytes()
        machine.memory[0x100:0x100 + len(program)] = program
        machine.pc = 0x100
        # The endurance app's finite emulator build traverses every usable
        # bank byte, which takes more than the generic CP/M command budget.
        # Let it run in larger slices while preserving a finite guard.
        start = len(case.output)
        for _ in range(200000):
            machine.ticks_to_stop = 1000000
            machine.run()
            self.assertFalse(machine.halted)
            if case.output[start:].endswith(b">"):
                break
        else:
            self.fail(f"No prompt within banked-app execution budget: {bytes(case.output)!r}")
        output = bytes(case.output[start:])
        self.assertEqual(dma_writes, [], "banked C library wrote into CP/M DMA area")
        self.assertLessEqual(capability_reads, 2, "capability should be cached between chunks")
        if b"PASS: cycle 1;" in output:
            self.assertTrue(timer["started"])
            self.assertEqual(timer["reads"], 24 * ((2 * (63232 // 256) + 7) // 8))
            self.assertEqual(timer["seconds"], timer["reads"])
            self.assertIn(b"Iterations completed: 1", output)
            self.assertIn(b"Test plan:", output)
            self.assertIn(b"full-bank write, then independent read/compare", output)
        if b"PASS: MEMPERF complete" in output:
            self.assertEqual(timer["reads"], 8)
            self.assertFalse(timing)
            if native_bios:
                self.assertEqual([len(calls) for calls in measurements], [1, 1, 0, 0, 128, 128, 0, 0])
                for index, calls in enumerate(measurements):
                    if not calls:
                        continue
                    self.assertEqual(sum(length for _, length in calls), 32768)
                    self.assertTrue(all(entry == (0xFF03 if index % 2 == 0 else 0xFF00)
                                        for entry, _ in calls))
        self.assertEqual(bytes(machine.memory[0xFF00:0xFF00 + len(code)]), base_code)
        self.assertEqual(bytes(banks[1][0xFF00:0xFF00 + len(code)]), code)
        if native_bios:
            self.assertEqual(bytes(machine.memory[0xFFF0:]), bytes([0xA5]) * 16)
        return output, banks, active, switches

    def test_missing_bank_exits_without_switching(self) -> None:
        output, _, active, switches = self.execute_program(False)
        self.assertIn(b"banked SRAM unavailable", output)
        self.assertEqual((active, switches), (0, 0))

    def test_missing_alloc_abi_is_rejected(self) -> None:
        output, _, active, switches = self.execute_program(True, abi_major=1)
        self.assertIn(b"banked SRAM unavailable", output)
        self.assertEqual((active, switches), (0, 0))

    def test_old_firmware_abi_is_rejected(self) -> None:
        for capability in (0xB0, 0xB2):
            with self.subTest(capability=capability):
                output, _, active, switches = self.execute_program(True, capability=capability)
                self.assertIn(b"banked SRAM unavailable", output)
                self.assertEqual((active, switches), (0, 0))

    def test_bios_without_extension_is_rejected_before_calling_it(self) -> None:
        output, _, active, switches = self.execute_program(True, signature=b"OLD!")
        self.assertIn(b"banked SRAM unavailable", output)
        self.assertEqual((active, switches), (0, 0))

    def test_same_com_uses_native_bios_without_private_workspace_or_ports(self) -> None:
        output, _, active, switches = self.execute_program(True, native_bios=True)
        self.assertIn(b"PASS: cycle 1; all 63232 usable bytes verified with 24 patterns.", output)
        self.assertEqual((active, switches), (0, 0))

    def test_compiled_c_allocator_and_copy(self) -> None:
        output, banks, active, switches = self.execute_program(True)
        self.assertIn(b"PASS: cycle 1; all 63232 usable bytes verified with 24 patterns.",
                      output, output.decode(errors="replace"))
        self.assertIn(b"Free banked memory: 63232 bytes", output)
        self.assertEqual(active, 0)
        self.assertEqual(banks[1][0x10], 8)
        self.assertEqual(banks[1][0x108], 247)
        self.assertEqual(banks[1][0x308], 0)
        self.assertEqual(banks[1][0x800], 0x7F)
        self.assertGreater(switches, 10000)
        print(output.decode(errors="replace")[-600:])
        print(f"Bank switches: {switches}; final bank: {active}")

    def test_success_without_copying_reads_is_detected(self) -> None:
        for fault, offset in (("no_read", 0), ("partial_read", 255)):
            with self.subTest(fault=fault):
                output, _, active, _ = self.execute_program(True, native_bios=True, fault=fault)
                self.assertIn(f"FAIL: pattern 0 mismatch at {offset},".encode(), output)
                self.assertNotIn(b"PASS:", output)
                self.assertEqual(active, 0)

    def test_heap_aliasing_is_detected(self) -> None:
        output, _, active, _ = self.execute_program(True, native_bios=True, fault="alias")
        self.assertIn(b"FAIL: pattern 6 mismatch", output)
        self.assertNotIn(b"PASS:", output)
        self.assertEqual(active, 0)

    def test_dirty_timer_parser_is_recovered(self) -> None:
        for state in ("parameters", "error"):
            with self.subTest(state=state):
                output, _, _, _ = self.execute_program(True, native_bios=True, timer_dirty=state)
                self.assertIn(b"Iterations completed: 1", output)

    def test_timer_expiry_does_not_claim_a_complete_iteration(self) -> None:
        output, _, _, _ = self.execute_program(True, native_bios=True, seconds_per_poll=1000)
        self.assertIn(b"PASS: 12-hour banked memory endurance run completed.", output)
        self.assertIn(b"Iterations completed: 0", output)
        self.assertNotIn(b"PASS: cycle 1;", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, default=Path(__file__).resolve().parents[3] /
                        "examples/BANKED/build/BANKED.COM")
    args = parser.parse_args()
    BankedHeapExecution.program = args.program
    unittest.main(argv=[__file__], verbosity=2)
