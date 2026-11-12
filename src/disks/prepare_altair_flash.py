#!/usr/bin/env python3
"""Build flash inputs from the validated staged Altair disks."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import stage_altair_disks as converter


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=converter.DISK_DIR / "source-altair")
    parser.add_argument("--staged-dir", type=Path, default=converter.REPO_ROOT / "build/altair-disks")
    parser.add_argument("--output-dir", type=Path, default=converter.REPO_ROOT / "build/cpm-disk-update")
    args = parser.parse_args()
    images = converter.load_staged(list(converter.DISKS), args.source_dir, args.staged_dir)
    with tempfile.TemporaryDirectory(prefix="z80sbc-altair-build-") as directory:
        root = Path(directory)
        snapshot = root / "source"
        snapshot.mkdir()
        built = root / "built"
        for name, image in images.items():
            (snapshot / name).write_bytes(image)
        subprocess.run(
            [sys.executable, str(converter.REPO_ROOT / "src/cpm/build_images.py"),
             "--disk-dir", str(snapshot), "--output-dir", str(built)],
            cwd=converter.REPO_ROOT, check=True,
        )
        outputs = {
            "drive_a_cpm63k-z80.img": (built / "drive_a_cpm63k-z80.img").read_bytes(),
            **{name: (built / name).read_bytes() for name in list(images)[1:]},
        }
        converter.validate_filesystems(outputs)
        for name in list(images)[1:]:
            if outputs[name] != images[name]:
                raise SystemExit(f"prepared {name} differs from its validated source")
        args.output_dir.mkdir(parents=True, exist_ok=True)
        for artifact in built.iterdir():
            if not artifact.is_file():
                raise SystemExit(f"unexpected build output: {artifact}")
            with tempfile.NamedTemporaryFile(dir=args.output_dir, delete=False) as temporary:
                temporary_path = Path(temporary.name)
            try:
                shutil.copyfile(artifact, temporary_path)
                os.replace(temporary_path, args.output_dir / artifact.name)
            finally:
                temporary_path.unlink(missing_ok=True)
    print("Prepared all flash disks from validated staged Altair images.")


if __name__ == "__main__":
    main()
