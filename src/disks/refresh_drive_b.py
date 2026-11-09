#!/usr/bin/env python3
"""Install the current banked-memory examples into the drive B Altair source disk.

Run this after rebuilding the examples, then run convert_altair_disks.py to
refresh src/disks/generated/.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

DISK_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DISK_DIR))
sys.path.insert(0, str(DISK_DIR.parent / "cpm"))

import build_images
import convert_altair_disks as altair

SOURCE = DISK_DIR / "source-altair" / altair.DISKS["drive_b_bdsc.img"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpmcp", default=shutil.which("cpmcp"))
    args = parser.parse_args()
    if args.cpmcp is None:
        raise SystemExit("cpmcp was not found; install cpmtools or pass --cpmcp")
    template = SOURCE.read_bytes()
    native = altair.convert(template)
    updated = build_images.install_files(
        native, build_images.example_files(), build_images.EXAMPLE_REMOVED_FILES, args.cpmcp
    )
    SOURCE.write_bytes(altair.encode(updated, template))
    print(f"updated {SOURCE}")


if __name__ == "__main__":
    main()
