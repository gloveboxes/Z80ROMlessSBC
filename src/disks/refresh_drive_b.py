#!/usr/bin/env python3
"""Install the current examples into the native drive B image.

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
    files = build_images.example_files()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpmcp", default=shutil.which("cpmcp"))
    parser.add_argument("--program", action="append",
                        choices=sorted(Path(path).name for path in build_images.EXAMPLE_PROGRAMS),
                        help="install only this example (repeatable); preserve other files")
    args = parser.parse_args()
    if args.cpmcp is None:
        raise SystemExit("cpmcp was not found; install cpmtools or pass --cpmcp")
    selected = files if args.program is None else {name: files[name] for name in args.program}
    removed = build_images.EXAMPLE_REMOVED_FILES if args.program is None else ()
    IMAGE.write_bytes(build_images.install_files(
        IMAGE.read_bytes(), selected, removed, args.cpmcp,
    ))
    print(f"updated {IMAGE}")


if __name__ == "__main__":
    main()
