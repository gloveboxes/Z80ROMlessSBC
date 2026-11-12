from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import stage_altair_disks as staging
from geometry import IMAGE_BYTES
from src.cpm.build_images import SYSTEM_BYTES

DISK_DIR = Path(__file__).resolve().parent


def filesystem_disk(duplicate: bool = False) -> bytes:
    boot = (DISK_DIR / "native/drive_a_cpm63k.img").read_bytes()[:SYSTEM_BYTES]
    image = bytearray(boot + bytes([0xE5]) * (IMAGE_BYTES - SYSTEM_BYTES))
    if duplicate:
        entry = bytearray(32)
        entry[1:12] = b"TAIL    C  "
        entry[15] = 1
        entry[16] = 1
        image[SYSTEM_BYTES:SYSTEM_BYTES + 32] = entry
        image[SYSTEM_BYTES + 32:SYSTEM_BYTES + 64] = entry
    return bytes(image)


class StageAltairDisksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.output = self.root / "output"
        self.source.mkdir()

    def stage(self, *drives: str, env: dict | None = None) -> subprocess.CompletedProcess:
        command = [sys.executable, str(DISK_DIR / "stage_altair_disks.py"),
                   "--source-dir", str(self.source), "--output-dir", str(self.output)]
        for drive in drives:
            command.extend(["--drive", drive])
        return subprocess.run(command, env=env, capture_output=True, text=True)

    def test_selected_disk_is_byte_identical_and_hashed(self) -> None:
        image = filesystem_disk()
        (self.source / "drive_b_bdsc.img").write_bytes(image)
        result = self.stage("B")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / "drive_b_bdsc.img").read_bytes(), image)
        entry = json.loads((self.output / "manifest.json").read_text())["images"]["drive_b_bdsc.img"]
        self.assertEqual(entry["source_sha256"], hashlib.sha256(image).hexdigest())
        self.assertEqual(entry["output_sha256"], entry["source_sha256"])
        self.assertEqual(entry["drive"], "B")

    def test_bad_size_and_filesystem_preserve_existing_output(self) -> None:
        (self.source / "drive_a_cpm63k.img").write_bytes(filesystem_disk())
        bad = self.source / "drive_b_bdsc.img"
        self.output.mkdir()
        marker = self.output / "manifest.json"
        marker.write_bytes(b"existing")
        for image, reason in ((b"invalid", "expected 524288 bytes"),
                              (filesystem_disk(duplicate=True), "Duplicate extent")):
            bad.write_bytes(image)
            result = self.stage("A", "B")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(reason, result.stderr)
            self.assertEqual(marker.read_bytes(), b"existing")
            self.assertFalse((self.output / "drive_a_cpm63k.img").exists())

    def test_duplicate_drive_and_missing_fsck_fail(self) -> None:
        self.assertIn("only once", self.stage("D", "D").stderr)
        (self.source / "drive_b_bdsc.img").write_bytes(filesystem_disk())
        result = self.stage("B", env={**os.environ, "PATH": ""})
        self.assertIn("fsck.cpm was not found", result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
