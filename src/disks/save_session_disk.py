#!/usr/bin/env python3
"""Save a drive image modified in an emulator session into its checked-in source.

Interactive sessions write to build/cpm/drive_*.img, which build_images.py
regenerates. Run this after the session ends, then rerun convert_altair_disks.py
to refresh src/disks/generated/. Drive D is stored as a native image; B and C
are re-framed into their Altair source disks and must fit its 77 tracks. Drive
A is excluded: it carries board-native system tracks.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR))

import convert_altair_disks as altair

DRIVES = {"B": "drive_b_bdsc.img", "C": "drive_c_escape.img", "D": "drive_d_blank.img"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("drive", choices=DRIVES)
    parser.add_argument("image", nargs="?", type=Path,
                        help="session image (default: build/cpm/<generated image name>)")
    args = parser.parse_args()
    name = DRIVES[args.drive]
    image = args.image or DISK_DIR.parents[1] / "build/cpm" / name
    data = image.read_bytes()
    if name in altair.NATIVE_DISKS:
        if len(data) != altair.OUTPUT_BYTES:
            raise SystemExit(f"{image} is not a {altair.OUTPUT_BYTES}-byte native image")
        target = DISK_DIR / "source-native" / altair.NATIVE_DISKS[name]
        target.write_bytes(data)
    else:
        target = DISK_DIR / "source-altair" / altair.DISKS[name]
        target.write_bytes(altair.encode(data, target.read_bytes()))
    print(f"saved {image} into {target}")


if __name__ == "__main__":
    main()
