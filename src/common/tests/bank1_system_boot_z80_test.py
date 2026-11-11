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
        bank1 = machine.bank(1)
        bank1[0:3] = b"\xC3\x40\xFB"
        bank1[5:8] = b"\xC3\x06\xEE"
        bank1[0xDE00:0xE600] = self.service
        bank1[0xE600:0xEE00] = self.resident
        bank1[0xEE00:0xFB40] = self.bdos
        bank1[0xFB40:0x10000] = self.image[0xFB40:0x10000]
        bank1[0xFC03:0xFC06] = b"\xC3\x40\xFB"
        machine.set_output_callback(
            lambda port, value: None if port & 255 == 7 else case.write_port(port, value))
        machine.set_input_callback(lambda port: 0xB4 | machine.active_bank if port & 255 == 7
                                   else case.read_port(port))

    @property
    def banks(self):
        return [self.case.machine.bank(0), self.case.machine.bank(1)]

    @property
    def active(self) -> int:
        return self.case.machine.active_bank

    FCB = 0x005C

    def run_code(self, code: bytes, **registers: int) -> int:
        """Run bank-0 code as an application would; returns its T-states."""
        machine = self.case.machine
        for _ in range(100):
            # The CCP proxies a BDOS call right after its prompt; let it finish.
            if not self.active:
                break
            machine.ticks_to_stop = 1000
            machine.run()
        machine.memory[0x0E00:0x0E00 + len(code)] = code
        machine.set_breakpoint(0x0E00 + len(code))
        machine.pc, machine.sp = 0x0E00, 0x0DF0
        for name, value in registers.items():
            setattr(machine, name, value)
        machine.ticks_to_stop = 20_000_000
        events = machine.run()
        machine.clear_breakpoint(0x0E00 + len(code))
        self.assertTrue(events & machine._BREAKPOINT_HIT, f"pc={machine.pc:04x}")
        self.assertEqual((self.active, machine.sp), (0, 0x0DF0))
        return 20_000_000 - machine.ticks_to_stop

    def bdos_call(self, function: int, argument: int = 0) -> int:
        self.ticks = self.run_code(b"\xCD\x05\x00", c=function, de=argument)
        self.assertEqual(self.case.machine.a, self.case.machine.l)
        return self.case.machine.a

    def name_file(self, name: bytes) -> None:
        self.case.machine.memory[self.FCB:self.FCB + 36] = b"\0" + name + bytes(24)

    def count_bank_selects(self) -> list[int]:
        """Banks chosen by each OUT to port 7; only D0 selects the bank."""
        selects: list[int] = []
        case = self.case
        case.machine.set_output_callback(
            lambda port, value: selects.append(value & 1) if port & 255 == 7
            else case.write_port(port, value))
        return selects

    def test_record_reads_land_directly_in_the_callers_dma(self) -> None:
        case, (base, extra) = self.case, self.banks
        case.until_prompt()
        selects = self.count_bank_selects()
        self.name_file(b"AUTORUN COM")
        self.assertLess(self.bdos_call(15, self.FCB), 4)
        # The same addresses in bank 1 and the search staging stay untouched;
        # 0080h is below the 0100h limit of the copying gateway.
        for dma, record in ((0x4000, 0), (0x0080, 1), (0xDD80, 2)):
            with self.subTest(dma=f"{dma:04x}"):
                extra[dma:dma + 128] = b"\xA5" * 128
                extra[0xE400:0xE480] = b"\x5A" * 128
                self.bdos_call(26, dma)
                del selects[:]
                self.assertEqual(self.bdos_call(20, self.FCB), 0)
                self.assertEqual(bytes(base[dma:dma + 128]),
                                 case.autorun[record * 128:record * 128 + 128])
                self.assertEqual(bytes(extra[dma:dma + 128]), b"\xA5" * 128)
                self.assertEqual(bytes(extra[0xE400:0xE480]), b"\x5A" * 128)
                self.assertEqual((base[0xFC37], extra[0xFC37]), (0, 0))
                # FCB out and back (3 bursts each), the gateway and the record.
                self.assertEqual(selects, [1, 0] * 8)
        self.assertEqual(case.machine.memory[self.FCB + 32], 3)
        # Random read of the first record through the same path.
        base[self.FCB + 33:self.FCB + 36] = bytes(3)
        self.bdos_call(26, 0x4000)
        self.assertEqual(self.bdos_call(33, self.FCB), 0)
        self.assertEqual(bytes(base[0x4000:0x4080]), case.autorun[:128])

    def test_record_writes_take_the_callers_dma_across_extents(self) -> None:
        case, (base, extra) = self.case, self.banks
        case.until_prompt()
        records = [bytes((index * 7 + offset) & 255 for offset in range(128))
                   for index in range(140)]
        extra[0x4000:0x4080] = b"\xEE" * 128
        base[0xFB80:0xFC00] = b"\x77" * 128       # bank 0's own directory buffer
        self.name_file(b"RECORDS DAT")
        self.bdos_call(19, self.FCB)
        self.assertLess(self.bdos_call(22, self.FCB), 4)
        self.bdos_call(26, 0x4000)
        for record in records:
            base[0x4000:0x4080] = record
            self.assertEqual(self.bdos_call(21, self.FCB), 0)
        # Zero-fill random write: the BDOS clears the new block from its own
        # directory buffer in bank 1, then writes the caller's record.
        tail = bytes(range(128, 256))
        base[0x4000:0x4080] = tail
        base[self.FCB + 33:self.FCB + 36] = (300).to_bytes(2, "little") + b"\0"
        self.assertEqual(self.bdos_call(40, self.FCB), 0)
        self.assertLess(self.bdos_call(16, self.FCB), 4)
        self.assertEqual(bytes(extra[0x4000:0x4080]), b"\xEE" * 128)
        self.assertEqual((base[0xFC37], extra[0xFC37]), (0, 0))
        written = [data for _, _, _, data in case.writes]
        for record in records + [tail]:
            self.assertIn(record, written)
        self.assertNotIn(b"\xEE" * 128, written)
        self.assertNotIn(b"\x77" * 128, written)

        self.name_file(b"RECORDS DAT")
        self.assertLess(self.bdos_call(15, self.FCB), 4)
        self.bdos_call(26, 0x0080)
        for record in records:
            self.assertEqual(self.bdos_call(20, self.FCB), 0)
            self.assertEqual(bytes(base[0x80:0x100]), record)
        self.assertEqual(self.bdos_call(20, self.FCB), 1)
        self.assertEqual(bytes(base[0x80:0x100]), records[-1])  # untouched at EOF
        for number, expected in ((300, tail), (299, bytes(128)), (128, records[128])):
            base[self.FCB + 33:self.FCB + 36] = number.to_bytes(2, "little") + b"\0"
            self.assertEqual(self.bdos_call(33, self.FCB), 0)
            self.assertEqual(bytes(base[0x80:0x100]), expected)

    def test_search_results_return_through_bank_one_staging(self) -> None:
        case, (base, extra) = self.case, self.banks
        case.until_prompt()
        extra[0x4000:0x4080] = b"\xA5" * 128
        # A BDOS error exit can leave the record flag set; every proxy call
        # states it again and the next application start clears it.
        extra[0xFC37] = 1
        self.name_file(b"AUTORUN COM")
        self.bdos_call(26, 0x4000)
        index = self.bdos_call(17, self.FCB)
        self.assertLess(index, 4)
        self.assertEqual(bytes(base[0x4000 + index * 32:0x400C + index * 32]),
                         b"\0AUTORUN COM")
        self.assertEqual(bytes(extra[0x4000:0x4080]), b"\xA5" * 128)
        self.assertEqual(self.bdos_call(18), 0xFF)
        self.assertEqual(extra[0xFC37], 0)
        extra[0xFC37] = 1
        self.run_code(b"\x1E\x0A\xCD\x10\xFF")       # APPLICATION START
        self.assertEqual((case.machine.hl, extra[0xFC37]), (1, 0))

    def test_bdos_call_cycle_budget(self) -> None:
        self.case.until_prompt()
        self.name_file(b"AUTORUN COM")
        self.bdos_call(15, self.FCB)
        self.bdos_call(26, 0x4000)
        self.bdos_call(20, self.FCB)
        measured = {}
        self.bdos_call(20, self.FCB)
        measured["sequential read"] = self.ticks
        self.bdos_call(25)
        measured["current disk"] = self.ticks
        self.name_file(b"BUDGET  DAT")
        self.bdos_call(19, self.FCB)
        self.bdos_call(22, self.FCB)
        self.bdos_call(21, self.FCB)
        self.bdos_call(21, self.FCB)
        measured["sequential write"] = self.ticks
        for name, limit in (("sequential read", 12500), ("sequential write", 13100),
                            ("current disk", 2400)):
            with self.subTest(call=name):
                self.assertLessEqual(measured[name], limit, measured)

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
        case.machine.select_bank(1)
        case.machine.pc = 0xFC03
        self.assertIn(b"A>", case.until_prompt(len(case.output)))
        self.assertEqual(self.active, 0)

    def test_application_launch_recovers_pool_and_exhausted_runtime(self) -> None:
        case = self.case
        # Deliberately abandon a 4 KiB allocation on both CP/M exit paths.
        allocate = bytes((0x21, 0x00, 0x10, 0x1E, 0x00,
                          0xCD, 0x10, 0xFF, 0x22, 0x88, 0x00))
        for name, exit_code in (("LEAKRET.COM", b"\xC9"),
                                ("LEAKBOOT.COM", b"\xC3\x00\x00")):
            case.disks[0][:] = cpm_boot_z80_test.install_program(
                bytes(case.disks[0]), name, allocate + exit_code)
        case.until_prompt()
        for _ in range(100):
            if not self.active:
                break
            case.machine.ticks_to_stop = 1000
            case.machine.run()
        self.assertEqual(self.active, 0)
        bank = self.banks[1]
        bank[0xDFA0:0xDFA2] = b"\xFF\xFF"
        bank[0xDFA6:0xDFA8] = b"\xFF\xFF"
        bank[0xDE9F] = 1
        bank[0xE4D2] = 8
        for command in ("LEAKRET", "LEAKRET", "LEAKBOOT", "LEAKRET"):
            with self.subTest(command=command):
                self.assertIn(b"A>", case.command(command))
                for _ in range(100):
                    if not self.active:
                        break
                    case.machine.ticks_to_stop = 1000
                    case.machine.run()
                self.assertEqual(self.active, 0)
                self.assertEqual(case.machine.memory[0x88:0x8A], b"\x00\xC8")
                self.assertEqual(bank[0xE614:0xE616], b"\x00\x06")
                self.assertEqual(bank[0xDFA0:0xDFA2], b"\x00\x00")
                self.assertEqual(bank[0xDFA6:0xDFA8], b"\x01\x00")
                self.assertEqual(bank[0xDE9F], 0)
                self.assertEqual(bank[0xE4D2], 0)
                self.assertEqual(bank[0xE701:0xE703], b"\xBF\x08")
                self.assertEqual(bank[0xE800:0xEC00], self.resident[0x200:0x600])

    def test_dcc_library_loaded_and_called_from_bank_zero(self) -> None:
        self.check_dcc_library(0)

    def test_dcc_library_loaded_from_drive_b(self) -> None:
        self.check_dcc_library(1)

    def test_dcc_library_reports_configured_stack_override(self) -> None:
        self.check_dcc_library(1, 1024)

    def test_typed_library_call_cycle_budget(self) -> None:
        self.install_dcc_library(0)
        example = Path(__file__).resolve().parents[3] / "examples/LIBDEMO"
        symbols = {name: int(address, 16) for address, name in re.findall(
            r"\b([0-9A-Fa-f]{4})\s+(_\w+)\b",
            (example / "host/build/DATATEST.SYM").read_text())}
        case, machine = self.case, self.case.machine
        case.until_prompt()
        selects = self.count_bank_selects()
        start = len(case.output)
        case.input.extend(b"DATATEST\r")
        entry = symbols["_ADD16"]
        machine.set_breakpoint(entry)
        measured = []
        # check_values calls add16(120, 23), which binds the export's
        # signature, then add16(65535, 1) through the cached binding.
        for expected in (143, 0):
            for _ in range(20000):
                machine.ticks_to_stop = 100000
                if (machine.run() & machine._BREAKPOINT_HIT
                        and machine.pc == entry and not self.active):
                    break
            else:
                self.fail(f"add16 was not called: {bytes(case.output[start:])!r}")
            caller = int.from_bytes(machine.memory[machine.sp:machine.sp + 2], "little")
            machine.set_breakpoint(caller)
            del selects[:]
            machine.ticks_to_stop = 1_000_000
            machine.run()
            machine.clear_breakpoint(caller)
            self.assertEqual((machine.pc, machine.hl, self.active), (caller, expected, 0))
            measured.append((1_000_000 - machine.ticks_to_stop, list(selects)))
        machine.clear_breakpoint(entry)
        ticks, switches = measured[1]
        # Frame out (one burst), the 32-byte CALL record out, one LIB CONTROL
        # request whose status returns in HL, and the frame back: no other
        # entry to bank 1.
        self.assertEqual(switches, [1, 0] * 5, measured)
        self.assertLessEqual(ticks, 20500, measured)
        self.assertIn(b"PASS: all 8 bank-1 library checks.", case.until_prompt(start))

    def check_dcc_library(self, drive: int, stack: int = 512) -> None:
        heap_size, stack_bytes = self.install_dcc_library(drive, stack)
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
            b"reload isolation, unload cleanup and pool-reset rejection.",
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

    def install_dcc_library(self, drive: int, stack: int = 512) -> tuple[int, int]:
        """Build LIBDEMO and place its library and client on a drive."""
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
        return heap_size, stack_bytes


if __name__ == "__main__":
    unittest.main()
