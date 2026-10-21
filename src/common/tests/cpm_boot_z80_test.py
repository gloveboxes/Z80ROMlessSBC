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
        cls.autorun = build_images.build_autorun(assembler)
        cpmcp = shutil.which("cpmcp")
        if cpmcp is None:
            raise RuntimeError("cpmtools is required for AUTORUN execution tests")
        cls.disks[0] = build_images.install_autorun(
            cls.disks[0], cls.autorun, cpmcp
        )
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
        self.disks = [bytearray(disk) for disk in type(self).disks]
        self.input: deque[int] = deque()
        self.output = bytearray()
        self.markers: list[int] = []
        self.drive = 0
        self.lba = 0
        self.record: deque[int] = deque()
        self.flushes = 0
        self.reads = 0
        self.writes: list[tuple[int, int, int, bytes]] = []
        self.write_data: bytearray | None = None
        self.write_type = 0
        self.disk_error = False
        self.fail_config_reads = 0
        self.fail_config_writes = 0
        self.corrupt_config_readback = False
        self.bypass_on_config_read = False
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
            if self.disk_error:
                return 0x81
            if self.write_data is not None:
                return 5
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
        elif port == 0x14:
            self.assertIsNotNone(self.write_data)
            self.write_data.append(value)
            if len(self.write_data) == 128:
                offset = self.lba * 128
                self.disks[self.drive][offset:offset + 128] = self.write_data
                self.writes.append(
                    (self.drive, self.lba, self.write_type, bytes(self.write_data))
                )
                self.write_data = None
        elif port == 0x10:
            self.disk_error = False
            is_config = self.drive == 0 and self.lba == build_images.AUTORUN_RECORD
            if value == 1:
                self.assertFalse(self.record)
                self.assertLess(self.lba, 2560)
                if is_config and self.fail_config_reads:
                    self.fail_config_reads -= 1
                    self.disk_error = True
                    return
                if is_config and self.bypass_on_config_read:
                    self.input.append(3)
                    self.bypass_on_config_read = False
                offset = self.lba * 128
                self.record.extend(self.disks[self.drive][offset:offset + 128])
                if is_config and self.writes and self.corrupt_config_readback:
                    self.record[10] ^= 1
                self.reads += 1
            elif value in (2, 3, 4):
                if is_config and self.fail_config_writes:
                    self.fail_config_writes -= 1
                    self.disk_error = True
                    return
                self.write_type = value - 2
                self.write_data = bytearray()
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

    def configure(self, mode: int, command: str) -> None:
        offset = build_images.AUTORUN_OFFSET
        self.disks[0][offset:offset + 128] = build_images.autorun_record(mode, command)

    def warm_boot(self) -> bytes:
        self.machine.pc = 0
        return self.until_prompt(len(self.output))

    def test_autorun_boot_modes(self) -> None:
        for mode in range(4):
            with self.subTest(mode=mode):
                self.setUp()
                self.configure(mode, "DIR B:")
                cold = self.until_prompt()
                self.assertEqual(b"CC2" in cold, bool(mode & 1))
                warm = self.warm_boot()
                self.assertEqual(b"CC2" in warm, bool(mode & 2))
                warm_again = self.warm_boot()
                self.assertEqual(b"CC2" in warm_again, bool(mode & 2))
                self.assertEqual(self.writes, [])

    def test_cold_only_transient_does_not_repeat(self) -> None:
        self.configure(1, "LS")
        output = self.until_prompt()
        self.assertNotIn(b"LS?", output)
        self.assertEqual(self.markers.count(0xB0), 1)
        self.assertEqual(self.markers.count(0xC0), 2)

    def test_autorun_key_bypass(self) -> None:
        self.configure(3, "DIR B:")
        self.input.append(3)
        self.assertNotIn(b"CC2", self.until_prompt())
        self.input.append(3)
        self.assertNotIn(b"CC2", self.warm_boot())
        self.assertIn(b"CC2", self.warm_boot())

    def test_disabled_autorun_does_not_consume_console_input(self) -> None:
        self.input.extend(b"DIR B:\r")
        self.assertIn(b"CC2", self.until_prompt())

    def test_repeating_transient_can_be_bypassed(self) -> None:
        self.configure(3, "LS")
        for _ in range(1000):
            self.machine.ticks_to_stop = 10000
            self.machine.run()
            if self.markers.count(0xC0) >= 3:
                break
        else:
            self.fail("warm-boot auto-run did not repeat")
        self.bypass_on_config_read = True
        self.assertIn(b"A>", self.until_prompt())

    def test_bad_autorun_record_reports_error_and_boots(self) -> None:
        for field, value in ((0, 0), (4, 4), (5, 81), (5, 0), (127, 0)):
            with self.subTest(field=field):
                self.setUp()
                self.configure(1, "DIR B:")
                offset = build_images.AUTORUN_OFFSET
                self.disks[0][offset + field] = value
                if field != 127:
                    self.disks[0][offset + 127] = 0
                    self.disks[0][offset + 127] = (
                        -sum(self.disks[0][offset:offset + 128]) & 255
                    )
                output = self.until_prompt()
                self.assertIn(b"AUTORUN config error", output)
                self.assertNotIn(b"CC2", output)
                self.assertEqual(self.writes, [])

    def test_autorun_config_read_failure_is_reported(self) -> None:
        self.fail_config_reads = 1
        self.assertIn(b"AUTORUN config error", self.until_prompt())

    def test_autorun_utility_updates_only_configuration(self) -> None:
        self.until_prompt()
        before = [bytes(disk) for disk in self.disks]
        result = self.command("AUTORUN\r1\rc:date")
        self.assertIn(b"Configuration saved and verified.", result)
        self.assertEqual(len(self.writes), 1)
        self.assertEqual(self.writes[0], (
            0, build_images.AUTORUN_RECORD, 1, build_images.autorun_record(1, "C:DATE")
        ))
        offset = build_images.AUTORUN_OFFSET
        expected = bytearray(before[0])
        expected[offset:offset + 128] = build_images.autorun_record(1, "C:DATE")
        self.assertEqual(self.disks, [expected, *map(bytearray, before[1:])])
        self.assertNotIn(b"AUTORUN?", result)
        self.assertNotIn(b"DATE?", result)

    def test_autorun_utility_quit_disable_and_keep_command(self) -> None:
        self.configure(1, "DIR B:")
        self.until_prompt()
        self.assertIn(b"No changes saved.", self.command("AUTORUN\rQ"))
        self.assertEqual(self.writes, [])
        result = self.command("AUTORUN\r2\r")
        self.assertIn(b"Configuration saved and verified.", result)
        self.assertIn(b"CC2", self.warm_boot())
        result = self.command("AUTORUN\r4")
        self.assertIn(b"Configuration saved and verified.", result)
        self.assertEqual(self.writes[-1][3], build_images.autorun_record(0, "DIR B:"))
        self.assertNotIn(b"CC2", result)

    def test_autorun_utility_accepts_exactly_80_characters(self) -> None:
        self.until_prompt()
        command = "REM " + "X" * 76
        result = self.command("AUTORUN\r1\r" + command)
        self.assertIn(b"Configuration saved and verified.", result)
        self.assertEqual(self.writes[-1][3], build_images.autorun_record(1, command))

    def test_autorun_utility_rejects_empty_enabled_command(self) -> None:
        self.until_prompt()
        result = self.command("AUTORUN\r1\r")
        self.assertIn(b"No changes saved.", result)
        self.assertEqual(self.writes, [])

    def test_autorun_utility_restores_default_drive(self) -> None:
        self.until_prompt()
        self.command("B:")
        result = self.command("A:AUTORUN\r1\rDIR C:")
        self.assertIn(b"Configuration saved and verified.", result)
        self.assertTrue(result.endswith(b"B>"))
        self.assertEqual(self.writes[-1][:3], (0, build_images.AUTORUN_RECORD, 1))
        self.assertIn(b"CC2", self.command("DIR"))

    def test_autorun_utility_repairs_invalid_record(self) -> None:
        self.until_prompt()
        offset = build_images.AUTORUN_OFFSET
        self.disks[0][offset:offset + 128] = bytes(128)
        result = self.command("AUTORUN\r4")
        self.assertIn(b"Invalid configuration", result)
        self.assertIn(b"Configuration saved and verified.", result)
        self.assertEqual(self.writes[-1][3], build_images.autorun_record())

    def test_autorun_utility_refuses_other_bios(self) -> None:
        self.until_prompt()
        self.machine.memory[0xFD33] = 0
        result = self.command("AUTORUN")
        self.assertIn(b"requires the native ZAR1 BIOS", result)
        self.assertEqual(self.writes, [])

    def test_autorun_utility_reports_read_write_and_verify_failures(self) -> None:
        for failure in ("read", "write", "verify"):
            with self.subTest(failure=failure):
                self.setUp()
                self.until_prompt()
                if failure == "read":
                    self.fail_config_reads = 1
                    result = self.command("AUTORUN")
                    self.assertIn(b"Unable to read", result)
                else:
                    self.fail_config_writes = int(failure == "write")
                    self.corrupt_config_readback = failure == "verify"
                    result = self.command("AUTORUN\r1\rDIR")
                    self.assertIn(b"write or verification failed", result)
                self.assertNotIn(b"Configuration saved and verified.", result)

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
