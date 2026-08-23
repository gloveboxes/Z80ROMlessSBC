#!/usr/bin/env python3
"""Convert framed Altair 88-DCDD images into Pico flash disk slots."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from geometry import IMAGE_BYTES as OUTPUT_BYTES
from geometry import RECORD_BYTES
from geometry import SECTORS_PER_TRACK
from geometry import TRACKS as OUTPUT_TRACKS

SOURCE_TRACKS = 77
PHYSICAL_SECTOR_BYTES = 137
RECORD_OFFSET = 3
SOURCE_GRID_BYTES = SOURCE_TRACKS * SECTORS_PER_TRACK * PHYSICAL_SECTOR_BYTES
SECTOR_TRANSLATION = (
    1,
    9,
    17,
    25,
    3,
    11,
    19,
    27,
    5,
    13,
    21,
    29,
    7,
    15,
    23,
    31,
    2,
    10,
    18,
    26,
    4,
    12,
    20,
    28,
    6,
    14,
    22,
    30,
    8,
    16,
    24,
    32,
)

DISKS = {
    "drive_a_cpm63k.img": "cpm63k.dsk",
    "drive_b_bdsc.img": "bdsc-v1.60.dsk",
    "drive_c_escape.img": "escape-posix.dsk",
    "drive_d_blank.img": "blank.dsk",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def convert(source: bytes) -> bytes:
    if len(source) not in {SOURCE_GRID_BYTES, SOURCE_GRID_BYTES + 96}:
        raise ValueError(
            f"unexpected Altair image size {len(source)}; "
            f"expected {SOURCE_GRID_BYTES} or {SOURCE_GRID_BYTES + 96}"
        )

    output = bytearray([0xE5]) * OUTPUT_BYTES
    for track in range(SOURCE_TRACKS):
        for logical_sector, physical_sector in enumerate(SECTOR_TRANSLATION):
            source_index = track * SECTORS_PER_TRACK + physical_sector - 1
            source_offset = source_index * PHYSICAL_SECTOR_BYTES
            output_index = track * SECTORS_PER_TRACK + logical_sector
            output_offset = output_index * RECORD_BYTES
            output[output_offset : output_offset + RECORD_BYTES] = source[
                source_offset + RECORD_OFFSET : source_offset
                + RECORD_OFFSET
                + RECORD_BYTES
            ]
    return bytes(output)


def main() -> None:
    disk_dir = Path(__file__).resolve().parent
    source_dir = disk_dir / "source-altair"
    output_dir = disk_dir / "generated"
    output_dir.mkdir(exist_ok=True)

    manifest: dict[str, object] = {
        "format": "80 tracks x 32 records x 128 bytes",
        "image_bytes": OUTPUT_BYTES,
        "images": {},
    }
    images = manifest["images"]
    assert isinstance(images, dict)

    for output_name, source_name in DISKS.items():
        source = (source_dir / source_name).read_bytes()
        output = convert(source)
        (output_dir / output_name).write_bytes(output)
        images[output_name] = {
            "source": source_name,
            "source_bytes": len(source),
            "source_sha256": sha256(source),
            "output_bytes": len(output),
            "output_sha256": sha256(output),
        }

    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="ascii"
    )
    print(f"wrote {len(DISKS)} images and {manifest_path}")


if __name__ == "__main__":
    main()