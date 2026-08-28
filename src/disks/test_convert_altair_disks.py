from __future__ import annotations

from pathlib import Path
import sys
import unittest


DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR))

import convert_altair_disks
import geometry


class ConvertAltairDisksTest(unittest.TestCase):
    def test_every_record_uses_authentic_burcon_mapping(self) -> None:
        source = bytearray(convert_altair_disks.SOURCE_GRID_BYTES)
        expected: dict[tuple[int, int], bytes] = {}

        for track in range(convert_altair_disks.SOURCE_TRACKS):
            record_offset = 3 if track < 6 else 7
            for slot in range(geometry.SECTORS_PER_TRACK):
                payload = bytes(
                    ((track * 37 + slot * 11 + index) & 0xFF)
                    for index in range(geometry.RECORD_BYTES)
                )
                source_offset = (
                    (track * geometry.SECTORS_PER_TRACK + slot)
                    * convert_altair_disks.PHYSICAL_SECTOR_BYTES
                )
                source[
                    source_offset + record_offset :
                    source_offset + record_offset + geometry.RECORD_BYTES
                ] = payload
                expected[track, slot] = payload

        converted = convert_altair_disks.convert(bytes(source))
        for track in range(convert_altair_disks.SOURCE_TRACKS):
            for logical_sector, translated_sector in enumerate(
                convert_altair_disks.SECTOR_TRANSLATION
            ):
                slot = translated_sector - 1
                if track >= 6:
                    slot = (slot * 17) % geometry.SECTORS_PER_TRACK
                output_offset = (
                    (track * geometry.SECTORS_PER_TRACK + logical_sector)
                    * geometry.RECORD_BYTES
                )
                self.assertEqual(
                    converted[output_offset : output_offset + geometry.RECORD_BYTES],
                    expected[track, slot],
                    f"track {track}, logical sector {logical_sector}",
                )

        padding_offset = (
            convert_altair_disks.SOURCE_TRACKS
            * geometry.SECTORS_PER_TRACK
            * geometry.RECORD_BYTES
        )
        self.assertEqual(
            converted[padding_offset:],
            bytes([0xE5]) * (geometry.IMAGE_BYTES - padding_offset),
        )

    def test_generated_images_match_all_converted_records(self) -> None:
        for output_name, source_name in convert_altair_disks.DISKS.items():
            with self.subTest(image=output_name):
                source = (DISK_DIR / "source-altair" / source_name).read_bytes()
                generated = (DISK_DIR / "generated" / output_name).read_bytes()
                converted = convert_altair_disks.convert(source)
                self.assertEqual(generated, converted)
                self.assertEqual(len(generated), geometry.IMAGE_BYTES)

    def test_all_directory_extents_match_native_dpb(self) -> None:
        directory_offset = geometry.RESERVED_TRACKS * geometry.SECTORS_PER_TRACK * geometry.RECORD_BYTES
        directory_bytes = (geometry.DPB_DRM + 1) * 32
        records_per_block = 1 << geometry.DPB_BSH
        blocks_per_extent = 128 // records_per_block

        self.assertEqual(geometry.DPB_EXM, 0)
        self.assertEqual(blocks_per_extent, 8)
        for output_name in convert_altair_disks.DISKS:
            image = (DISK_DIR / "generated" / output_name).read_bytes()
            directory = image[directory_offset : directory_offset + directory_bytes]
            files: dict[tuple[int, bytes], list[int]] = {}
            allocated: dict[int, str] = {}
            seen_entries: set[bytes] = set()
            for index in range(geometry.DPB_DRM + 1):
                entry = directory[index * 32 : (index + 1) * 32]
                if entry[0] > 15:
                    continue
                if entry in seen_entries:
                    continue
                seen_entries.add(entry)
                label = f"{output_name}: directory entry {index}"
                extent = entry[12] + 32 * (entry[14] & 0x3F)
                records = entry[15]
                self.assertLessEqual(records, 128, label)
                used_blocks = (records + records_per_block - 1) // records_per_block
                blocks = entry[16 : 16 + blocks_per_extent]
                self.assertTrue(all(blocks[:used_blocks]), label)
                self.assertEqual(blocks[used_blocks:], bytes(blocks_per_extent - used_blocks), label)
                self.assertEqual(entry[16 + blocks_per_extent : 32], bytes(8), label)
                key = (entry[0], entry[1:12])
                files.setdefault(key, []).append(extent)
                for block in blocks[:used_blocks]:
                    self.assertLessEqual(block, geometry.DPB_DSM, label)
                    self.assertNotIn(block, allocated, label)
                    allocated[block] = label
            for key, extents in files.items():
                self.assertEqual(
                    sorted(extents),
                    list(range(len(extents))),
                    f"{output_name}: {key[1]!r}",
                )

    def test_source_sector_framing_and_checksums(self) -> None:
        for output_name, source_name in convert_altair_disks.DISKS.items():
            source = (DISK_DIR / "source-altair" / source_name).read_bytes()
            for track in range(convert_altair_disks.SOURCE_TRACKS):
                sector_ids: list[int] = []
                for slot in range(geometry.SECTORS_PER_TRACK):
                    offset = (
                        (track * geometry.SECTORS_PER_TRACK + slot)
                        * convert_altair_disks.PHYSICAL_SECTOR_BYTES
                    )
                    sector = source[
                        offset : offset + convert_altair_disks.PHYSICAL_SECTOR_BYTES
                    ]
                    label = f"{output_name}: track {track}, slot {slot}"
                    if track < 6:
                        self.assertEqual(sector[131], 0xFF, label)
                        self.assertEqual(sector[132], sum(sector[3:131]) & 0xFF, label)
                    else:
                        self.assertEqual(sector[0] & 0x7F, track, label)
                        sector_ids.append(sector[1])
                        self.assertEqual(sector[135:137], b"\xFF\x00", label)
                        checksum = (
                            sum(sector[7:135])
                            + sector[2]
                            + sector[3]
                            + sector[5]
                            + sector[6]
                        ) & 0xFF
                        self.assertEqual(sector[4], checksum, label)
                if track >= 6:
                    self.assertEqual(
                        sorted(sector_ids),
                        list(range(geometry.SECTORS_PER_TRACK)),
                        f"{output_name}: track {track}",
                    )


if __name__ == "__main__":
    unittest.main()