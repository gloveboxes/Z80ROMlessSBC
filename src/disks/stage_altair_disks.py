#!/usr/bin/env python3
"""Validate and stage cloned 512 KiB Altair application disks."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from geometry import IMAGE_BYTES, RECORD_BYTES, SECTORS_PER_TRACK, TRACKS

DISK_DIR = Path(__file__).resolve().parent
REPO_ROOT = DISK_DIR.parents[1]
sys.path.insert(0, str(REPO_ROOT))
from src.cpm.build_images import cpm_diskdefs

DISKS = {
    "A": ("drive_a_cpm63k.img", "drive_a_cpm63k.img"),
    "B": ("drive_b_bdsc.img", "drive_b_bdsc.img"),
    "C": ("drive_c_escape.img", "drive_c_escape.img"),
    "D": ("drive_d_blank.img", "drive_d_blank.img"),
}


def validate_source(source: bytes, path: Path) -> None:
    if len(source) != IMAGE_BYTES:
        raise ValueError(f"{path}: expected {IMAGE_BYTES} bytes, got {len(source)}")


def validate_filesystems(images: dict[str, bytes]) -> None:
    fsck = shutil.which("fsck.cpm")
    if fsck is None:
        raise SystemExit("fsck.cpm was not found; install cpmtools to validate imported disks")
    with tempfile.TemporaryDirectory(prefix="z80sbc-altair-check-") as directory:
        temporary = Path(directory)
        diskdefs = temporary / "diskdefs"
        diskdefs.write_text(cpm_diskdefs(), encoding="ascii")
        environment = {**os.environ, "DISKDEFS": str(diskdefs)}
        for name, output in images.items():
            image = temporary / name
            image.write_bytes(output)
            result = subprocess.run(
                [fsck, "-f", "z80sbc", "-n", str(image)],
                cwd=temporary, env=environment, capture_output=True, text=True,
            )
            if result.returncode:
                details = (result.stdout + result.stderr).strip()
                raise SystemExit(
                    f"filesystem validation failed for {name} "
                    f"(fsck.cpm exit {result.returncode}):\n{details}\n"
                    "No images were updated."
                )
            print(f"filesystem validated: {name}")


def load_staged(
    drives: list[str], source_dir: Path, staged_dir: Path,
) -> dict[str, bytes]:
    manifest = json.loads((staged_dir / "manifest.json").read_text(encoding="utf-8"))
    images: dict[str, bytes] = {}
    for drive in drives:
        source_name, image_name = DISKS[drive]
        source = (source_dir / source_name).read_bytes()
        image = (staged_dir / image_name).read_bytes()
        entry = manifest.get("images", {}).get(image_name)
        if not isinstance(entry, dict) or (
            entry.get("drive") != drive
            or entry.get("source") != source_name
            or entry.get("source_sha256") != hashlib.sha256(source).hexdigest()
            or entry.get("output_sha256") != hashlib.sha256(image).hexdigest()
            or len(image) != IMAGE_BYTES
            or source != image
        ):
            raise SystemExit(
                f"{image_name} does not match the current clone and import manifest; "
                "run the staging task again. No images were updated."
            )
        images[image_name] = image
    validate_filesystems(images)
    return images


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=DISK_DIR / "source-altair")
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / "build" / "altair-disks")
    parser.add_argument(
        "--drive", choices=DISKS, action="append",
        help="drive to stage; repeat for multiple drives (default: all four)",
    )
    args = parser.parse_args()
    drives = args.drive or list(DISKS)
    if len(set(drives)) != len(drives):
        parser.error("each drive can be specified only once")
    staged: dict[str, bytes] = {}
    images: dict[str, dict[str, object]] = {}
    for drive in drives:
        source_name, output_name = DISKS[drive]
        source_path = args.source_dir / source_name
        source = source_path.read_bytes()
        validate_source(source, source_path)
        output = source
        staged[output_name] = output
        images[output_name] = {
            "drive": drive,
            "source": source_name,
            "source_bytes": len(source),
            "source_sha256": hashlib.sha256(source).hexdigest(),
            "output_bytes": len(output),
            "output_sha256": hashlib.sha256(output).hexdigest(),
        }

    validate_filesystems(staged)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, output in staged.items():
        path = args.output_dir / name
        path.write_bytes(output)
        print(f"wrote {path}")
    manifest = {
        "format": f"{TRACKS} tracks x {SECTORS_PER_TRACK} records x {RECORD_BYTES} bytes",
        "image_bytes": IMAGE_BYTES,
        "images": images,
    }
    manifest_path = args.output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {manifest_path}")


if __name__ == "__main__":
    main()
