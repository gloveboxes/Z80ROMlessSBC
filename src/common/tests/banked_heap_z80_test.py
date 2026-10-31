"""Run banked-memory example COM files on the real CP/M, BIOS and bank-1 image.

Bank switching, the mirrored gateway, the bank-0 BDOS shim and every bank-1
service execute as Z80 code; only console, disk and the stopwatch port are
modeled. This checks software behaviour, not physical bus timing.
"""
from pathlib import Path
import argparse
import unittest

import bank1_system_boot_z80_test as system_boot

ROOT = Path(__file__).resolve().parents[3]
TIMER_PORT = 0x17


class BankedHeapExecution(unittest.TestCase):
    program: Path = ROOT / "examples/BANKED/build/emulator/BANKED.COM"

    @classmethod
    def setUpClass(cls) -> None:
        system_boot.BankOneSystemBoot.setUpClass()

    def execute_program(self, available: bool = True,
                        stopwatch_ms: tuple[int, ...] | None = None,
                        timer_dirty: str | None = None,
                        seconds_per_poll: int = 1,
                        fault: str | None = None,
                        ) -> tuple[bytes, list[bytearray], int, int]:
        """Return (output, banks, final bank, bank-select count)."""
        boot = system_boot.BankOneSystemBoot("test_directory_file_read_and_warm_boot")
        boot.setUp()
        case, machine = boot.case, boot.case.machine
        case.until_prompt()
        while boot.active:  # finish the CCP's proxied call after its prompt
            machine.ticks_to_stop = 1000
            machine.run()
        switches = 0
        timer = {"seconds": 7200, "position": 4, "reads": 0, "started": False,
                 "command": 0xFF if timer_dirty == "error" else 0x30,
                 "pending": 2 if timer_dirty == "parameters" else 0}

        def read_port(port: int) -> int:
            if port & 255 == 7:
                return (0xB4 | boot.active) if available else 0
            if port & 255 != TIMER_PORT:
                return case.read_port(port)
            self.assertEqual(boot.active, 0, "timer read from bank 1")
            if timer["pending"]:
                timer["pending"] = 0
                timer["command"] = 0xFF
            if timer["command"] != 0x30 or timer["position"] >= 4:
                timer["command"] = 0xFF
                return 0xFF if timer["command"] == 0xFF and timer["position"] >= 4 else 0
            position = timer["position"]
            timer["position"] = position + 1
            return (timer["seconds"] >> (8 * position)) & 0xFF

        def write_port(port: int, value: int) -> None:
            nonlocal switches
            if port & 255 == 7:
                boot.banks[boot.active][:] = machine.memory
                boot.active = value & 1
                machine.memory[:] = boot.banks[boot.active]
                switches += 1
                return
            if port & 255 != TIMER_PORT:
                case.write_port(port, value)
                return
            self.assertEqual(boot.active, 0, "timer written from bank 1")
            if timer["pending"]:
                timer["pending"] -= 1
            elif value == 0:
                timer["command"] = 0x30
            elif timer["command"] == 0xFF:
                pass
            elif value == 0x40:
                timer["started"] = True
                timer["seconds"] = 0
            elif value in (0x50, 0x60):
                self.assertTrue(timer["started"], "stopwatch latched before start")
                if stopwatch_ms is None:
                    self.assertEqual(value, 0x50)
                    timer["seconds"] += seconds_per_poll
                else:
                    self.assertEqual(value, 0x60, "benchmarks read milliseconds")
                    self.assertLess(timer["reads"], len(stopwatch_ms))
                    timer["seconds"] = stopwatch_ms[timer["reads"]]
                    timer["reads"] += 1
                timer["position"] = 0
            else:
                timer["command"] = 0xFF

        machine.set_input_callback(read_port)
        machine.set_output_callback(write_port)
        cpu_run = run = machine.run
        if fault == "no_read":
            # READ reports success for large application copies but moves nothing.
            machine.set_breakpoint(0xFF00)

            def run_with_fault() -> int:
                while True:
                    events = cpu_run()
                    if not events & machine._BREAKPOINT_HIT:
                        return events
                    caller = int.from_bytes(machine.memory[machine.sp:machine.sp + 2], "little")
                    if machine.bc >= 256 and caller < 0xE600:
                        machine.a, machine.f = 0, 0
                        machine.pc = caller
                        machine.sp += 2
                    else:
                        machine.step_over_breakpoint()
                    if not machine.ticks_to_stop:
                        return events & ~machine._BREAKPOINT_HIT
            run = run_with_fault

        program = self.program.read_bytes()
        machine.memory[0x100:0x100 + len(program)] = program
        machine.pc = 0x100
        start = len(case.output)
        for _ in range(200000):
            machine.ticks_to_stop = 1000000
            run()
            self.assertFalse(machine.halted)
            if case.output[start:].endswith(b">"):
                break
        else:
            self.fail(f"No prompt within banked-app budget: {bytes(case.output[-400:])!r}")
        while boot.active:
            machine.ticks_to_stop = 1000
            run()
        boot.banks[0][:] = machine.memory
        return bytes(case.output[start:]), boot.banks, boot.active, switches

    def assert_pool_restored(self, banks: list[bytearray]) -> None:
        bank = banks[1]
        self.assertEqual(bank[0xE610], 8, "free list starts at C800h")
        self.assertEqual(int.from_bytes(bank[0xE614:0xE616], "little"), 0x1600)
        self.assertEqual(bank[0xE708], 22, "one coalesced 5,632-byte extent")

    def test_unavailable_bank_exits_without_switching(self) -> None:
        output, banks, active, _ = self.execute_program(available=False)
        self.assertIn(b"banked SRAM unavailable", output, output.decode(errors="replace"))
        self.assertEqual(active, 0)
        self.assert_pool_restored(banks)

    def test_compiled_c_allocator_and_copy(self) -> None:
        output, banks, active, switches = self.execute_program()
        self.assertIn(b"PASS: cycle 1; all 5632 usable bytes verified with 24 patterns.",
                      output, output.decode(errors="replace"))
        self.assertIn(b"Free banked memory: 5632 bytes", output)
        self.assertIn(b"PASS: finite emulator banked-memory cycle completed.", output)
        self.assertNotIn(b"FAIL:", output)
        self.assertEqual(active, 0)
        self.assertGreater(switches, 1000)
        self.assert_pool_restored(banks)

    def test_dirty_timer_parser_is_recovered(self) -> None:
        for state in ("parameters", "error"):
            with self.subTest(state=state):
                output, _, _, _ = self.execute_program(timer_dirty=state)
                self.assertIn(b"Iterations completed: 1", output,
                              output.decode(errors="replace"))

    def test_timer_expiry_does_not_claim_a_complete_iteration(self) -> None:
        output, banks, _, _ = self.execute_program(seconds_per_poll=1000)
        self.assertIn(b"PASS: 12-hour banked memory endurance run completed.", output,
                      output.decode(errors="replace"))
        self.assertIn(b"Iterations completed: 0", output)
        self.assertNotIn(b"PASS: cycle 1;", output)
        self.assert_pool_restored(banks)

    def test_compiled_direct_offset_primes(self) -> None:
        self.program = ROOT / "examples/PRIMEBK1/build/PRIMEBK1.COM"
        output, banks, active, switches = self.execute_program()
        self.assertIn(b"Count <= 30: 10 primes; last=29; HL=10", output,
                      output.decode(errors="replace"))
        self.assertIn(b"Next after 30: 31; HL=31", output)
        self.assertIn(b"Count <= 50: 15 primes; last=47; HL=15", output)
        self.assertIn(b"Next after 50: 53; HL=53", output)
        self.assertIn(b"Next after 254: 0; HL=0", output)
        self.assertNotIn(b"FAIL:", output)
        self.assertEqual(active, 0)
        self.assert_pool_restored(banks)

    def test_compiled_spreads_two_entries(self) -> None:
        self.program = ROOT / "examples/SPREADS/build/SPREADS.COM"
        output, banks, active, switches = self.execute_program()
        self.assertIn(b"SPREADS: two bank-1 entry points.\r\nVEGEMITE\r\nMARMITE\r\nVEG\r\n",
                      output, output.decode(errors="replace"))
        self.assertIn(b"PASS: both entry points ran and the bank-1 heap was restored.", output)
        self.assertEqual(active, 0)
        self.assert_pool_restored(banks)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, default=BankedHeapExecution.program)
    args, remaining = parser.parse_known_args()
    BankedHeapExecution.program = args.program
    unittest.main(argv=[__file__, *remaining], verbosity=2)
