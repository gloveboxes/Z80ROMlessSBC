from __future__ import annotations

import binascii
import shutil
import struct
import unittest

import build_images


class BuildImagesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assembler = shutil.which("z80asm")
        if assembler is None:
            raise unittest.SkipTest("z80asm is not installed")

        cls.cpm_system = build_images.load_cpm_system()
        cls.bios = build_images.build_bios(assembler)
        cls.z80_image = build_images.build_z80_image(cls.cpm_system, cls.bios)
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
            cls.generated_drive, cls.cpm_system, cls.bios
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
            self.cpm_system,
        )
        self.assertEqual(self.cpm_system[:3], bytes((0xC3, 0x5C, 0xE9)))
        self.assertEqual(
            self.cpm_system[0x806:0x809], bytes((0xC3, 0x11, 0xEE))
        )
        self.assertEqual(
            self.z80_image[
                build_images.BIOS_BASE : build_images.BIOS_BASE + len(self.bios)
            ],
            self.bios,
        )

    def test_native_drive_only_replaces_system_tracks(self) -> None:
        system = self.cpm_system + self.bios
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
        self.assertEqual(build_images.CCP_BASE, 0xE600)
        self.assertEqual(build_images.BDOS_ENTRY, 0xEE06)
        self.assertEqual(build_images.BIOS_BASE, 0xFC00)
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