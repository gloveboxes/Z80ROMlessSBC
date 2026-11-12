#!/usr/bin/env python3
"""Save a drive image modified in an emulator session into its checked-in source.

Interactive sessions write to build/cpm-session/drive_*.img, regenerated from
src/disks/native/. Run this after the session ends, then
commit the result. Drive A's system tracks are rebuilt by build_images.py, so
only changes to its files persist.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR.parent / "cpm"))

import build_images

DRIVES = dict(zip("ABCD", build_images.SOURCE_DISK_NAMES))
def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("drive", choices=DRIVES)
    parser.add_argument("image", nargs="?", type=Path,
                        help="session image (default: build/cpm-session/<drive image name>)")
    args = parser.parse_args()
    name = DRIVES[args.drive]
    image = args.image or DISK_DIR.parents[1] / "build/cpm-session" / name
    data = image.read_bytes()
    if len(data) != build_images.DISK_IMAGE_BYTES:
        raise SystemExit(f"{image} is not a {build_images.DISK_IMAGE_BYTES}-byte native image")
    target = DISK_DIR / "native" / name
    target.write_bytes(data)
    print(f"saved {image} into {target}")


if __name__ == "__main__":
    main()
