"""Bank-1 copy of the native BDOS/BIOS can call the real console port."""

import unittest
import os
from pathlib import Path
import subprocess
import tempfile

import cpm_boot_z80_test
import bank1_system_boot_z80_test
from banked_allocator_z80_test import assemble


class BankOneBdos(unittest.TestCase):
    @staticmethod
    def library_record(code: bytes, *, entry: int = 0,
                       brk: int = 0xFFFF, hlimit: int = 0xFFFF,
                       stack: int = 2048, heap_start: int | None = None,
                       signature: int = 0) -> bytes:
        header = bytearray(b"B1LB" + bytes((1, 1, 0, 1, 0, 1))
                           + len(code).to_bytes(2, "little")
                           + bytes((1, 0, 0, 0, 0, 8, 38, 0))
                           + brk.to_bytes(2, "little")
                           + hlimit.to_bytes(2, "little")
                           + (0x100 + len(code) if heap_start is None
                              else heap_start).to_bytes(2, "little")
                           + b"_BEEF\0\0\0" + entry.to_bytes(2, "little")
                           + signature.to_bytes(2, "little"))
        header[16:18] = stack.to_bytes(2, "little")
        return (header + code).ljust(128, b"\0")

    @classmethod
    def setUpClass(cls) -> None:
        cpm_boot_z80_test.CpmBootExecution.setUpClass()
        cls.transfer = assemble("src/common/banked_transfer.asm")
        cls.file_payload = assemble("src/common/tests/bank1_bdos_file.asm")
        cls.proxy = assemble("src/common/bank1_bdos_proxy.asm")
        cls.loader = assemble("src/common/bank1_library_loader.asm")

    def run_bank_one_payload(self, payload: bytes, operation: int = 5,
                             request: bytes = b"", lib_file: bytes | None = None
                             ) -> tuple[cpm_boot_z80_test.CpmBootExecution, bytearray]:
        case = cpm_boot_z80_test.CpmBootExecution()
        case.setUp()
        if lib_file is not None:
            with tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                (folder / "diskdefs").write_text(cpm_boot_z80_test.build_images.cpm_diskdefs())
                disk = folder / "disk.img"
                disk.write_bytes(case.disks[0])
                library = folder / "TEST.LIB"
                library.write_bytes(lib_file)
                subprocess.run(["cpmcp", "-f", "z80sbc", str(disk),
                                str(library), "0:TEST.LIB"], check=True,
                               cwd=folder,
                               env={**os.environ, "DISKDEFS": str(folder / "diskdefs")})
                case.disks[0][:] = disk.read_bytes()
        case.until_prompt()
        machine = case.machine
        base = bytearray(machine.memory)
        extra = bytearray(65536)
        extra[0xEE00:0xFF00] = case.image[0xEE00:0xFF00]
        extra[5:8] = bytes((0xC3, 0x06, 0xEE))
        extra[0xDE00:0xE600] = bank1_system_boot_z80_test.generated_array("bank1_service_code")
        extra[0xE600:0xEE00] = bank1_system_boot_z80_test.generated_array("bank1_resident_code")
        extra[0xDE00:0xDE00 + len(self.proxy)] = self.proxy
        extra[0xE000:0xE000 + len(self.loader)] = self.loader
        if payload:
            extra[0xDE9F] = 1
        if lib_file is not None:
            extra[0x100:0xC800] = b"\xA5" * (0xC800 - 0x100)
        extra[0xE4B0:0xE4B0 + len(request)] = request
        extra[0x100:0x100 + len(payload)] = payload
        for bank in (base, extra):
            bank[0xFF00:0xFF00 + len(self.transfer)] = self.transfer
        banks = [base, extra]
        active = 0

        def output(port: int, value: int) -> None:
            nonlocal active
            if port & 255 == 7:
                banks[active][:] = machine.memory
                active = value & 1
                machine.memory[:] = banks[active]
            else:
                case.write_port(port, value)

        machine.set_output_callback(output)
        machine.set_input_callback(lambda port: 0xB4 | active if port & 255 == 7
                                   else case.read_port(port))
        machine.set_breakpoint(0x1003)

        def gateway(operation: int, hl: int, bc: int = 0) -> int:
            if active == 0:
                banks[0][:] = machine.memory
            banks[0][0x1000:0x1003] = bytes((0xCD, 0x10, 0xFF))
            machine.memory[:] = banks[0]
            machine.pc, machine.sp, machine.hl, machine.e = 0x1000, 0xD000, hl, operation
            machine.bc, machine.ix, machine.iy = bc, 0, 0
            machine.ticks_to_stop = 5_000_000
            for _ in range(100):
                if machine.run() & machine._BREAKPOINT_HIT:
                    break
            else:
                self.fail(f"bank-1 service did not return: PC={machine.pc:04x} bank={active} SP={machine.sp:04x} status={machine.memory[0xE4D1]:02x} reads={case.reads} output={case.output[-100:]!r}")
            self.assertEqual((active, machine.pc, machine.sp), (0, 0x1003, 0xD000))
            return machine.hl

        if lib_file is not None and operation == 8:
            # Public LOAD: stage the FCB in a live pool extent, then submit
            # a library request. HL returns the BIOS status.
            fcb = gateway(0, 48)
            self.assertEqual(fcb, 0xC800)
            banks[1][fcb:fcb + 48] = b"\0TEST    LIB" + bytes(36)
            request = bytearray(32)
            for offset, value in {0: 32, 2: 1, 4: 0, 12: fcb}.items():
                request[offset:offset + 2] = value.to_bytes(2, "little")
            banks[1][0xDF20:0xDF40] = request
            gateway(8, 0xDF20)
        else:
            gateway(operation, 0x0100, 0xC000)
        return case, banks[1]

    def test_bdos_console_output_from_bank_one(self) -> None:
        message = b"BANK 1 BDOS$"
        # LD DE,0120h; LD C,9; CALL 0005h; LD HL,BEEFh; RET
        payload = bytearray(0x20 + len(message))
        payload[:12] = bytes((0x11, 0x20, 0x01, 0x0E, 9,
                              0xCD, 5, 0, 0x21, 0xEF, 0xBE, 0xC9))
        payload[0x20:] = message
        case, _ = self.run_bank_one_payload(payload)
        self.assertEqual(case.machine.hl, 0xBEEF)
        self.assertIn(message[:-1], case.output)

    def test_bdos_reads_a_file_into_bank_one_dma(self) -> None:
        case, bank = self.run_bank_one_payload(self.file_payload)
        self.assertEqual(case.machine.hl, 0)
        self.assertEqual(bank[0x200:0x280], case.autorun[:128])

    def test_gateway_bdos_proxy_returns_registers(self) -> None:
        case, bank = self.run_bank_one_payload(b"", operation=6,
                                               request=bytes((12, 0, 0, 0)))
        self.assertEqual(bank[0xE4B6], 0x22)
        self.assertEqual(case.machine.pc, 0x1003)
        case, bank = self.run_bank_one_payload(b"", operation=6,
                                               request=bytes((32, 0, 0xFF, 0x00)))
        self.assertEqual(case.machine.pc, 0x1003)

    def test_resident_loader_reads_and_verifies_library(self) -> None:
        code = bytes((0x21, 0xEF, 0xBE, 0xC9))
        library = self.library_record(code)
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual(case.machine.hl, 0)
        self.assertEqual(bank[0xE4D1], 0)
        self.assertEqual(bank[0xE4D2], 1)
        self.assertEqual(bank[0xE300:0xE30A], b"_BEEF\0\0\0" + bytes((0, 0)))
        self.assertEqual(bank[0x100:0x104], code)

    def test_resident_loader_ignores_reserved_header_word(self) -> None:
        # Like a .COM, the image is copied verbatim; bytes 14..15 are reserved.
        code = bytes((0x21, 0xEF, 0xBE, 0xC9))
        library = bytearray(self.library_record(code))
        library[14:16] = b"\x34\x12"
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual((case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (0, 0, 1))
        self.assertEqual(bank[0x100:0x104], code)

    def test_resident_loader_image_limit_follows_heap_and_stack(self) -> None:
        # The image and one heap byte must fit below C800h minus the stack.
        # With 256 bytes of stack the image may extend through C6FEh.
        for size, status in ((0xC5FF, (0, 0)), (0xC600, (7, 2))):
            with self.subTest(size=size):
                code = bytes((0x21, 0xEF, 0xBE, 0xC9)) + bytes(
                    (i * 7) & 255 for i in range(size - 5)) + b"\x5A"
                library = self.library_record(code, stack=256)
                case, bank = self.run_bank_one_payload(
                    b"", operation=8, lib_file=library)
                self.assertEqual((case.machine.hl, bank[0xE4D1]), status)
                if not status[0]:
                    self.assertEqual(bank[0x100:0x100 + size], code)

    def test_typed_library_exposes_signature_offset_and_image_length(self) -> None:
        code = b"\x21\x01\x00\xC9u16()\0"
        case, bank = self.run_bank_one_payload(
            b"", operation=8, lib_file=self.library_record(code, signature=4))
        self.assertEqual(case.machine.hl, 0)
        self.assertEqual(bank[0xE4D1:0xE4D3], bytes((0, 1)))
        self.assertEqual(bank[0xE4D4:0xE4D6], len(code).to_bytes(2, "little"))
        self.assertEqual(bank[0xE308:0xE30C], b"\0\0\x04\0")

    def test_typed_library_rejects_out_of_image_signature_offsets(self) -> None:
        code = b"\x21\x01\x00\xC9"
        for offset in (len(code), 0xFFFF):
            with self.subTest(offset=offset):
                _, bank = self.run_bank_one_payload(
                    b"", operation=8,
                    lib_file=self.library_record(code, signature=offset))
                self.assertEqual(bank[0xE4D1], 2)

    def test_native_linker_emits_checked_typed_exports(self) -> None:
        root = Path(__file__).resolve().parents[3]
        dcc = Path(os.environ.get("DCC_DIR", str(root.parent / "dcc")))
        if not (dcc / "m80c").is_file() or not (dcc / "l80c").is_file():
            self.skipTest("native library linker and assembler are required")
        with tempfile.TemporaryDirectory(prefix="typed-linker-") as directory:
            folder = Path(directory)
            (folder / "LIB.MAC").write_text(
                "\tcseg\n\tpublic _b1e00\n_b1e00:\n"
                "\tld hl,1\n\tret\n"
                "\tpublic _b1s00\n_b1s00:\n\tdb 'u16()',0\n\tend\n")
            subprocess.run(
                [str(dcc / "m80c"), "=LIB.MAC", "/X", "/O", "/Z", "/L", "/C"],
                cwd=folder, check=True, capture_output=True)
            base = [str(dcc / "l80c"), "/P:100,LIB", "--lib",
                    "--export", "VALUE=_b1e00", "-o", "TYPED"]
            subprocess.run(base + ["--signature", "VALUE=_b1s00"],
                           cwd=folder, check=True, capture_output=True)
            library = (folder / "TYPED.LIB").read_bytes()
            self.assertEqual(library[:8], b"B1LB\x01\x01\x00\x01")
            self.assertEqual(int.from_bytes(library[18:20], "little"), 38)
            signature = int.from_bytes(library[36:38], "little")
            self.assertEqual(library[38 + signature:38 + signature + 6],
                             b"u16()\0")
            case, bank = self.run_bank_one_payload(
                b"", operation=8, lib_file=library)
            self.assertEqual((case.machine.hl, bank[0xE4D2]), (0, 1))
            eight = base + ["--signature", "VALUE=_b1s00"]
            for index in range(1, 8):
                eight += ["--export", f"V{index}=_b1e00",
                          "--signature", f"V{index}=_b1s00"]
            subprocess.run(eight, cwd=folder, check=True, capture_output=True)
            library = (folder / "TYPED.LIB").read_bytes()
            self.assertEqual(int.from_bytes(library[18:20], "little"), 122)
            case, bank = self.run_bank_one_payload(
                b"", operation=8, lib_file=library)
            self.assertEqual((case.machine.hl, bank[0xE4D2]), (0, 8))
            self.assertEqual(bank[0x100:0x10A], b"\x21\x01\0\xC9u16()\0")
            for arguments in (
                [],
                ["--signature", "OTHER=_b1s00"],
                ["--signature", "VALUE=_b1s00",
                 "--signature", "VALUE=_b1s00"],
                ["--signature", "VALUE=_none"],
                ["--signature", "VALUE="],
            ):
                with self.subTest(arguments=arguments):
                    result = subprocess.run(
                        base + arguments, cwd=folder,
                        capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("l80c:", result.stderr)

    def test_resident_loader_initializes_heap_for_each_supported_stack(self) -> None:
        # A second record replaces the header DMA buffer before heap setup.
        for stack in range(256, 2049, 256):
            with self.subTest(stack=stack):
                library = self.library_record(bytes(128), brk=0, hlimit=2,
                                              stack=stack)
                case, bank = self.run_bank_one_payload(
                    b"", operation=8, lib_file=library)
                self.assertEqual((case.machine.hl, bank[0xE4D1]), (0, 0))
                self.assertEqual(int.from_bytes(bank[0x100:0x102], "little"),
                                 0x180)
                self.assertEqual(int.from_bytes(bank[0x102:0x104], "little"),
                                 0xC800 - stack)

    def test_resident_loader_clears_bss_but_not_free_heap(self) -> None:
        library = self.library_record(bytes(128), brk=0, hlimit=2,
                                      stack=512, heap_start=0x4000)
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual(case.machine.hl, 0)
        self.assertEqual(bank[0x180:0x4000], bytes(0x4000 - 0x180))
        self.assertEqual(bank[0x4000:0x4010], b"\xA5" * 16)
        self.assertEqual(int.from_bytes(bank[0x100:0x102], "little"), 0x4000)
        self.assertEqual(int.from_bytes(bank[0x102:0x104], "little"), 0xC600)

    def test_resident_loader_rejects_heap_overlapping_image_or_stack(self) -> None:
        for heap_start in (0, 0x103, 0xC000, 0xFFFF):
            with self.subTest(heap_start=heap_start):
                library = self.library_record(bytes(4), heap_start=heap_start)
                case, bank = self.run_bank_one_payload(
                    b"", operation=8, lib_file=library)
                self.assertEqual(
                    (case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (7, 2, 0))

    def test_dcc_malloc_uses_reclaimed_heap_after_zeroed_bss(self) -> None:
        root = Path(__file__).resolve().parents[3]
        dcc = Path(os.environ.get("DCC_DIR", str(root.parent / "dcc")))
        if not (dcc / "dcc").exists():
            self.skipTest("sibling DCC toolchain is unavailable")
        subprocess.run(
            [str(dcc / "dccmake")], cwd=root / "examples/LIBDEMO/lib",
            env={**os.environ, "DCC_DIR": str(dcc)},
            check=True, capture_output=True)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "HEAP.C").write_text(
                "#include <stdlib.h>\n"
                "static unsigned char bss[2048];\n"
                "char hsig[] = \"u16(u16)\";\n"
                "unsigned int check(unsigned int *unused) {\n"
                " unsigned char *p;\n"
                " (void)unused;\n"
                " if (bss[0] || bss[2047]) return 0;\n"
                " p = (unsigned char *)malloc(32768U);\n"
                " if (!p) return 0;\n"
                " p[0] = 17; p[32767] = 29;\n"
                " bss[0] = p[0]; bss[2047] = p[32767];\n"
                " free(p);\n"
                " return bss[0] == 17 && bss[2047] == 29 ? 0xBEEF : 0;\n"
                "}\n")
            commands = [
                [str(dcc / "dcc"), "-stack", "512", "HEAP.C", "-o", "HEAP.MAC"],
                [str(dcc / "dccrtlstrip"), "-r",
                 str(root / "examples/LIBDEMO/lib/build/DCCRTL.MAC"),
                 "-o", "RTLMIN.MAC", "HEAP.MAC"],
                [str(dcc / "m80c"), "=HEAP.MAC", "/X", "/O", "/Z", "/L"],
                [str(dcc / "m80c"), "=RTLMIN.MAC", "/X", "/O", "/Z", "/L"],
                [str(dcc / "l80c"), "/P:100,RTLMIN,HEAP", "--lib",
                 "--stack-bytes", "512", "--export", "_HEAP=_CHECK",
                 "--signature", "_HEAP=_HSIG",
                 "-o", "HEAP"],
            ]
            for command in commands:
                result = subprocess.run(command, cwd=folder, capture_output=True,
                                        text=True)
                if result.returncode:
                    for listing in folder.glob("*.PRN"):
                        lines = listing.read_text().splitlines()
                        for index, line in enumerate(lines):
                            if line.startswith("*****"):
                                print("\n".join(lines[max(0, index - 2):index + 1]))
                self.assertEqual(result.returncode, 0,
                                 f"{command}: {result.stdout}\n{result.stderr}")
                if command[0] == str(dcc / "dccrtlstrip"):
                    runtime = folder / "RTLMIN.MAC"
                    runtime.write_text(
                        runtime.read_text().replace("\tend\tstart", "\tend"))
            library = (folder / "HEAP.LIB").read_bytes()
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual(case.machine.hl, 0)
        machine = case.machine
        machine.pc, machine.sp = 0x1000, 0xD000
        machine.hl, machine.e, machine.bc = 0x100, 5, 0xC000
        machine.ix = int.from_bytes(bank[0xE308:0xE30A], "little")
        machine.iy = 0
        machine.ticks_to_stop = 5_000_000
        for _ in range(100):
            if machine.run() & machine._BREAKPOINT_HIT:
                break
        else:
            self.fail("DCC heap probe did not return")
        self.assertEqual(machine.hl, 0xBEEF)

    def test_resident_loader_rejects_invalid_stack_sizes(self) -> None:
        for stack in (0, 1, 255, 257, 2049, 2304, 65535):
            with self.subTest(stack=stack):
                library = self.library_record(bytes(4), stack=stack)
                case, bank = self.run_bank_one_payload(
                    b"", operation=8, lib_file=library)
                self.assertEqual(
                    (case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (7, 2, 0))

    def test_resident_loader_rejects_other_format_and_abi_bytes(self) -> None:
        for offset, value in ((4, 0), (4, 2), (4, 255), (5, 2), (6, 1), (7, 2),
                              (8, 1), (9, 2)):
            with self.subTest(offset=offset, value=value):
                library = bytearray(self.library_record(bytes(4)))
                library[offset] = value
                case, bank = self.run_bank_one_payload(
                    b"", operation=8, lib_file=library)
                self.assertEqual(
                    (case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (7, 2, 0))

    def test_resident_loader_rejects_changed_header_field(self) -> None:
        library = bytearray(self.library_record(bytes((0x21, 0xEF, 0xBE, 0xC9))))
        library[20] = 0  # __brk without __hlimit is an incomplete runtime.
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual((case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (7, 2, 0))

    def test_resident_loader_rejects_wrapped_runtime_offset(self) -> None:
        library = self.library_record(bytes((0x21, 0xEF, 0xBE, 0xC9)),
                                      brk=0, hlimit=0xFFFF)
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual((case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (7, 2, 0))
        self.assertEqual(bank[0x00FF], 0)

    def test_resident_loader_rejects_export_past_image(self) -> None:
        library = self.library_record(bytes((0x21, 0xEF, 0xBE, 0xC9)),
                                      entry=0x0100)
        case, bank = self.run_bank_one_payload(b"", operation=8, lib_file=library)
        self.assertEqual((case.machine.hl, bank[0xE4D1], bank[0xDE9F]), (7, 2, 0))


if __name__ == "__main__":
    unittest.main()
