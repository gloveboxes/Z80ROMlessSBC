from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import pico_flash as flash
from pico_supervisor import FlashError


REPO_ROOT = Path(__file__).resolve().parents[1]
IMAGE_BYTES = flash.cpm_images.DISK_IMAGE_BYTES


class DiskFlashTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.output = io.StringIO()
        stdout = redirect_stdout(self.output)
        stdout.__enter__()
        self.addCleanup(stdout.__exit__, None, None, None)
        self.source = self.directory / "native.img"
        self.data = b"\xe5" * IMAGE_BYTES
        self.source.write_bytes(self.data)

    def arguments(self, **overrides: object) -> argparse.Namespace:
        values = dict(
            drive=[("B", self.source)], serial="TEST", port=None,
            stopped=False, power_confirmed=True, bootsel=False,
            no_disconnect=False, no_reboot=False, timeout=1,
        )
        values.update(overrides)
        return argparse.Namespace(**values)

    def boot_package(self) -> tuple[bytes, bytes]:
        system = bytes(index & 255 for index in range(flash.cpm_images.CPM_SYSTEM_BYTES))
        bios = b"\xc3\x00\xfd"
        memory = flash.cpm_images.build_z80_image(system, bios)
        package = flash.cpm_images.build_boot_package(memory)
        native = flash.cpm_images.build_native_drive(self.data, system, bios)
        return package, native

    def test_drive_mapping_parser(self) -> None:
        self.assertEqual(flash.drive_image("b=some path/disk=1.dsk"), (
            "B", Path("some path/disk=1.dsk"),
        ))
        for value in ("", "B", "B=", "=disk.img", "E=disk.img", "AB=disk.img"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                flash.drive_image(value)

    def test_native_exact_geometry_not_extension(self) -> None:
        source = self.directory / "native.dsk"
        source.write_bytes(self.data)
        self.assertEqual(flash.load_disk_image("B", source), self.data)
        for size in (0, IMAGE_BYTES - 1, IMAGE_BYTES + 1, 4 * 1024 * 1024):
            source.write_bytes(bytes(size))
            with self.subTest(size=size), self.assertRaises(FlashError):
                flash.load_disk_image("B", source)

    def test_altair_sources_use_maintained_conversion(self) -> None:
        source = REPO_ROOT / "src/disks/source-altair/bdsc-v1.60.dsk"
        expected = REPO_ROOT / "src/disks/generated/drive_b_bdsc.img"
        self.assertEqual(flash.load_disk_image("B", source), expected.read_bytes())
        grid = source.read_bytes()[:flash.SOURCE_GRID_BYTES]
        marked = self.directory / "altair-with-marker.img"
        marked.write_bytes(grid + bytes(96))
        self.assertEqual(flash.load_disk_image("D", marked), expected.read_bytes())

    def test_corrupt_altair_and_framed_drive_a_fail_before_hardware(self) -> None:
        source = REPO_ROOT / "src/disks/source-altair/bdsc-v1.60.dsk"
        with self.assertRaisesRegex(FlashError, "board-native"):
            flash.load_disk_image("A", source)
        corrupted = bytearray(source.read_bytes())
        corrupted[131] ^= 1
        self.source.write_bytes(corrupted)
        with patch.object(flash, "prepare_storage") as prepare:
            with self.assertRaisesRegex(FlashError, "framing/checksum"):
                flash.flash_disks(self.arguments())
        prepare.assert_not_called()

    def test_all_inputs_validated_before_disconnect_or_stop(self) -> None:
        bad = self.directory / "bad.img"
        bad.write_bytes(b"bad")
        for mappings in (
            [], [("E", self.source)], [("B", self.source), ("B", self.source)],
            [("B", self.source), ("C", bad)],
        ):
            with (
                self.subTest(mappings=mappings),
                patch.object(flash, "prepare_storage") as prepare,
                patch.object(flash, "picotool") as tool,
            ):
                with self.assertRaises(FlashError):
                    flash.flash_disks(self.arguments(drive=mappings))
            prepare.assert_not_called()
            tool.assert_not_called()

    def test_selected_writes_journal_first_no_backups_and_one_reboot(self) -> None:
        events: list[str] = []
        reads: list[tuple[int, int]] = []
        writes: list[tuple[int, bytes]] = []
        paths: list[Path] = []

        def tool(arguments: list[str], device: str) -> None:
            self.assertEqual(device, "TEST")
            events.append(arguments[0])
            if arguments[0] == "save":
                start, end = int(arguments[2], 16), int(arguments[3], 16)
                reads.append((start, end))
                Path(arguments[4]).write_bytes(b"\xff" * (end - start))
            elif arguments[0] == "load":
                self.assertEqual(arguments[1], "-v")
                self.assertEqual(arguments[3:6], ["-t", "bin", "-o"])
                self.assertNotIn("-F", arguments)
                path = Path(arguments[2])
                paths.append(path)
                writes.append((int(arguments[6], 16), path.read_bytes()))

        with (
            patch.object(flash, "prepare_target", return_value="TEST") as prepare,
            patch.object(flash, "picotool", side_effect=tool),
        ):
            args = self.arguments(drive=[("B", self.source), ("C", self.source)])
            flash.flash_disks(args)
        prepare.assert_called_once_with(args)
        self.assertEqual(events, ["info", *(["save"] * 8), "load", "load", "reboot"])
        self.assertEqual(reads, [
            (flash.JOURNAL_BASE + pair * 8192, flash.JOURNAL_BASE + pair * 8192 + 256)
            for pair in range(8)
        ])
        self.assertEqual(writes, [(0x10310000, self.data), (0x10360000, self.data)])
        self.assertTrue(all(not path.exists() for path in paths))
        self.assertEqual(self.source.read_bytes(), self.data)
        self.assertIn("No backups", self.output.getvalue())

    def test_all_four_slot_ranges_are_exact(self) -> None:
        writes: list[tuple[int, int]] = []

        def tool(arguments: list[str], device: str) -> None:
            if arguments[0] == "load":
                writes.append((int(arguments[6], 16), Path(arguments[2]).stat().st_size))

        with (
            patch.object(flash, "prepare_storage", return_value="TEST"),
            patch.object(flash, "check_drive_a") as check_a,
            patch.object(flash, "picotool", side_effect=tool),
        ):
            flash.flash_disks(self.arguments(drive=[(drive, self.source) for drive in "ABCD"]))
        check_a.assert_called_once()
        self.assertEqual(writes, [
            (0x102C0000, IMAGE_BYTES), (0x10310000, IMAGE_BYTES),
            (0x10360000, IMAGE_BYTES), (0x103B0000, IMAGE_BYTES),
        ])
        self.assertEqual(writes[-1][0] + writes[-1][1], 0x10400000)
        for offset in flash.cpm_images.FLASH_DISK_OFFSETS:
            self.assertIn(flash.FLASH_BASE + offset, flash.DISK_ADDRESSES.values())

    def test_snapshots_prevent_source_changes_during_programming(self) -> None:
        def prepare(args: argparse.Namespace) -> str:
            self.source.write_bytes(bytes(IMAGE_BYTES))
            return "TEST"

        def tool(arguments: list[str], device: str) -> None:
            if arguments[0] == "load":
                self.assertEqual(Path(arguments[2]).read_bytes(), self.data)

        with (
            patch.object(flash, "prepare_storage", side_effect=prepare),
            patch.object(flash, "picotool", side_effect=tool),
        ):
            flash.flash_disks(self.arguments())

    def test_pending_journal_prevents_every_write(self) -> None:
        with (
            patch.object(flash, "prepare_target", return_value="TEST"),
            patch.object(flash, "check_journal", side_effect=FlashError("pending journal")),
            patch.object(flash, "picotool") as tool,
        ):
            with self.assertRaises(FlashError):
                flash.flash_disks(self.arguments())
        tool.assert_called_once_with(["info", "-F"], "TEST")

    def test_drive_a_compatibility_checked_before_any_selected_write(self) -> None:
        with (
            patch.object(flash, "prepare_storage", return_value="TEST"),
            patch.object(flash, "check_drive_a", side_effect=FlashError("incompatible")),
            patch.object(flash, "picotool") as tool,
        ):
            with self.assertRaises(FlashError):
                flash.flash_disks(self.arguments(drive=[("B", self.source), ("A", self.source)]))
        tool.assert_not_called()

    def test_write_failure_does_not_continue_or_reboot(self) -> None:
        for fail_at in (1, 2):
            paths: list[Path] = []

            def tool(arguments: list[str], device: str) -> None:
                self.assertEqual(arguments[0], "load")
                paths.append(Path(arguments[2]))
                if len(paths) == fail_at:
                    raise FlashError("write/verification failed")

            with (
                self.subTest(fail_at=fail_at),
                patch.object(flash, "prepare_storage", return_value="TEST"),
                patch.object(flash, "picotool", side_effect=tool) as run,
            ):
                with self.assertRaises(FlashError):
                    flash.flash_disks(
                        self.arguments(drive=[(drive, self.source) for drive in "BCD"])
                    )
            self.assertEqual(run.call_count, fail_at)
            self.assertTrue(all(not path.exists() for path in paths))

    def test_no_reboot_keeps_bootsel(self) -> None:
        with (
            patch.object(flash, "prepare_storage", return_value="TEST"),
            patch.object(flash, "picotool") as tool,
        ):
            flash.flash_disks(self.arguments(no_reboot=True))
        self.assertEqual([call.args[0][0] for call in tool.call_args_list], ["load"])
        self.assertIn("Device left in BOOTSEL", self.output.getvalue())

    def test_drive_a_matches_installed_warm_boot_records(self) -> None:
        package, native = self.boot_package()

        def read(arguments: list[str], device: str) -> None:
            self.assertEqual(arguments[0], "save")
            self.assertEqual(device, "TEST")
            self.assertEqual(int(arguments[3], 16) - int(arguments[2], 16), len(package))
            Path(arguments[4]).write_bytes(package)

        with patch.object(flash, "picotool", side_effect=read):
            flash.check_drive_a(native, "TEST", self.directory)
            with self.assertRaisesRegex(FlashError, "warm-boot records"):
                flash.check_drive_a(self.data, "TEST", self.directory)

    def test_drive_a_rejects_invalid_installed_boot_package(self) -> None:
        package, native = self.boot_package()
        corrupt_header = bytearray(package)
        corrupt_header[0] ^= 1
        corrupt_payload = bytearray(package)
        corrupt_payload[-1] ^= 1
        for damaged in (package[:-1], bytes(corrupt_header), bytes(corrupt_payload)):
            def read(arguments: list[str], device: str) -> None:
                Path(arguments[4]).write_bytes(damaged)

            with (
                self.subTest(size=len(damaged)),
                patch.object(flash, "picotool", side_effect=read),
            ):
                with self.assertRaises(FlashError):
                    flash.check_drive_a(native, "TEST", self.directory)

    def test_cli_multiple_disks_uses_safe_defaults(self) -> None:
        with (
            patch.object(flash.sys, "argv", [
                "pico_flash.py", "disks", "--drive", f"b={self.source}",
                "--drive", f"C={self.source}", "--power-confirmed",
            ]),
            patch.object(flash, "flash_disks") as disks,
        ):
            self.assertEqual(flash.main(), 0)
        args = disks.call_args.args[0]
        self.assertEqual(args.drive, [("B", self.source), ("C", self.source)])
        self.assertFalse(args.no_disconnect)
        self.assertFalse(args.stopped)
        self.assertFalse(args.no_reboot)
        self.assertIsNone(args.serial)
        self.assertIsNone(args.port)

    def test_cli_missing_drive_is_rejected(self) -> None:
        with (
            patch.object(flash.sys, "argv", ["pico_flash.py", "disks", "--power-confirmed"]),
            patch.object(flash, "prepare_target") as prepare,
            patch("sys.stderr", new=io.StringIO()),
        ):
            with self.assertRaises(SystemExit) as error:
                flash.main()
        self.assertEqual(error.exception.code, 2)
        prepare.assert_not_called()

    def test_picotool_serial_selection_applies_to_disk_load(self) -> None:
        with patch.object(
            flash.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 0, "OK\n", ""),
        ) as run:
            flash.picotool(["load", "-v", "disk.img", "-t", "bin", "-o", "0x10310000"], "BOARD")
        self.assertEqual(run.call_args.args[0], [
            "picotool", "load", "-v", "disk.img", "-t", "bin", "-o", "0x10310000",
            "--ser", "BOARD",
        ])


if __name__ == "__main__":
    unittest.main()
