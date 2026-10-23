from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR))

import convert_altair_disks
import geometry


class ConvertAltairDisksTest(unittest.TestCase):
    def test_cli_converts_current_sources_into_separate_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_dir = Path(temporary) / "nested" / "converted"
            with mock.patch.object(
                sys, "argv", ["convert_altair_disks.py", "--output-dir", str(output_dir)]
            ):
                convert_altair_disks.main()
            manifest = json.loads((output_dir / "manifest.json").read_text())
            for output_name, source_name in convert_altair_disks.DISKS.items():
                with self.subTest(image=output_name):
                    source = (DISK_DIR / "source-altair" / source_name).read_bytes()
                    output = (output_dir / output_name).read_bytes()
                    self.assertEqual(output, convert_altair_disks.convert(source))
                    self.assertEqual(len(output), geometry.IMAGE_BYTES)
                    self.assertEqual(
                        manifest["images"][output_name]["source_sha256"],
                        convert_altair_disks.sha256(source),
                    )
                    self.assertEqual(
                        manifest["images"][output_name]["output_sha256"],
                        convert_altair_disks.sha256(output),
                    )

    def test_cli_rejects_corrupt_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with (
                mock.patch.object(
                    sys, "argv", ["convert_altair_disks.py", "--output-dir", temporary]
                ),
                mock.patch.object(Path, "read_bytes", return_value=b"invalid"),
                self.assertRaisesRegex(ValueError, "size"),
            ):
                convert_altair_disks.main()
            self.assertEqual(list(Path(temporary).iterdir()), [])

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
            convert_altair_disks.validate_source(source)
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

    def test_source_validator_rejects_corrupt_sectors_and_ids(self) -> None:
        source = (DISK_DIR / "source-altair/bdsc-v1.60.dsk").read_bytes()
        high_track = 6 * geometry.SECTORS_PER_TRACK * convert_altair_disks.PHYSICAL_SECTOR_BYTES
        for offset in (131, 132, high_track, high_track + 4, high_track + 7, high_track + 135):
            corrupted = bytearray(source)
            corrupted[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "framing/checksum"):
                convert_altair_disks.validate_source(bytes(corrupted))
        duplicate = bytearray(source)
        duplicate[high_track + 1] = duplicate[
            high_track + convert_altair_disks.PHYSICAL_SECTOR_BYTES + 1
        ]
        with self.assertRaisesRegex(ValueError, "sector IDs"):
            convert_altair_disks.validate_source(bytes(duplicate))
        with self.assertRaisesRegex(ValueError, "size"):
            convert_altair_disks.validate_source(source[:-1])


if __name__ == "__main__":
    unittest.main()