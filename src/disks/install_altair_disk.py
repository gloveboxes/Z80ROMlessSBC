#!/usr/bin/env python3
"""Install one staged Altair disk into the corresponding native source."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil

import stage_altair_disks as converter
from src.cpm import build_images


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("drive", choices=converter.DISKS)
    parser.add_argument("--confirm", required=True)
    parser.add_argument("--source-dir", type=Path, default=converter.DISK_DIR / "source-altair")
    parser.add_argument("--staged-dir", type=Path, default=converter.REPO_ROOT / "build/altair-disks")
    parser.add_argument("--native-dir", type=Path, default=converter.DISK_DIR / "native")
    args = parser.parse_args()
    if args.confirm != "Replace selected native disk":
        raise SystemExit("Import cancelled; native disks were not changed.")

    _, image_name = converter.DISKS[args.drive]
    image = converter.load_staged([args.drive], args.source_dir, args.staged_dir)[image_name]
    target = args.native_dir / image_name
    if not target.is_file():
        raise SystemExit(f"native disk not found: {target}; native disks were not changed")
    if args.drive == "A":
        assembler = shutil.which("z80asm")
        cpmcp = shutil.which("cpmcp")
        if not assembler or not cpmcp:
            raise SystemExit("z80asm and cpmcp are required to rebuild drive A; native disks were not changed")
        cpm = build_images.build_banked_cpm_system(assembler)
        bios = build_images.build_bios(assembler)
        image = build_images.build_native_drive(image, cpm, bios)
        image = build_images.install_autorun(image, build_images.build_autorun(assembler), cpmcp)
        converter.validate_filesystems({image_name: image})
    target.write_bytes(image)
    print(f"Installed {args.drive}: {target}. Flashing is a separate operation.")


if __name__ == "__main__":
    main()
