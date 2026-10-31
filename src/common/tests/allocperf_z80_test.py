"""Execute the extent allocator benchmark on the real CP/M/BIOS and bank-1 image."""
import unittest

import banked_heap_z80_test as banked_fixture


class AllocPerfExecution(unittest.TestCase):
    program = banked_fixture.ROOT / "examples/ALLOCPRF/build/emulator/ALLOCPRF.COM"

    @classmethod
    def setUpClass(cls):
        banked_fixture.BankedHeapExecution.setUpClass()

    def execute(self, times=(100,) * 9):
        case = banked_fixture.BankedHeapExecution()
        case.program = self.program
        output, banks, active, _ = case.execute_program(stopwatch_ms=times)
        self.assertEqual(active, 0)
        case.assert_pool_restored(banks)
        return output

    def test_extent_sizes_and_scan_positions(self):
        output = self.execute()
        self.assertEqual(output.count(b"Bank 1: 100 ms; 25000 us/pair"), 9)
        self.assertIn(b"Case: empty heap.", output)
        self.assertIn(b"Case: first 1 KiB occupied.", output)
        self.assertIn(b"Case: four small holes separated by live allocations.", output)
        for size in (256, 1024, 2048):
            self.assertEqual(output.count(f"{size} bytes |".encode()), 3)
        self.assertIn(b"PASS: allocator benchmark complete; heap restored.", output)
        self.assertNotIn(b"FAIL:", output)

    def test_timer_failure_and_zero_resolution(self):
        output = self.execute((0xFFFFFFFF,))
        self.assertIn(b"FAIL: allocation, cleanup, or millisecond stopwatch.", output)
        self.assertNotIn(b"PASS:", output)
        output = self.execute((0,) * 9)
        self.assertEqual(output.count(b"<1 ms"), 9)
        self.assertIn(b"PASS:", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
