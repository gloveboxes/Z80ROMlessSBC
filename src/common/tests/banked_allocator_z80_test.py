"""Check the private BANKMEM allocation scanner with real Z80 instructions."""
from pathlib import Path
import subprocess
import tempfile
import unittest

import z80


class AllocationScanner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[3]
        source = (root / "examples/MEMPERF/BANKMEM.C").read_text()
        assembly = source.split("#asm", 1)[1].split("#endasm", 1)[0]
        assembly = "org 02000h\n" + "\n".join(
            line for line in assembly.splitlines()
            if line.strip() != "public _bmcap")
        with tempfile.TemporaryDirectory(prefix="banked-capacity-") as name:
            directory = Path(name)
            (directory / "scanner.asm").write_text(assembly)
            subprocess.run(["z80asm", "-o", str(directory / "scanner.bin"),
                            str(directory / "scanner.asm")], check=True)
            cls.code = (directory / "scanner.bin").read_bytes()

    def setUp(self):
        self.machine = z80.Z80Machine()
        self.machine.memory[0x2000:0x2000 + len(self.code)] = self.code
        self.machine.memory[0x1000:0x1003] = bytes((0xCD, 0, 0x20))
        self.machine.set_breakpoint(0x1003)

    def capacity(self, mapping, handle):
        m = self.machine
        m.memory[0x3000:0x3040] = mapping
        m.memory[0xE000:0xE004] = bytes((0, 0x30, handle & 255, handle >> 8))
        m.pc, m.sp, m.ix, m.iy = 0x1000, 0xE000, 0x4567, 0x89AB
        for _ in range(5):
            m.ticks_to_stop = 10000
            if m.run() & m._BREAKPOINT_HIT:
                break
        else:
            self.fail("allocation scanner exceeded execution budget")
        self.assertEqual((m.sp, m.ix, m.iy), (0xE000, 0x4567, 0x89AB))
        self.assertEqual(bytes(m.memory[0x3000:0x3040]), bytes(mapping))
        return m.hl

    @staticmethod
    def put(mapping, block, value):
        shift = (block & 3) * 2
        mapping[block >> 2] = (mapping[block >> 2] & ~(3 << shift)) | (value << shift)

    def test_every_start_length_and_terminating_tag(self):
        cases = 0
        for first in range(1, 256):
            for count in range(1, 257 - first):
                mapping = bytearray(64)
                self.put(mapping, first, 1)
                for block in range(first + 1, first + count):
                    self.put(mapping, block, 2)
                for tail in (0, 1, 3):
                    if first + count < 256:
                        self.put(mapping, first + count, tail)
                    self.assertEqual(self.capacity(mapping, first * 256), count * 256,
                                     (first, count, tail))
                    cases += 1
        print(f"PASS: {cases} Z80 allocation start/length/end-tag combinations")

    def test_invalid_and_freed_handles(self):
        mapping = bytearray(64)
        self.assertEqual(self.capacity(mapping, 0), 0)
        for block in range(1, 256):
            for value in (0, 2, 3):
                self.put(mapping, block, value)
                self.assertEqual(self.capacity(mapping, block * 256), 0)
            self.put(mapping, block, 1)
            for low in (1, 15, 255):
                self.assertEqual(self.capacity(mapping, block * 256 + low), 0)
            self.put(mapping, block, 0)
            self.assertEqual(self.capacity(mapping, block * 256), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
