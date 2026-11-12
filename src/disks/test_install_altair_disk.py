from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_stage_altair_disks import filesystem_disk
from src.cpm import build_images

DISK_DIR = Path(__file__).resolve().parent


class InstallAltairDiskTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.staged = self.root / "staged"
        self.native = self.root / "native"
        self.source.mkdir()
        self.native.mkdir()
        (self.source / "drive_c_escape.img").write_bytes(filesystem_disk())
        for name in ("drive_b_bdsc.img", "drive_c_escape.img", "drive_d_blank.img"):
            (self.native / name).write_bytes(b"original " + name.encode())
        subprocess.run(
            [sys.executable, str(DISK_DIR / "stage_altair_disks.py"), "--drive", "C",
             "--source-dir", str(self.source), "--output-dir", str(self.staged)],
            check=True, capture_output=True, text=True,
        )

    def install(self, confirmation: str = "Replace selected native disk") -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(DISK_DIR / "install_altair_disk.py"), "C",
             "--confirm", confirmation, "--source-dir", str(self.source),
             "--staged-dir", str(self.staged), "--native-dir", str(self.native)],
            capture_output=True, text=True,
        )

    def test_selected_drive_only_is_installed(self) -> None:
        result = self.install()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.native / "drive_c_escape.img").read_bytes(),
                         (self.staged / "drive_c_escape.img").read_bytes())
        for name in ("drive_b_bdsc.img", "drive_d_blank.img"):
            self.assertEqual((self.native / name).read_bytes(), b"original " + name.encode())

    def test_cancel_and_stale_or_modified_images_do_not_replace_native(self) -> None:
        target = self.native / "drive_c_escape.img"
        original = target.read_bytes()
        self.assertNotEqual(self.install("Cancel").returncode, 0)
        self.assertEqual(target.read_bytes(), original)
        source = self.source / "drive_c_escape.img"
        source.write_bytes(source.read_bytes() + b"changed")
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("run the staging task again", result.stderr)
        self.assertEqual(target.read_bytes(), original)

    def test_matching_hashes_do_not_bypass_filesystem_validation(self) -> None:
        source = filesystem_disk(duplicate=True)
        (self.source / "drive_c_escape.img").write_bytes(source)
        import hashlib
        image = source
        (self.staged / "drive_c_escape.img").write_bytes(image)
        path = self.staged / "manifest.json"
        manifest = json.loads(path.read_text())
        entry = manifest["images"]["drive_c_escape.img"]
        entry["source_sha256"] = hashlib.sha256(source).hexdigest()
        entry["output_sha256"] = hashlib.sha256(image).hexdigest()
        path.write_text(json.dumps(manifest))
        target = self.native / "drive_c_escape.img"
        original = target.read_bytes()
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Duplicate extent", result.stderr)
        self.assertEqual(target.read_bytes(), original)

    def test_import_a_rebuilds_current_boot_system(self) -> None:
        image = filesystem_disk()
        (self.source / "drive_a_cpm63k.img").write_bytes(image)
        target = self.native / "drive_a_cpm63k.img"
        target.write_bytes(b"old")
        subprocess.run(
            [sys.executable, str(DISK_DIR / "stage_altair_disks.py"), "--drive", "A",
             "--source-dir", str(self.source), "--output-dir", str(self.staged)],
            check=True, capture_output=True, text=True,
        )
        result = subprocess.run(
            [sys.executable, str(DISK_DIR / "install_altair_disk.py"), "A",
             "--confirm", "Replace selected native disk", "--source-dir", str(self.source),
             "--staged-dir", str(self.staged), "--native-dir", str(self.native)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        imported = target.read_bytes()
        bios = build_images.build_bios("z80asm")
        cpm = build_images.build_banked_cpm_system("z80asm")
        self.assertEqual(imported[:len(cpm) + len(bios)], cpm + bios)
        self.assertEqual(len(imported), build_images.DISK_IMAGE_BYTES)


if __name__ == "__main__":
    unittest.main()
