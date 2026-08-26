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
        djnz_branches = []
        section_ends = {}
        while address < disassemble_cpm64.BIOS_BASE:
            if address == disassemble_cpm64.BDOS_BASE:
                section_ends["ccp"] = output_address
                output_address = disassemble_cpm64.BDOS_BASE
            relocated[address] = output_address
            instruction = instructions.get(address)
            if instruction is None:
                size = 1
            elif address == 0xE4A7:
                relocated[0xE4AB] = output_address + 9
                size = 17
                address = 0xE4BA
                output_address += size
                continue
            elif address == 0xE4BA:
                size = 1
            elif address == 0xE742:
                size = 6
                address = 0xE74B
                output_address += size
                continue
            elif address == 0xEEEF:
                size = 5
                address = 0xEEF5
                output_address += size
                continue
            elif address in disassemble_cpm64.DJNZ_SITES:
                jump = instructions[address + instruction.size]
                djnz_branches.append((instruction, jump.target))
                size = 2
                address += instruction.size + jump.size
                output_address += size
                continue
            elif address in disassemble_cpm64.SUB_TWO_SITES:
                following_decrement = instructions[address + instruction.size]
                size = 2
                address += instruction.size + following_decrement.size
                output_address += size
                continue
            elif address == 0xF113:
                size = 2
                address = 0xF119
                output_address += size
                continue
            elif address in disassemble_cpm64.CONDITIONAL_TAIL_CALLS:
                following_return = instructions[address + instruction.size]
                size = 4
                address += instruction.size + following_return.size
                output_address += size
                continue
            elif address in disassemble_cpm64.THREADED_CONDITIONAL_JUMPS:
                size = 3
            elif address == 0xF4FB:
                size = 0
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
        section_ends["bdos"] = output_address

        jr_opcodes = {
            "JMP": 0x18,
            "JNZ": 0x20,
            "JZ": 0x28,
            "JNC": 0x30,
            "JC": 0x38,
        }
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

        self.assertEqual(
            disassemble_cpm64.BDOS_BASE - section_ends["ccp"], 109
        )
        self.assertEqual(
            disassemble_cpm64.BIOS_BASE - section_ends["bdos"], 151
        )
        self.assertEqual(len(djnz_branches), 10)
        for instruction, target in djnz_branches:
            source = relocated[instruction.address]
            offset = source - disassemble_cpm64.ORIGIN
            self.assertEqual(self.z80_system[offset], 0x10)
            displacement = int.from_bytes(
                self.z80_system[offset + 1 : offset + 2], signed=True
            )
            self.assertEqual(source + 2 + displacement, relocated[target])

        copy = relocated[0xE742] - disassemble_cpm64.ORIGIN
        self.assertEqual(
            self.z80_system[copy : copy + 6],
            bytes((0x48, 0x06, 0x00, 0xED, 0xB0, 0xC9)),
        )
        inverted = relocated[0xF113] - disassemble_cpm64.ORIGIN
        self.assertEqual(self.z80_system[inverted], 0x38)
        displacement = int.from_bytes(
            self.z80_system[inverted + 1 : inverted + 2], signed=True
        )
        self.assertEqual(
            relocated[0xF113] + 2 + displacement, relocated[0xF0FE]
        )

        for address, target in disassemble_cpm64.THREADED_CONDITIONAL_JUMPS.items():
            offset = relocated[address] - disassemble_cpm64.ORIGIN
            opcode = 0xCA if instructions[address].mnemonic == "JZ" else 0xC2
            self.assertEqual(self.z80_system[offset], opcode)
            destination = int.from_bytes(
                self.z80_system[offset + 1 : offset + 3], byteorder="little"
            )
            self.assertEqual(destination, relocated[target])

        for address in disassemble_cpm64.CONDITIONAL_TAIL_CALLS:
            offset = relocated[address] - disassemble_cpm64.ORIGIN
            self.assertEqual(self.z80_system[offset : offset + 2], bytes((0xC0, 0xC3)))

        de_subtract = relocated[0xEEEF] - disassemble_cpm64.ORIGIN
        self.assertEqual(
            self.z80_system[de_subtract : de_subtract + 5],
            bytes((0xB7, 0xEB, 0xED, 0x52, 0xEB)),
        )
        for address in disassemble_cpm64.SUB_TWO_SITES:
            offset = relocated[address] - disassemble_cpm64.ORIGIN
            self.assertEqual(self.z80_system[offset : offset + 2], bytes((0xD6, 0x02)))

        for table_address, count in (
            disassemble_cpm64.CCP_COMMAND_TABLE,
            disassemble_cpm64.BDOS_ERROR_TABLE,
            disassemble_cpm64.BDOS_FUNCTION_TABLE,
        ):
            output = relocated[table_address] - disassemble_cpm64.ORIGIN
            for index, target in enumerate(
                disassemble_cpm64.table_targets(
                    self.cpm_system, table_address, count
                )
            ):
                actual = int.from_bytes(
                    self.z80_system[output + index * 2 : output + index * 2 + 2],
                    byteorder="little",
                )
                expected = relocated.get(target, target)
                self.assertEqual(
                    actual,
                    expected,
                    f"relocated table target mismatch at {table_address + index * 2:04X}",
                )

        workspace_pointer = relocated[0xE388] - disassemble_cpm64.ORIGIN
        self.assertEqual(
            int.from_bytes(
                self.z80_system[workspace_pointer : workspace_pointer + 2],
                byteorder="little",
            ),
            relocated[0xE308],
        )
        for site, target, opcode in (
            (0xE6D2, 0xE300, 0x22),
            (0xEBEE, 0xEBC6, 0x32),
        ):
            offset = relocated[site] - disassemble_cpm64.ORIGIN
            self.assertEqual(self.z80_system[offset], opcode)
            self.assertEqual(
                int.from_bytes(
                    self.z80_system[offset + 1 : offset + 3], byteorder="little"
                ),
                relocated[target],
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