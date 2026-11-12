"""Exercise disk import with disposable images through Apple Container."""

import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "src/disks"))
from src.cpm import build_images
from test_stage_altair_disks import filesystem_disk

DISKS = ("drive_a_cpm63k.img", "drive_b_bdsc.img",
         "drive_c_escape.img", "drive_d_blank.img")


class ContainerDiskIntegration(unittest.TestCase):
    def test_build_images_with_and_without_firmware(self):
        with tempfile.TemporaryDirectory(prefix="z80-image-container-test-") as directory:
            root = Path(directory)
            output = root / "output"
            firmware = root / "firmware.bin"
            firmware_bytes = bytes(range(256)) * 4
            firmware.write_bytes(firmware_bytes)
            source = ROOT / "src/disks/native"
            original = {name: hashlib.sha256((source / name).read_bytes()).digest()
                        for name in DISKS}

            def invoke(*options):
                result = subprocess.run(
                    [sys.executable, str(ROOT / "scripts/z80_container.py"), "images",
                     "--output-dir", str(output), *options],
                    cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            invoke()
            boot = (output / "z80boot.img").read_bytes()
            package = (output / "z80boot.pkg").read_bytes()
            drive_a = (output / "drive_a_cpm63k-z80.img").read_bytes()
            self.assertEqual(drive_a[:build_images.CPM_SYSTEM_BYTES],
                             boot[build_images.CCP_BASE:build_images.BIOS_BASE])
            self.assertEqual(package, build_images.build_boot_package(boot))
            for name in ("drive_b_bdsc.img", "drive_c_escape.img", "drive_d_blank.img"):
                self.assertEqual((output / name).stat().st_size, build_images.DISK_IMAGE_BYTES)
            for name in DISKS[1:]:
                self.assertEqual((output / name).read_bytes(), (source / name).read_bytes())
            invoke("--firmware", str(firmware))
            complete = (output / "z80romless-flash.bin").read_bytes()
            self.assertEqual(len(complete), build_images.FLASH_BYTES)
            self.assertEqual(complete[:len(firmware_bytes)], firmware_bytes)
            offset = build_images.FLASH_BOOT_OFFSET
            self.assertEqual(complete[offset:offset + len(package)], package)
            for offset, name in zip(build_images.FLASH_DISK_OFFSETS,
                                    ("drive_a_cpm63k-z80.img", *DISKS[1:])):
                self.assertEqual(complete[offset:offset + build_images.DISK_IMAGE_BYTES],
                                 (output / name).read_bytes())
            self.assertEqual(original, {name: hashlib.sha256((source / name).read_bytes()).digest()
                                        for name in DISKS})

    def test_import_prepares_matching_boot_and_preserves_failed_outputs(self):
        with tempfile.TemporaryDirectory(prefix="z80-disk-container-test-") as directory:
            root = Path(directory)
            source, staged, native, output = (root / name for name in
                                               ("source", "staged", "native", "output"))
            source.mkdir()
            native.mkdir()
            for name in DISKS:
                (source / name).write_bytes(filesystem_disk())
                (native / name).write_bytes((source / name).read_bytes())

            def invoke(operation, *options, succeeds=True):
                result = subprocess.run(
                    [sys.executable, str(ROOT / "scripts/z80_container.py"), "disk", operation,
                     "--source-dir", str(source), "--staged-dir", str(staged), *options],
                    cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
                return result

            invoke("stage")
            for name in DISKS:
                self.assertEqual((staged / name).read_bytes(), (source / name).read_bytes())
            manifest = json.loads((staged / "manifest.json").read_text())
            self.assertEqual(len(manifest["images"]), 4)
            invoke("prepare", "--output-dir", str(output))
            for name in DISKS[1:]:
                self.assertEqual((output / name).read_bytes(), (source / name).read_bytes())
            boot = (output / "z80boot.img").read_bytes()
            drive_a = (output / "drive_a_cpm63k-z80.img").read_bytes()
            self.assertEqual(drive_a[:build_images.CPM_SYSTEM_BYTES],
                             boot[build_images.CCP_BASE:build_images.BIOS_BASE])
            self.assertEqual((output / "z80boot.pkg").read_bytes(),
                             build_images.build_boot_package(boot))
            invoke("install", "--drive", "A", "--confirm", "Replace selected native disk",
                   "--native-dir", str(native))
            self.assertEqual((native / DISKS[0]).read_bytes(), drive_a)
            for name in DISKS[1:]:
                self.assertEqual((native / name).read_bytes(), (source / name).read_bytes())
            old_boot = (output / "z80boot.pkg").read_bytes()
            (source / DISKS[2]).write_bytes(b"invalid")
            failed = invoke("prepare", "--output-dir", str(output), succeeds=False)
            self.assertIn("run the staging task again", failed.stderr)
            self.assertEqual((output / "z80boot.pkg").read_bytes(), old_boot)
            failed = invoke("stage", succeeds=False)
            self.assertIn("expected 524288 bytes", failed.stderr)
            self.assertEqual((staged / DISKS[2]).stat().st_size, 524288)


if __name__ == "__main__":
    unittest.main()
