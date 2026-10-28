"""Execute the extent allocator benchmark under CP/M with real and modeled BIOS calls."""
from pathlib import Path
import unittest

import banked_heap_z80_test


class AllocPerfExecution(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        banked_heap_z80_test.BankedHeapExecution.setUpClass()
        banked_heap_z80_test.BankedHeapExecution.program = (
            Path(__file__).resolve().parents[3] /
            "examples/ALLOCPRF/build/emulator/ALLOCPRF.COM")

    def execute(self, native=False, times=(100,) * 9):
        case = banked_heap_z80_test.BankedHeapExecution()
        output, banks, active, _ = case.execute_program(
            True, native_bios=native, stopwatch_ms=times)
        self.assertEqual(active, 0)
        self.assertEqual(banks[1][0x10], 8)
        self.assertEqual(banks[1][0x108], 247)
        self.assertEqual(banks[1][0x308], 0)
        return output

    def test_extent_sizes_and_scan_positions(self):
        for native in (False, True):
            with self.subTest(native=native):
                output = self.execute(native)
                self.assertNotIn(b"C reference", output)
                self.assertEqual(output.count(b"Bank 1: 100 ms; 25000 us/pair"), 9)
                self.assertIn(b"Case: empty heap.", output)
                self.assertIn(b"Case: first 16 KiB occupied", output)
                self.assertIn(b"Case: 16 small holes", output)
                for size in (256, 4096, 32768):
                    self.assertEqual(output.count(f"{size} bytes |".encode()), 3)
                self.assertIn(b"PASS: allocator benchmark complete; heap restored.", output)
                self.assertNotIn(b"FAIL:", output)

    def test_timer_failure_and_zero_resolution(self):
        output = self.execute(True, (0xFFFFFFFF,))
        self.assertIn(b"FAIL: allocation, cleanup, or millisecond stopwatch.", output)
        self.assertNotIn(b"PASS:", output)
        output = self.execute(True, (0,) * 9)
        self.assertEqual(output.count(b"<1 ms"), 9)
        self.assertIn(b"PASS:", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
