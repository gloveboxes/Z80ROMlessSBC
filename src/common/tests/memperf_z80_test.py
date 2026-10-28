"""Execute MEMPERF.COM with real CP/M/BIOS and a modeled millisecond timer."""
from pathlib import Path
import argparse
import unittest

import banked_heap_z80_test as banked_fixture


class MemPerfExecution(unittest.TestCase):
    program: Path

    @classmethod
    def setUpClass(cls) -> None:
        banked_fixture.BankedHeapExecution.setUpClass()

    def execute(self, native: bool = True, **kwargs) -> bytes:
        case = banked_fixture.BankedHeapExecution()
        case.program = self.program
        values = kwargs.pop("stopwatch_ms", (64, 65, 34, 35, 512, 513, 36, 37))
        output, banks, active, _ = case.execute_program(
            kwargs.pop("available", True), native_bios=native,
            stopwatch_ms=values, **kwargs)
        self.assertEqual(active, 0)
        self.assertEqual(banks[1][0x10], 8)
        self.assertEqual(banks[1][0x108], 247)
        self.assertEqual(banks[1][0x308], 0)
        return output

    def test_bulk_and_chunked_transfers_and_rates(self) -> None:
        for native in (False, True):
            with self.subTest(native=native):
                output = self.execute(native=native)
                self.assertIn(b"base RAM 2 x 16384-byte copies; banked RAM 1 x 32768-byte call", output)
                self.assertIn(b"256-byte chunks: 128 calls per direction for both memory types", output)
                self.assertIn(b"Direction | Z80 base RAM | Banked RAM", output)
                self.assertIn(b"Write     | 34 ms; 941.1 KiB/s | 64 ms; 500.0 KiB/s", output)
                self.assertIn(b"Read      | 35 ms; 914.2 KiB/s | 65 ms; 492.3 KiB/s", output)
                self.assertIn(b"Write     | 36 ms; 888.8 KiB/s | 512 ms; 62.5 KiB/s", output)
                self.assertIn(b"Read      | 37 ms; 864.8 KiB/s | 513 ms; 62.3 KiB/s", output)
                self.assertEqual(output.count(b"PASS: banked payload and both local RAM"), 2)
                self.assertIn(b"PASS: MEMPERF complete; 63232 bytes free.", output)
                self.assertNotIn(b"FAIL:", output)
                if not native:
                    print(output.decode(errors="replace"))

    def test_zero_millisecond_reading_does_not_divide_by_zero(self) -> None:
        output = self.execute(stopwatch_ms=(0,) * 8)
        self.assertEqual(output.count(b"<1 ms (below timer resolution)"), 8)
        self.assertIn(b"PASS: MEMPERF complete", output)

    def test_read_success_without_full_copy_is_detected(self) -> None:
        for fault, offset in (("no_read", 0), ("partial_read", 32767)):
            with self.subTest(fault=fault):
                output = self.execute(fault=fault)
                self.assertIn(f"FAIL: verification at offset {offset},".encode(), output)
                self.assertNotIn(b"PASS:", output)

    def test_parser_recovery_and_stopwatch_failure(self) -> None:
        output = self.execute(timer_dirty="parameters")
        self.assertIn(b"PASS: MEMPERF complete", output)
        output = self.execute(stopwatch_ms=(0xFFFFFFFF,))
        self.assertIn(b"FAIL: millisecond stopwatch read after write", output)
        self.assertNotIn(b"PASS:", output)
        output = self.execute(stopwatch_ms=(64, 65, 0xFFFFFFFF))
        self.assertIn(b"FAIL: local RAM write stopwatch read", output)
        self.assertNotIn(b"PASS:", output)

    def test_new_bios_with_missing_firmware_allocator_exits_cleanly(self) -> None:
        output = self.execute(native=False, allocator_installed=False)
        self.assertIn(b"Bank-1 allocator missing; flash matching Phase 10 firmware", output)
        self.assertNotIn(b"PASS:", output)

    def test_missing_bank_exits_cleanly(self) -> None:
        output = self.execute(native=False, available=False)
        self.assertIn(b"FAIL: banked SRAM unavailable", output)
        self.assertIn(b"BIOS INFO reports banked SRAM unavailable", output)

    def test_missing_descriptor_and_bad_heap_have_distinct_diagnostics(self) -> None:
        output = self.execute(signature=b"OLD!")
        self.assertIn(b"BIOS bank extension descriptor missing or incompatible", output)
        output = self.execute(heap_signature=bytes(4))
        self.assertIn(b"Heap signature: 00 00 00 00; expected 42 4b 4d 33", output)
        self.assertNotIn(b"PASS:", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, default=Path(__file__).resolve().parents[3] /
                        "examples/MEMPERF/build/MEMPERF.COM")
    args = parser.parse_args()
    MemPerfExecution.program = args.program
    unittest.main(argv=[__file__], verbosity=2)
