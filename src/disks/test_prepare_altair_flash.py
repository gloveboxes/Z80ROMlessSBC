from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import stage_altair_disks as converter
from test_stage_altair_disks import filesystem_disk
from src.cpm import build_images

ROOT = Path(__file__).resolve().parents[2]
DISK_DIR = ROOT / "src/disks"


class PrepareAltairFlashTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.staged = self.root / "staged"
        self.output = self.root / "output"
        self.source.mkdir()
        for source_name, _ in converter.DISKS.values():
            (self.source / source_name).write_bytes(filesystem_disk())
        subprocess.run(
            [sys.executable, str(DISK_DIR / "stage_altair_disks.py"),
             "--source-dir", str(self.source), "--output-dir", str(self.staged)],
            check=True, capture_output=True, text=True,
        )

    def prepare(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(DISK_DIR / "prepare_altair_flash.py"),
             "--source-dir", str(self.source), "--staged-dir", str(self.staged),
             "--output-dir", str(self.output)],
            capture_output=True, text=True,
        )

    def test_build_uses_all_staged_images(self) -> None:
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        for drive in "BCD":
            name = converter.DISKS[drive][1]
            self.assertEqual((self.output / name).read_bytes(), (self.staged / name).read_bytes())
        self.assertTrue((self.output / "drive_a_cpm63k-z80.img").is_file())
        self.assertTrue((self.output / "drive_b_bdsc.img").is_file())
        self.assertTrue((self.output / "z80boot.pkg").is_file())
        boot = (self.output / "z80boot.img").read_bytes()
        image = (self.output / "drive_a_cpm63k-z80.img").read_bytes()
        self.assertEqual(image[:build_images.BIOS_BASE - build_images.CCP_BASE],
                         boot[build_images.CCP_BASE:build_images.BIOS_BASE])
        self.assertEqual(image[build_images.BIOS_BASE - build_images.CCP_BASE:
                               build_images.BIOS_BASE - build_images.CCP_BASE + 1024],
                         boot[build_images.BIOS_BASE:])
        self.assertEqual((self.output / "z80boot.pkg").read_bytes(),
                         build_images.build_boot_package(boot))

    def test_stale_clone_does_not_build_or_overwrite_existing_output(self) -> None:
        self.output.mkdir()
        marker = self.output / "drive_c_escape.img"
        marker.write_bytes(b"existing")
        clone = self.source / "drive_c_escape.img"
        clone.write_bytes(clone.read_bytes() + b"changed")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("run the staging task again", result.stderr)
        self.assertEqual(marker.read_bytes(), b"existing")
        self.assertFalse((self.output / "z80boot.pkg").exists())

    def test_all_disk_task_uses_prepared_outputs_and_confirmation(self) -> None:
        tasks = json.loads((ROOT / ".vscode/tasks.json").read_text())
        task = next(item for item in tasks["tasks"] if item["label"] == "Flash Pico - All disks (A-D)")
        command = task["command"]
        self.assertIn("${input:altairAllDiskFlashSafety}", command)
        self.assertIn("python scripts/z80_container.py disk prepare && python scripts/pico_flash.py disks", command)
        for drive, name in zip("ABCD", (
            "drive_a_cpm63k-z80.img", "drive_b_bdsc.img",
            "drive_c_escape.img", "drive_d_blank.img",
        )):
            self.assertIn(f"--drive {drive}=build/cpm-disk-update/{name}", command)


if __name__ == "__main__":
    unittest.main()
