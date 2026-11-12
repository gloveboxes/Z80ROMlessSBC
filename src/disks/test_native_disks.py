from __future__ import annotations

from pathlib import Path
import sys
import unittest


DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR))
sys.path.insert(0, str(DISK_DIR.parent / "cpm"))

import build_images
import geometry

NATIVE = DISK_DIR / "native"


class NativeDisksTest(unittest.TestCase):
    def test_every_source_is_exactly_one_native_image(self) -> None:
        self.assertEqual(sorted(path.name for path in NATIVE.glob("*.img")),
                         sorted(build_images.SOURCE_DISK_NAMES))
        for name in build_images.SOURCE_DISK_NAMES:
            with self.subTest(image=name):
                self.assertEqual((NATIVE / name).stat().st_size, geometry.IMAGE_BYTES)

    def test_all_directory_extents_match_native_dpb(self) -> None:
        directory_offset = geometry.RESERVED_TRACKS * geometry.SECTORS_PER_TRACK * geometry.RECORD_BYTES
        directory_bytes = (geometry.DPB_DRM + 1) * 32
        records_per_block = 1 << geometry.DPB_BSH
        blocks_per_extent = 128 // records_per_block

        self.assertEqual(geometry.DPB_EXM, 0)
        self.assertEqual(blocks_per_extent, 8)
        for name in build_images.SOURCE_DISK_NAMES:
            image = (NATIVE / name).read_bytes()
            directory = image[directory_offset:directory_offset + directory_bytes]
            files: dict[tuple[int, bytes], list[int]] = {}
            allocated: dict[int, str] = {}
            seen_entries: set[bytes] = set()
            for index in range(geometry.DPB_DRM + 1):
                entry = directory[index * 32:(index + 1) * 32]
                if entry[0] > 15 or entry in seen_entries:
                    continue
                seen_entries.add(entry)
                label = f"{name}: directory entry {index}"
                extent = entry[12] + 32 * (entry[14] & 0x3F)
                records = entry[15]
                self.assertLessEqual(records, 128, label)
                used_blocks = (records + records_per_block - 1) // records_per_block
                blocks = entry[16:16 + blocks_per_extent]
                self.assertTrue(all(blocks[:used_blocks]), label)
                self.assertEqual(blocks[used_blocks:], bytes(blocks_per_extent - used_blocks), label)
                self.assertEqual(entry[16 + blocks_per_extent:32], bytes(8), label)
                files.setdefault((entry[0], entry[1:12]), []).append(extent)
                for block in blocks[:used_blocks]:
                    self.assertLessEqual(block, geometry.DPB_DSM, label)
                    self.assertNotIn(block, allocated, label)
                    allocated[block] = label
            for key, extents in files.items():
                self.assertEqual(sorted(extents), list(range(len(extents))), f"{name}: {key[1]!r}")

if __name__ == "__main__":
    unittest.main()
