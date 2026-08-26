from __future__ import annotations

import binascii
import shutil
import struct
import unittest

import build_images
import disassemble_cpm64


class BuildImagesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assembler = shutil.which("z80asm")
        if assembler is None:
            raise unittest.SkipTest("z80asm is not installed")

        cls.cpm_system = build_images.load_cpm_system()
        cls.z80_system = build_images.build_cpm_system(assembler)
        cls.bios = build_images.build_bios(assembler)
        cls.z80_image = build_images.build_z80_image(cls.z80_system, cls.bios)
        cls.package = build_images.build_boot_package(cls.z80_image)

        generated_path = (
            build_images.REPO_ROOT
            / "src"
            / "disks"
            / "generated"
            / "drive_a_cpm63k.img"
        )
        cls.generated_drive = generated_path.read_bytes()
        cls.native_drive = build_images.build_native_drive(
            cls.generated_drive, cls.z80_system, cls.bios
        )

    def test_boot_package_header_and_memory_map(self) -> None:
        header = self.package[: build_images.BOOT_HEADER_BYTES]
        magic, version, header_bytes, image_bytes, image_crc, header_crc = (
            struct.unpack("<IHHIII", header)
        )
        self.assertEqual(magic, build_images.BOOT_MAGIC)
        self.assertEqual(version, build_images.BOOT_VERSION)
        self.assertEqual(header_bytes, len(header))
        self.assertEqual(image_bytes, build_images.Z80_IMAGE_BYTES)
        self.assertEqual(
            image_crc, binascii.crc32(self.z80_image) & 0xFFFFFFFF
        )
        self.assertEqual(header_crc, binascii.crc32(header[:16]) & 0xFFFFFFFF)
        self.assertEqual(
            self.package[build_images.BOOT_PAYLOAD_OFFSET :], self.z80_image
        )
        self.assertEqual(
            self.package[
                build_images.BOOT_HEADER_BYTES : build_images.BOOT_PAYLOAD_OFFSET
            ],
            bytes([0xFF])
            * (build_images.BOOT_PAYLOAD_OFFSET - build_images.BOOT_HEADER_BYTES),
        )

        reset = bytes(
            (
                0xC3,
                build_images.BIOS_BASE & 0xFF,
                build_images.BIOS_BASE >> 8,
            )
        )
        self.assertEqual(self.z80_image[:3], reset)
        self.assertEqual(
            self.z80_image[build_images.CCP_BASE : build_images.BIOS_BASE],
            self.z80_system,
        )
        self.assertEqual(self.z80_system[0], 0xC3)
        self.assertEqual(self.z80_system[0x801], 22)
        self.assertIn(self.z80_system[0x806], (0x18, 0xC3))
        self.assertEqual(
            self.z80_image[
                build_images.BIOS_BASE : build_images.BIOS_BASE + len(self.bios)
            ],
            self.bios,
        )

    def test_native_drive_only_replaces_system_tracks(self) -> None:
        system = self.z80_system + self.bios
        self.assertEqual(self.native_drive[: len(system)], system)
        self.assertEqual(
            self.native_drive[len(system) : build_images.SYSTEM_BYTES],
            bytes([0xE5]) * (build_images.SYSTEM_BYTES - len(system)),
        )
        self.assertEqual(
            self.native_drive[build_images.SYSTEM_BYTES :],
            self.generated_drive[build_images.SYSTEM_BYTES :],
        )
        self.assertEqual(len(self.native_drive), build_images.DISK_IMAGE_BYTES)

    def test_cpm_and_disk_geometry(self) -> None:
        self.assertEqual(
            build_images.CPM_SYSTEM_RECORDS * build_images.RECORD_BYTES,
            build_images.BIOS_BASE - build_images.CCP_BASE,
        )
        self.assertEqual(build_images.CCP_BASE, 0xE300)
        self.assertEqual(build_images.BDOS_ENTRY, 0xEB06)
        self.assertEqual(build_images.BIOS_BASE, 0xF900)
        self.assertEqual(
            divmod(
                build_images.CPM_SYSTEM_RECORDS,
                build_images.SECTORS_PER_TRACK,
            ),
            (1, 12),
        )
        allocation_bytes = (build_images.DPB_DSM + 8) // 8
        self.assertEqual(allocation_bytes, 20)

        for left, right in zip(
            build_images.FLASH_DISK_OFFSETS,
            build_images.FLASH_DISK_OFFSETS[1:],
        ):
            self.assertEqual(left + build_images.DISK_IMAGE_BYTES, right)
        self.assertEqual(
            build_images.FLASH_DISK_OFFSETS[-1]
            + build_images.DISK_IMAGE_BYTES,
            build_images.FLASH_BYTES,
        )

    def test_bios_jump_table_abi(self) -> None:
        jump_table_entries = 17
        self.assertLessEqual(
            len(self.bios), build_images.Z80_IMAGE_BYTES - build_images.BIOS_BASE
        )
        self.assertEqual(
            self.bios[0 : jump_table_entries * 3 : 3],
            bytes([0xC3]) * jump_table_entries,
        )

        def jump_target(entry: int) -> int:
            offset = entry * 3
            return self.bios[offset + 1] | (self.bios[offset + 2] << 8)

        self.assertNotEqual(jump_target(5), jump_target(4))
        self.assertEqual(jump_target(6), jump_target(5))
        self.assertNotEqual(jump_target(7), jump_target(3))
        self.assertEqual(self.bios[jump_target(5) - build_images.BIOS_BASE], 0xC9)
        reader = jump_target(7) - build_images.BIOS_BASE
        self.assertEqual(self.bios[reader : reader + 3], bytes((0x3E, 0x1A, 0xC9)))

    def test_cpm64_i8080_disassembly(self) -> None:
        image = build_images.load_cpm_system()
        instructions, targets = disassemble_cpm64.discover(image)
        listing = disassemble_cpm64.emit_listing(image, instructions, targets)
        report = "\n".join(disassemble_cpm64.audit(image, instructions)) + "\n"
        self.assertEqual(
            listing,
            (build_images.REPO_ROOT / "src/cpm/cpm64_i8080.asm").read_text(
                encoding="ascii"
            ),
        )
        self.assertEqual(
            report,
            (build_images.REPO_ROOT / "src/cpm/cpm64_i8080_audit.md").read_text(
                encoding="ascii"
            ),
        )

        z80_source = disassemble_cpm64.emit_z80_port(
            image, instructions, targets
        )
        self.assertEqual(
            z80_source,
            build_images.CPM_Z80_SOURCE_PATH.read_text(encoding="ascii"),
        )

    def test_z80_port_translation_and_layout(self) -> None:
        instructions, targets = disassemble_cpm64.discover(self.cpm_system)
        baseline_source = disassemble_cpm64.emit_z80_port(
            self.cpm_system, instructions, targets, optimize=False
        )
        assembler = shutil.which("z80asm")
        assert assembler is not None
        self.assertEqual(
            build_images.assemble_cpm_source(assembler, baseline_source),
            self.cpm_system,
        )
        self.assertEqual(len(self.z80_system), build_images.CPM_SYSTEM_BYTES)
        self.assertNotEqual(self.z80_system, self.cpm_system)
        self.assertEqual(self.z80_system[0x800:0x806], self.cpm_system[0x800:0x806])

        relocated: dict[int, int] = {}
        output_address = disassemble_cpm64.ORIGIN
        address = disassemble_cpm64.ORIGIN
        relative_branches = []
        while address < disassemble_cpm64.BIOS_BASE:
            if address == disassemble_cpm64.BDOS_BASE:
                output_address = disassemble_cpm64.BDOS_BASE
            relocated[address] = output_address
            instruction = instructions.get(address)
            if instruction is None:
                size = 1
            elif address == 0xF0C3:
                size = 0
            elif address in {0xE55E, 0xF503}:
                size = 1
            elif (
                instruction.flow in {"jump", "conditional_jump"}
                and instruction.target is not None
                and ((address < disassemble_cpm64.BDOS_BASE)
                     == (instruction.target < disassemble_cpm64.BDOS_BASE))
                and -128 <= instruction.target - (address + 2) <= 127
                and (
                    instruction.flow == "jump"
                    or instruction.mnemonic[1:] in {"NZ", "Z", "NC", "C"}
                )
            ):
                size = 2
                relative_branches.append(instruction)
            else:
                size = instruction.size
            output_address += size
            address += instruction.size if instruction is not None else 1

        jr_opcodes = {
            "JMP": 0x18,
            "JNZ": 0x20,
            "JZ": 0x28,
            "JNC": 0x30,
            "JC": 0x38,
        }
        self.assertEqual(len(relative_branches), 242)
        for instruction in relative_branches:
            source = relocated[instruction.address]
            offset = source - disassemble_cpm64.ORIGIN
            self.assertEqual(self.z80_system[offset], jr_opcodes[instruction.mnemonic])
            displacement = int.from_bytes(
                self.z80_system[offset + 1 : offset + 2],
                byteorder="little",
                signed=True,
            )
            self.assertEqual(
                source + 2 + displacement,
                relocated[instruction.target],
                f"JR target mismatch for {instruction.address:04X}",
            )

    def test_i8080_decoder_opcode_coverage(self) -> None:
        image = bytes(range(256)) + bytes((0, 0))
        for opcode in range(256):
            instruction = disassemble_cpm64.decode(image, disassemble_cpm64.ORIGIN + opcode)
            if opcode in disassemble_cpm64.UNOFFICIAL_8080:
                self.assertEqual(instruction.flow, "unofficial")
            else:
                self.assertNotEqual(instruction.flow, "unofficial")

        with self.assertRaisesRegex(ValueError, "truncated instruction"):
            disassemble_cpm64.decode(bytes((0xC3,)), disassemble_cpm64.ORIGIN)

    def test_i8080_audit_rejects_wrong_fingerprint(self) -> None:
        image = bytearray(build_images.load_cpm_system())
        image[-1] ^= 1
        instructions, _ = disassemble_cpm64.discover(image)
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            disassemble_cpm64.audit(image, instructions)

    def test_complete_flash_layout(self) -> None:
        firmware = b"stage10"
        disks = [self.native_drive] * build_images.DRIVE_COUNT
        flash = build_images.build_flash_image(firmware, self.package, disks)
        self.assertEqual(len(flash), build_images.FLASH_BYTES)
        self.assertEqual(flash[: len(firmware)], firmware)
        self.assertEqual(
            flash[
                build_images.FLASH_JOURNAL_OFFSET : build_images.FLASH_BOOT_OFFSET
            ],
            bytes([0xFF]) * build_images.FLASH_JOURNAL_BYTES,
        )
        self.assertEqual(
            flash[
                build_images.FLASH_BOOT_OFFSET :
                build_images.FLASH_BOOT_OFFSET + len(self.package)
            ],
            self.package,
        )
        for offset in build_images.FLASH_DISK_OFFSETS:
            self.assertEqual(
                flash[offset : offset + len(self.native_drive)],
                self.native_drive,
            )


if __name__ == "__main__":
    unittest.main()