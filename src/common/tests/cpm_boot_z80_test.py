"""Execute the packaged CP/M and real BIOS with an independent Z80 core."""

from __future__ import annotations

from collections import deque
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import z80

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "cpm"))
import build_images


class CpmBootExecution(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assembler = shutil.which("z80asm")
        if assembler is None:
            raise RuntimeError("z80asm is required for BIOS execution tests")
        system = build_images.build_cpm_system(assembler)
        bios = build_images.build_bios(assembler)
        cls.image = build_images.build_z80_image(system, bios)
        directory = build_images.REPO_ROOT / "src" / "disks" / "generated"
        cls.disks = [
            (directory / name).read_bytes()
            for name in build_images.SOURCE_DISK_NAMES
        ]
        cls.disks[0] = build_images.build_native_drive(cls.disks[0], system, bios)
        with tempfile.TemporaryDirectory(prefix="z80-disk-diagnostic-") as path:
            binary = Path(path) / "diagnostic.bin"
            subprocess.run(
                [assembler, "-o", str(binary),
                 str(build_images.REPO_ROOT / "src" / "stage09_flash_storage"
                     / "disk_diagnostic.asm")],
                check=True,
                timeout=15,
            )
            cls.diagnostic = binary.read_bytes()

    def setUp(self) -> None:
        self.machine = z80.Z80Machine()
        self.machine.memory[:] = self.image
        self.input: deque[int] = deque()
        self.output = bytearray()
        self.markers: list[int] = []
        self.drive = 0
        self.lba = 0
        self.record: deque[int] = deque()
        self.flushes = 0
        self.reads = 0
        self.input_ports: set[int] = set()
        self.diagnostic_bytes = bytearray()
        self.diagnostic_active = False
        self.machine.set_input_callback(self.read_port)
        self.machine.set_output_callback(self.write_port)

    def read_port(self, address: int) -> int:
        port = address & 0x17
        self.input_ports.add(address)
        if port == 0:
            return self.input.popleft() if self.input else 0
        if port == 1:
            return 0x82 | bool(self.input)
        if port == 0x10:
            return 3 if self.record else 1
        if port == 0x14:
            self.assertTrue(self.record, "BIOS read past the 128-byte record")
            return self.record.popleft()
        self.fail(f"Unexpected IN {address:04x}")

    def write_port(self, address: int, value: int) -> None:
        port = address & 0x17
        if port == 0:
            self.output.append(value)
        elif port == 5:
            self.markers.append(value)
        elif port == 6:
            self.assertTrue(self.diagnostic_active)
            self.diagnostic_bytes.append(value)
        elif port == 0x11:
            self.assertLess(value, 4)
            self.drive = value
        elif port == 0x12:
            self.lba = (self.lba & 0xFF00) | value
        elif port == 0x13:
            self.lba = (self.lba & 0xFF) | (value << 8)
        elif port == 0x10:
            if value == 1:
                self.assertFalse(self.record)
                self.assertLess(self.lba, 2560)
                offset = self.lba * 128
                self.record.extend(self.disks[self.drive][offset:offset + 128])
                self.reads += 1
            elif value == 5:
                self.flushes += 1
            else:
                self.fail(f"Unexpected disk command {value}")
        else:
            self.fail(f"Unexpected OUT {address:04x}, {value:02x}")

    def until_prompt(self, start: int = 0) -> bytes:
        for _ in range(1000):
            self.machine.ticks_to_stop = 100000
            self.machine.run()
            self.assertFalse(self.machine.halted)
            if self.output[start:].endswith(b">"):
                self.assertFalse(self.input)
                return bytes(self.output[start:])
        self.fail(f"No prompt within execution budget: {bytes(self.output)!r}")

    def command(self, text: str) -> bytes:
        start = len(self.output)
        self.input.extend(text.encode("ascii") + b"\r")
        return self.until_prompt(start)

    def test_cold_boot_and_directory_reads(self) -> None:
        output = self.until_prompt()
        self.assertEqual(self.markers, [0xB0, 0xC0])
        self.assertIn(b"A>", output)
        for command, sentinel in (
            ("DIR", b"DUMP"),
            ("DIR B:", b"CC2"),
            ("DIR C:", b"ATTNC11"),
        ):
            with self.subTest(command=command):
                self.assertIn(sentinel, self.command(command))
        self.assertIn(b"DOCTOR", self.command("DIR D:"))
        self.assertGreater(self.reads, 0)
        self.assertTrue(any((port & 0xFF) == 0x14 and port >> 8
                            for port in self.input_ports))

    def test_transient_exit_and_warm_boot(self) -> None:
        self.until_prompt()
        result = self.command("LS")
        self.assertNotIn(b"LS?", result)
        self.assertGreater(self.flushes, 0)
        self.assertGreaterEqual(self.markers.count(0xC0), 2)
        self.assertEqual(self.machine.memory[5], 0xC3)
        self.assertEqual(bytes(self.machine.memory[6:8]), b"\x06\xEF")

    def test_read_only_all_record_diagnostic(self) -> None:
        self.assertLessEqual(len(self.diagnostic), 256)
        self.machine.memory[:len(self.diagnostic)] = self.diagnostic
        self.diagnostic_active = True
        for _ in range(2000):
            self.machine.ticks_to_stop = 100000
            self.machine.run()
            if self.machine.halted:
                break
        self.assertTrue(self.machine.halted, "Disk diagnostic exceeded budget")
        self.assertEqual(self.markers, [0x5A])
        self.assertEqual(self.reads, 4 * 2560)
        self.assertEqual(self.flushes, 0)
        self.assertEqual(bytes(self.diagnostic_bytes), b"".join(self.disks))


if __name__ == "__main__":
    unittest.main(verbosity=2)
