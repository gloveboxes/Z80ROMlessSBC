"""Verify emulator and flash disks through Apple Container without hardware."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "src/disks"))
from src.cpm import build_images
sys.path.insert(0, str(ROOT / "scripts"))
import z80_container as launcher

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
            session = root / "session"
            with mock.patch.object(launcher, "SESSION", session):
                launcher.prepare()
            for image, session_name in launcher.DISKS:
                self.assertEqual((session / session_name).read_bytes(), (output / image).read_bytes())
            self.assertEqual((session / "z80boot.pkg").read_bytes(), package)
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


if __name__ == "__main__":
    unittest.main()
