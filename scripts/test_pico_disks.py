from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import shlex
import subprocess
import struct
import tempfile
import unittest
from unittest.mock import patch

import pico_flash as flash
from pico_supervisor import FlashError


REPO_ROOT = Path(__file__).resolve().parents[1]
IMAGE_BYTES = flash.cpm_images.DISK_IMAGE_BYTES


class DiskFlashTaskTests(unittest.TestCase):
    def test_system_task_builds_matching_pair_before_flashing(self) -> None:
        tasks = json.loads((REPO_ROOT / ".vscode/tasks.json").read_text())["tasks"]
        task = next(t for t in tasks if t["label"] ==
                    "Build and flash Pico - CP/M system and Drive A")
        stages = [shlex.split(stage.strip()) for stage in task["command"].split("&&")]
        self.assertEqual(stages[-2], [
            "python", "src/cpm/build_images.py", "--disk-dir",
            "build/cpm-system-update/converted", "--output-dir", "build/cpm-system-update",
        ])
        self.assertEqual(stages[-1], [
            "python", "scripts/pico_flash.py", "system", "--boot-package",
            "build/cpm-system-update/z80boot.pkg", "--drive-a",
            "build/cpm-system-update/drive_a_cpm63k-z80.img", "--power-confirmed", "--force",
        ])

    def test_disk_tasks_use_current_source_disks(self) -> None:
        tasks = json.loads((REPO_ROOT / ".vscode/tasks.json").read_text())["tasks"]
        source_images = {
            "A": "build/cpm-disk-update/drive_a_cpm63k-z80.img",
            "B": "src/disks/source-altair/bdsc-v1.60.dsk",
            "C": "src/disks/source-altair/escape-posix.dsk",
            "D": "src/disks/source-altair/blank.dsk",
        }
        labels = ["Flash Pico - All disks (A-D)", *(
            f"Flash Pico - Drive {drive}" for drive in "ABCD"
        )]
        for label in labels:
            with self.subTest(task=label):
                task = next(task for task in tasks if task["label"] == label)
                command = task["command"]
                stages = [shlex.split(stage.strip()) for stage in command.split("&&")]
                flash_index = next(
                    index for index, stage in enumerate(stages)
                    if stage[:3] == ["python", "scripts/pico_flash.py", "disks"]
                )
                arguments = stages[flash_index]
                assignments = [
                    arguments[index + 1]
                    for index, argument in enumerate(arguments) if argument == "--drive"
                ]
                drives = "ABCD" if label == labels[0] else label[-1]
                self.assertEqual(
                    assignments, [f"{drive}={source_images[drive]}" for drive in drives]
                )
                self.assertEqual(flash_index, len(stages) - 1)
                self.assertEqual(command.count("scripts/pico_flash.py disks"), 1)
                if "A" in drives:
                    self.assertEqual(stages[flash_index - 2], [
                        "python", "src/disks/convert_altair_disks.py",
                        "--output-dir", "build/cpm-disk-update/converted",
                    ])
                    self.assertEqual(stages[flash_index - 1], [
                        "python", "src/cpm/build_images.py",
                        "--disk-dir", "build/cpm-disk-update/converted",
                        "--output-dir", "build/cpm-disk-update",
                    ])


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
        system = (
            system[:flash.cpm_images.BIOS_DIRECTORY_BUFFER - flash.cpm_images.CCP_BASE]
            + bytes(128)
        )
        bios = bytearray(256)
        bios[:3] = b"\xc3\x00\xfd"
        for drive in range(4):
            struct.pack_into("<8H", bios, 0x20 + drive * 16,
                             0, 0, 0, 0, flash.cpm_images.BIOS_DIRECTORY_BUFFER,
                             0xFD60, 0, 0xFD80 + drive * 32)
        struct.pack_into("<HBBBHHBBHH", bios, 0x60,
                         32, 4, 15, 0, 251, 63, 0x80, 0, 0, 2)
        bios = bytes(bios)
        memory = flash.cpm_images.build_z80_image(system, bios)
        package = flash.cpm_images.build_boot_package(memory)
        native = flash.cpm_images.build_native_drive(self.data, system, bios)
        return package, native

    def test_force_skips_firmware_handshake_but_checks_journal(self) -> None:
        with (
            patch.object(flash, "resolve_device", return_value=("PORT", "TEST")),
            patch.object(flash, "disconnect_picocom"),
            patch.object(flash, "require_free_port"),
            patch.object(flash, "open_supervisor") as connection,
            patch.object(flash, "picotool", return_value=flash.STORAGE_LAYOUT_FEATURE) as picotool,
            patch.object(flash, "check_journal") as journal,
        ):
            self.assertEqual(flash.prepare_storage(self.arguments(force=True)), "TEST")
        connection.assert_not_called()
        picotool.assert_called_once_with(["info", "-F"], "TEST")
        journal.assert_called_once_with("TEST")

    def test_every_selected_drive_rejects_unknown_or_old_firmware_layout(self) -> None:
        for drive in "ABCD":
            for info in ("features: USB stdin / stdout", "z80sbc-storage:old",
                         flash.STORAGE_LAYOUT_FEATURE + ":extra", "wrong-" + flash.STORAGE_LAYOUT_FEATURE):
                with (
                    self.subTest(drive=drive, info=info),
                    patch.object(flash, "prepare_target", return_value="TEST"),
                    patch.object(flash, "picotool", return_value=info) as tool,
                    patch.object(flash, "check_journal") as journal,
                ):
                    with self.assertRaisesRegex(FlashError, "no storage writes"):
                        flash.flash_disks(self.arguments(drive=[(drive, self.source)], force=True))
                self.assertEqual([call.args[0][0] for call in tool.call_args_list], ["info"])
                journal.assert_not_called()

    def test_system_update_rejects_old_firmware_before_any_storage_write(self) -> None:
        args = self.system_arguments()
        with (
            patch.object(flash, "prepare_target", return_value="TEST"),
            patch.object(flash, "picotool", return_value="old firmware") as tool,
            patch.object(flash, "check_journal") as journal,
        ):
            with self.assertRaisesRegex(FlashError, "no storage writes"):
                flash.flash_system(args)
        tool.assert_called_once_with(["info", "-F"], "TEST")
        journal.assert_not_called()

    def test_old_geometry_and_overlapping_allocation_maps_are_rejected(self) -> None:
        package, native = self.boot_package()
        offset = flash.cpm_images.BOOT_PAYLOAD_OFFSET
        bios = flash.cpm_images.BIOS_BASE
        for fault in ("old-dsm", "old-reservation", "overlap", "undersized", "unreferenced-dpb"):
            for target in ("package", "drive"):
                with self.subTest(fault=fault, target=target):
                    memory = bytearray(package[offset:])
                    drive = bytearray(native)
                    buffer = memory if target == "package" else drive
                    base = bios if target == "package" else flash.cpm_images.CPM_SYSTEM_BYTES
                    if fault == "old-dsm":
                        struct.pack_into("<H", buffer, base + 0x65, 155)
                    elif fault == "old-reservation":
                        buffer[base + 0x69] = 0xc0
                    elif fault == "overlap":
                        struct.pack_into("<H", buffer, base + 0x20 + 16 + 14, 0xFD80)
                    elif fault == "undersized":
                        struct.pack_into("<H", buffer, base + 0x20 + 3 * 16 + 14, 0xFFF0)
                    else:
                        struct.pack_into("<H", buffer, base + 0x20 + 10, 0xFD50)
                    changed_package = flash.cpm_images.build_boot_package(bytes(memory))
                    with self.assertRaisesRegex(FlashError, "geometry/allocation maps"):
                        flash.validate_boot_pair(changed_package, bytes(drive))

    def system_arguments(self, **overrides: object) -> argparse.Namespace:
        package, native = self.boot_package()
        boot = self.directory / "z80boot.pkg"
        boot.write_bytes(package)
        self.source.write_bytes(native)
        return self.arguments(boot_package=boot, drive_a=self.source, **overrides)

    def test_system_update_writes_only_matching_boot_and_drive_a_snapshots(self) -> None:
        args = self.system_arguments()
        expected = [args.boot_package.read_bytes(), self.source.read_bytes()]
        writes = []

        def prepare(args: argparse.Namespace) -> str:
            args.boot_package.write_bytes(b"changed")
            args.drive_a.write_bytes(b"changed")
            return "TEST"

        def tool(arguments: list[str], device: str) -> None:
            if arguments[0] == "load":
                self.assertEqual(arguments[1], "-v")
                writes.append((int(arguments[6], 16), Path(arguments[2]).read_bytes()))

        with (
            patch.object(flash, "prepare_storage", side_effect=prepare),
            patch.object(flash, "picotool", side_effect=tool) as run,
        ):
            flash.flash_system(args)
        self.assertEqual(writes, list(zip((0x101E0000, 0x10200000), expected)))
        self.assertLess(writes[0][0] + len(writes[0][1]),
                        flash.FLASH_BASE + flash.cpm_images.FLASH_ENV_OFFSET)
        self.assertEqual(run.call_args.args, (["reboot"], "TEST"))

    def test_system_rejects_damaged_or_mismatched_inputs_before_device_access(self) -> None:
        for damage in ("header", "payload", "size", "drive"):
            with self.subTest(damage=damage):
                args = self.system_arguments()
                package = bytearray(args.boot_package.read_bytes())
                if damage == "header":
                    package[0] ^= 1
                elif damage == "payload":
                    package[-1] ^= 1
                elif damage == "size":
                    package = package[:-1]
                else:
                    args.drive_a.write_bytes(self.data)
                args.boot_package.write_bytes(package)
                with (
                    patch.object(flash, "prepare_storage") as prepare,
                    patch.object(flash, "picotool") as run,
                ):
                    with self.assertRaises(FlashError):
                        flash.flash_system(args)
                prepare.assert_not_called()
                run.assert_not_called()

    def test_system_pending_journal_prevents_writes(self) -> None:
        args = self.system_arguments()
        with (
            patch.object(flash, "prepare_target", return_value="TEST"),
            patch.object(flash, "check_journal", side_effect=FlashError("pending journal")),
            patch.object(flash, "picotool", return_value=flash.STORAGE_LAYOUT_FEATURE) as run,
        ):
            with self.assertRaises(FlashError):
                flash.flash_system(args)
        run.assert_called_once_with(["info", "-F"], "TEST")

    def test_system_write_failure_prevents_further_writes_and_reboot(self) -> None:
        for fail_at in (1, 2):
            args = self.system_arguments()
            calls = []

            def tool(arguments: list[str], device: str) -> None:
                self.assertEqual(arguments[0], "load")
                calls.append(arguments)
                if len(calls) == fail_at:
                    raise FlashError("verification failed")

            with (
                self.subTest(fail_at=fail_at),
                patch.object(flash, "prepare_storage", return_value="TEST"),
                patch.object(flash, "picotool", side_effect=tool),
            ):
                with self.assertRaises(FlashError):
                    flash.flash_system(args)
            self.assertEqual(len(calls), fail_at)

    def test_system_cli_dispatch_and_no_reboot(self) -> None:
        args = self.system_arguments(no_reboot=True)
        with (
            patch.object(flash, "prepare_storage", return_value="TEST"),
            patch.object(flash, "picotool") as run,
        ):
            flash.flash_system(args)
        self.assertEqual([c.args[0][0] for c in run.call_args_list], ["load", "load"])
        with (
            patch.object(flash.sys, "argv", [
                "pico_flash.py", "system", "--boot-package", str(args.boot_package),
                "--drive-a", str(args.drive_a), "--power-confirmed",
            ]),
            patch.object(flash, "flash_system") as system,
        ):
            self.assertEqual(flash.main(), 0)
        self.assertFalse(system.call_args.args[0].force)

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
        expected = flash.convert(source.read_bytes())
        self.assertEqual(flash.load_disk_image("B", source), expected)
        grid = source.read_bytes()[:flash.SOURCE_GRID_BYTES]
        marked = self.directory / "altair-with-marker.img"
        marked.write_bytes(grid + bytes(96))
        self.assertEqual(flash.load_disk_image("D", marked), expected)

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
            if arguments[0] == "info":
                return flash.STORAGE_LAYOUT_FEATURE
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
        self.assertEqual(writes, [(0x10280000, self.data), (0x10300000, self.data)])
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
            (0x10200000, IMAGE_BYTES), (0x10280000, IMAGE_BYTES),
            (0x10300000, IMAGE_BYTES), (0x10380000, IMAGE_BYTES),
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
            patch.object(flash, "picotool", return_value=flash.STORAGE_LAYOUT_FEATURE) as tool,
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
            flash.picotool(["load", "-v", "disk.img", "-t", "bin", "-o", "0x10280000"], "BOARD")
        self.assertEqual(run.call_args.args[0], [
            "picotool", "load", "-v", "disk.img", "-t", "bin", "-o", "0x10280000",
            "--ser", "BOARD",
        ])


if __name__ == "__main__":
    unittest.main()
