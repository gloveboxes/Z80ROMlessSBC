"""Boot the production two-bank system image and its matching warm-boot tracks."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

import cpm_boot_z80_test


def generated_array(name: str) -> bytes:
    header = (Path(__file__).resolve().parents[1] /
              "include/z80sbc/bank1_system.h").read_text()
    payload = header.split(f"{name}[] = {{", 1)[1].split("};", 1)[0]
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", payload))


class BankOneSystemBoot(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cpm_boot_z80_test.CpmBootExecution.setUpClass()
        build = cpm_boot_z80_test.build_images
        cls.system = build.build_banked_cpm_system("z80asm")
        cls.bios = build.build_bios("z80asm")
        cls.image = build.build_z80_image(cls.system, cls.bios)
        cls.disk = build.build_native_drive(
            cpm_boot_z80_test.CpmBootExecution.disks[0], cls.system, cls.bios)
        cls.disk = build.install_autorun(cls.disk,
                                          cpm_boot_z80_test.CpmBootExecution.autorun,
                                          "cpmcp")
        cls.service = generated_array("bank1_service_code")
        cls.resident = generated_array("bank1_resident_code")
        cls.bdos = generated_array("bank1_bdos_code")

    def setUp(self) -> None:
        case = cpm_boot_z80_test.CpmBootExecution()
        case.image = self.image
        case.setUp()
        case.disks[0][:] = self.disk
        self.case = case
        machine = case.machine
        bank0 = bytearray(machine.memory)
        bank1 = bytearray(65536)
        bank1[0:3] = b"\xC3\x40\xFB"
        bank1[5:8] = b"\xC3\x06\xEE"
        bank1[0xDE00:0xE600] = self.service
        bank1[0xE600:0xEE00] = self.resident
        bank1[0xEE00:0xFB40] = self.bdos
        bank1[0xFB40:0x10000] = self.image[0xFB40:0x10000]
        bank1[0xFC03:0xFC06] = b"\xC3\x40\xFB"
        self.banks = [bank0, bank1]
        self.active = 0

        def output(port: int, value: int) -> None:
            if port & 255 == 7:
                self.banks[self.active][:] = machine.memory
                self.active = value & 1
                machine.memory[:] = self.banks[self.active]
            else:
                case.write_port(port, value)

        machine.set_output_callback(output)
        machine.set_input_callback(lambda port: 0xB4 | self.active if port & 255 == 7
                                   else case.read_port(port))

    def test_directory_file_read_and_warm_boot(self) -> None:
        case = self.case
        self.assertIn(b"A>", case.until_prompt())
        self.assertIn(b"DUMP", case.command("DIR"))
        self.assertIn(b"AUTORUN", case.command("DIR AUTORUN.*"))
        self.assertIn(b"A>", case.warm_boot())
        self.assertIn(b"DUMP", case.command("DIR"))
        self.assertEqual(self.active, 0)

    def test_directory_does_not_depend_on_uninitialized_bank_one_heap(self) -> None:
        # Hardware initializes page zero and DE00h..FFFFh, leaving the
        # application pool and bank-1 service stack with power-on contents.
        self.banks[1][0x0100:0xDE00] = b"\xA5" * (0xDE00 - 0x0100)
        self.case.until_prompt()
        ccp_code = bytes(self.case.machine.memory[0xED19:0xED32])
        self.assertIn(b"DUMP", self.case.command("DIR"))
        self.assertEqual(self.case.machine.memory[0xED19:0xED32], ccp_code)
        self.assertEqual(self.case.markers, [0xB0, 0xC0])

    def test_bank_zero_disk_proxy_selects_and_scans_drive_b(self) -> None:
        case = self.case
        case.until_prompt()
        self.assertIn(b"B>", case.command("B:"))
        self.assertIn(b"CC2", case.command("DIR"))
        self.assertIn(b"A>", case.command("A:"))
        self.assertIn(b"DUMP", case.command("DIR"))
        self.assertEqual(case.markers, [0xB0, 0xC0])

    def test_warm_boot_entered_from_bank_one_returns_to_bank_zero(self) -> None:
        case = self.case
        case.until_prompt()
        self.banks[0][:] = case.machine.memory
        self.active = 1
        case.machine.memory[:] = self.banks[1]
        case.machine.pc = 0xFC03
        self.assertIn(b"A>", case.until_prompt(len(case.output)))
        self.assertEqual(self.active, 0)

    def test_dcc_library_loaded_and_called_from_bank_zero(self) -> None:
        self.check_dcc_library(0)

    def test_dcc_library_loaded_from_drive_b(self) -> None:
        self.check_dcc_library(1)

    def test_dcc_library_reports_configured_stack_override(self) -> None:
        self.check_dcc_library(1, 1024)

    def check_dcc_library(self, drive: int, stack: int = 512) -> None:
        root = Path(__file__).resolve().parents[3]
        example = root / "examples/LIBDEMO"
        dcc = Path(os.environ.get("DCC_DIR", str(root.parent / "dcc")))
        if not (dcc / "dcc").exists() or not (dcc / "dccmake").exists():
            self.skipTest("sibling DCC toolchain is unavailable")
        env = {**os.environ, "DCC_DIR": str(dcc)}
        subprocess.run([str(dcc / "dccmake"), f"dcc-stack-bytes={stack}"],
                       cwd=example,
                       check=True, env=env, capture_output=True)
        library = (example / "lib/build/DATA.LIB").read_bytes()
        heap_start = int.from_bytes(library[24:26], "little")
        stack_bytes = int.from_bytes(library[16:18], "little")
        self.assertEqual(stack_bytes, stack)
        heap_size = 0xC800 - stack_bytes - heap_start
        self.assertEqual(library[4:8], b"\x01\x01\x00\x01")
        self.assertEqual(int.from_bytes(library[12:14], "little"), 8)
        self.assertGreater(heap_size, 40000)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "diskdefs").write_text(
                cpm_boot_z80_test.build_images.cpm_diskdefs())
            disk = folder / "drive.img"
            disk.write_bytes(self.case.disks[drive])
            for name in ("DATA.LIB", "DATATEST.COM"):
                artifact = (example / "lib/build" / name if name.endswith(".LIB")
                            else example / "host/build" / name)
                build = cpm_boot_z80_test.build_images
                stem, suffix = name.split(".")
                key = f"{stem:<8}{suffix:<3}".encode("ascii")
                native = self.case.disks[drive]
                if any(
                    native[offset] == 0
                    and bytes(value & 0x7F
                              for value in native[offset + 1:offset + 12]) == key
                    for offset in range(
                        build.SYSTEM_BYTES,
                        build.SYSTEM_BYTES + (build.DPB_DRM + 1) * 32, 32)
                ):
                    subprocess.run(
                        ["cpmrm", "-f", "z80sbc", str(disk), "0:" + name],
                        cwd=folder, check=True,
                        env={**env, "DISKDEFS": str(folder / "diskdefs")},
                        capture_output=True)
                subprocess.run(["cpmcp", "-f", "z80sbc", str(disk),
                                str(artifact), "0:" + name],
                               cwd=folder, check=True,
                               env={**env, "DISKDEFS": str(folder / "diskdefs")},
                               capture_output=True)
            self.case.disks[drive][:] = disk.read_bytes()
        self.case.until_prompt()
        if drive:
            self.assertIn(b"B>", self.case.command("B:"))
        try:
            output = self.case.command("DATATEST")
        except AssertionError as exc:
            self.fail(f"{exc} pc={self.case.machine.pc:04x} sp={self.case.machine.sp:04x} bank={self.active} reads={self.case.reads} req={self.case.machine.memory[0xE4B0:0xE4C0].hex()} stack={self.case.machine.memory[0xE5B0:0xE5C0].hex()}")
        self.assertIn(b"PASS: load and lifecycle.", output,
                      f"loader_status={self.banks[1][0xE4D1]:02x} "
                      f"image_len={self.banks[1][0xDE92:0xDE94].hex()} "
                      f"header={self.banks[1][0xE400:0xE420].hex()}")
        self.assertNotIn(b"FAIL:", output)
        self.assertIn(
            f"Read 128 bytes from A:AUTORUN.COM in bank 1; byte sum "
            f"{sum(self.case.autorun[:128])}.".encode("ascii"), output)
        self.assertIn(
            f"Bank 1 DCC heap: {heap_size} bytes capacity, 0 committed, "
            f"{heap_size} growth; stack: {stack_bytes} bytes".encode("ascii"),
            output)
        self.assertIn(b"add16(120, 23) = 143; caller values unchanged.", output)
        self.assertIn(b'upper("Bank 1") = "BANK 1".', output)
        exports = (
            b"ONLOAD: u16()", b"ONUNLOAD: u16()",
            b"ADD16: u16(u16,u16)", b"UPPER: u16(inout:u8[2<=128],u16)",
            b"FILERD: u16(out:u8[2<=128],u16)",
            b"BLKNEW: u16(in:rec1le:BlockSpec[10]{seed:u16@0;label:u8[8]@2},"
            b"out:rec1le:BlockInfo[17]{handle:u16@0;size:u16@2;byte_sum:u32@4;"
            b"state:i8@8;label:u8[8]@9})",
            b"BLKREAD: u16(u16,u16,out:u8[4<=128],u16)",
            b"BLKFREE: u16(inout:rec1le:BlockInfo[17]{handle:u16@0;size:u16@2;"
            b"byte_sum:u32@4;state:i8@8;label:u8[8]@9})",
        )
        self.assertEqual(
            [line for line in output.splitlines() if line.startswith(b"Export ")],
            [b"Export " + export for export in exports])
        results = (
            b"load and lifecycle.",
            b"scalar values and 16-bit wraparound.",
            b"text copy-back, limits and null-pointer rejection.",
            b"file read, reopen and buffer guards.",
            b"eight exports, signatures and malformed-frame rejection.",
            b"chunk data, bounds, stale handles and double-free rejection.",
            b"directory search preserved and transfer memory recovered.",
            b"reload isolation and unload cleanup.",
        )
        previous = -1
        for step, result in enumerate(results, 1):
            marker = f"[{step}/8] ".encode("ascii")
            position = output.index(marker)
            self.assertGreater(position, previous)
            passed = output.index(b"PASS: " + result, position)
            self.assertGreater(passed, position)
            previous = passed
        self.assertIn(b"PASS: flat record in/out/inout, 32-bit values, arrays and guards.", output)
        self.assertEqual(output.count(b"PASS:"), 10)
        self.assertIn(b"PASS: all 8 bank-1 library checks.", output)
        # The CCP proxies a BDOS call right after printing its prompt; let a
        # call in progress at the slice boundary finish before sampling.
        for _ in range(100):
            if not self.active:
                break
            self.case.machine.ticks_to_stop = 1000
            self.case.machine.run()
        self.assertEqual(self.active, 0)


if __name__ == "__main__":
    unittest.main()
