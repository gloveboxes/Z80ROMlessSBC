"""Execute MEMPERF.COM on the real CP/M/BIOS and bank-1 image with a modeled millisecond timer."""
from pathlib import Path
import argparse
import unittest

import banked_heap_z80_test as banked_fixture


class MemPerfExecution(unittest.TestCase):
    program = banked_fixture.ROOT / "examples/MEMPERF/build/MEMPERF.COM"

    @classmethod
    def setUpClass(cls) -> None:
        banked_fixture.BankedHeapExecution.setUpClass()

    def execute(self, **kwargs) -> bytes:
        case = banked_fixture.BankedHeapExecution()
        case.program = self.program
        kwargs.setdefault("stopwatch_ms", (64, 65, 34, 35, 512, 513, 36, 37))
        output, banks, active, _ = case.execute_program(**kwargs)
        self.assertEqual(active, 0)
        case.assert_pool_restored(banks)
        return output

    def test_bulk_and_chunked_transfers_and_rates(self) -> None:
        output = self.execute()
        self.assertIn(b"Bulk: base RAM 2 x 2048-byte copies; banked RAM 1 x 4096-byte call.", output)
        self.assertIn(b"256-byte chunks: 16 calls per direction for both memory types.", output)
        self.assertIn(b"Direction | Z80 base RAM | Banked RAM", output)
        self.assertIn(b"Write     | 34 ms; 3764.7 KiB/s | 64 ms; 2000.0 KiB/s", output)
        self.assertIn(b"Read      | 35 ms; 3657.1 KiB/s | 65 ms; 1969.2 KiB/s", output)
        self.assertIn(b"Write     | 36 ms; 3555.5 KiB/s | 512 ms; 250.0 KiB/s", output)
        self.assertIn(b"Read      | 37 ms; 3459.4 KiB/s | 513 ms; 249.5 KiB/s", output)
        self.assertEqual(output.count(b"PASS: banked payload and both local RAM"), 2)
        self.assertIn(b"PASS: MEMPERF complete; 5632 bytes free.", output)
        self.assertNotIn(b"FAIL:", output)

    def test_zero_millisecond_reading_does_not_divide_by_zero(self) -> None:
        output = self.execute(stopwatch_ms=(0,) * 8)
        self.assertEqual(output.count(b"<1 ms (below timer resolution)"), 8)
        self.assertIn(b"PASS: MEMPERF complete", output)

    def test_read_success_without_copy_is_detected(self) -> None:
        output = self.execute(fault="no_read")
        self.assertIn(b"FAIL: verification at offset 0,", output, output.decode(errors="replace"))
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

    def test_unavailable_bank_exits_cleanly(self) -> None:
        output = self.execute(available=False)
        self.assertIn(b"FAIL: banked SRAM unavailable", output)
        self.assertIn(b"Banked SRAM unavailable: INFO 0000, status 1.", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, default=MemPerfExecution.program)
    args, remaining = parser.parse_known_args()
    MemPerfExecution.program = args.program
    unittest.main(argv=[__file__, *remaining], verbosity=2)
