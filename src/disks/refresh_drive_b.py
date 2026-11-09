#!/usr/bin/env python3
"""Install the current banked-memory examples into the native drive B image.

Run this after rebuilding the examples. build_images.py applies the same
install at build time, so this only keeps the checked-in image current.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR.parent / "cpm"))

import build_images

IMAGE = DISK_DIR / "native" / "drive_b_bdsc.img"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpmcp", default=shutil.which("cpmcp"))
    args = parser.parse_args()
    if args.cpmcp is None:
        raise SystemExit("cpmcp was not found; install cpmtools or pass --cpmcp")
    IMAGE.write_bytes(build_images.install_files(
        IMAGE.read_bytes(), build_images.example_files(),
        build_images.EXAMPLE_REMOVED_FILES, args.cpmcp,
    ))
    print(f"updated {IMAGE}")


if __name__ == "__main__":
    main()
