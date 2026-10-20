from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import io
from pathlib import Path
import re
import signal
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import pico_flash as flash
import pico_supervisor as supervisor


STATUS = (
    "[diag] running=1 verified=1 clock=20000000 BIOS=1 CCP_handoff=1 "
    "disk=01 fatal=0 rx_drops=0 tx_drops=0 trap_timeouts=0 "
    "control_errors=0 boot_error=none failure=none\n"
)


def uf2(address: int = flash.FLASH_BASE, number: int = 0, total: int = 1) -> bytes:
    block = bytearray(512)
    struct.pack_into(
        "<8I", block, 0, 0x0A324655, 0x9E5D5157,
        0x2000, address, 256, number, total, flash.ARM_S_FAMILY,
    )
    struct.pack_into("<I", block, 508, 0x0AB16F30)
    return bytes(block)


class FlashTests(unittest.TestCase):
    def setUp(self) -> None:
        self.output = io.StringIO()
        self.stdout = redirect_stdout(self.output)
        self.stdout.__enter__()
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.stdout.__exit__, None, None, None)
        self.addCleanup(self.directory.cleanup)
        self.image = Path(self.directory.name) / "firmware.uf2"
        self.image.write_bytes(uf2())
        resolve = patch.object(
            flash, "resolve_device",
            side_effect=lambda port, device: (port or "/dev/cu.discovered", device or "TEST"),
        )
        resolve.start()
        self.addCleanup(resolve.stop)

    def arguments(self, **overrides: object) -> argparse.Namespace:
        values = dict(
            image=self.image, serial="TEST", port="/dev/cu.test",
            stopped=False, power_confirmed=True, bootsel=False,
            no_disconnect=False, no_reboot=False, timeout=1,
        )
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_firmware_bounds_and_format(self) -> None:
        flash.validate_firmware(self.image)
        for data in (
            b"", b"not uf2", uf2(flash.FIRMWARE_END),
            uf2(flash.FLASH_BASE + 256), uf2(number=1),
            uf2(total=2), uf2() + uf2(), uf2()[:-1],
        ):
            with self.subTest(data=data[:32]):
                self.image.write_bytes(data)
                with self.assertRaises(supervisor.FlashError):
                    flash.validate_firmware(self.image)

    def test_exact_firmware_limit(self) -> None:
        count = (flash.FIRMWARE_END - flash.FLASH_BASE) // 256
        image = b"".join(
            uf2(flash.FLASH_BASE + number * 256, number, count)
            for number in range(count)
        )
        self.image.write_bytes(image)
        flash.validate_firmware(self.image)
        self.image.write_bytes(image + uf2(flash.FIRMWARE_END, count, count + 1))
        with self.assertRaisesRegex(supervisor.FlashError, "outside firmware"):
            flash.validate_firmware(self.image)

    def test_layout_matches_maintained_firmware(self) -> None:
        header = (
            Path(__file__).resolve().parents[1]
            / "src/common/include/z80sbc/flash_layout.h"
        ).read_text()
        for name, value in (
            ("Z80_FLASH_LINK_LIMIT", flash.FIRMWARE_END - flash.FLASH_BASE),
            ("Z80_FLASH_JOURNAL_OFFSET", flash.JOURNAL_BASE - flash.FLASH_BASE),
            ("Z80_FLASH_DISK_OFFSET", flash.DISK_ADDRESSES["A"] - flash.FLASH_BASE),
            ("Z80_FLASH_DISK_SLOT_BYTES", flash.cpm_images.DISK_IMAGE_BYTES),
            ("Z80_FLASH_DISK_SLOT_COUNT", len(flash.DISK_ADDRESSES)),
        ):
            match = re.search(rf"\b{name} = (0x[0-9a-fA-F]+|[0-9]+)u", header)
            self.assertIsNotNone(match)
            assert match is not None
            self.assertEqual(int(match.group(1), 0), value)

    def test_sdk_ignore_block(self) -> None:
        block = bytearray(uf2())
        struct.pack_into("<6I", block, 8, 0xA000, 0x10FFFF00, 256, 0, 2, 0xE48BFF57)
        struct.pack_into("<I", block, 288, 0x9957E304)
        self.image.write_bytes(block + uf2())
        flash.validate_firmware(self.image)
        block[288] ^= 1
        self.image.write_bytes(block + uf2())
        with self.assertRaises(supervisor.FlashError):
            flash.validate_firmware(self.image)

    def test_defaults_disconnect_then_stop_then_verified_load(self) -> None:
        events: list[str] = []
        connection = MagicMock()
        with (
            patch.object(flash, "disconnect_picocom", side_effect=lambda port: events.append("disconnect")),
            patch.object(flash, "require_free_port"),
            patch.object(flash, "open_supervisor", return_value=connection),
            patch.object(flash.time, "sleep"),
            patch.object(flash, "stop", side_effect=lambda *args: events.append("stop")),
            patch.object(flash, "picotool", side_effect=lambda args, device: events.append(args[0])) as tool,
        ):
            flash.flash_firmware(self.arguments())
        self.assertEqual(events, ["disconnect", "stop", "load", "reboot"])
        tool.assert_any_call(["load", "-v", "-F", str(self.image)], "TEST")
        connection.__exit__.assert_called_once()

    def test_stop_failure_prevents_flash(self) -> None:
        with (
            patch.object(flash, "disconnect_picocom"),
            patch.object(flash, "require_free_port"),
            patch.object(flash, "open_supervisor", return_value=MagicMock()),
            patch.object(flash.time, "sleep"),
            patch.object(flash, "stop", side_effect=supervisor.FlashError("timeout")),
            patch.object(flash, "picotool") as tool,
        ):
            with self.assertRaises(supervisor.FlashError):
                flash.flash_firmware(self.arguments())
        tool.assert_not_called()

    def test_failed_verification_never_reboots(self) -> None:
        with (
            patch.object(flash, "require_free_port"),
            patch.object(flash, "disconnect_picocom"),
            patch.object(flash, "picotool", side_effect=supervisor.FlashError("verify failed")) as tool,
        ):
            with self.assertRaises(supervisor.FlashError):
                flash.flash_firmware(self.arguments(stopped=True))
        self.assertEqual(tool.call_count, 1)

    def test_opt_out_does_not_disconnect_busy_owner(self) -> None:
        with (
            patch.object(flash, "require_free_port", side_effect=supervisor.FlashError("busy")),
            patch.object(flash, "disconnect_picocom") as disconnect,
            patch.object(flash, "picotool") as tool,
        ):
            with self.assertRaises(supervisor.FlashError):
                flash.flash_firmware(self.arguments(no_disconnect=True))
        disconnect.assert_not_called()
        tool.assert_not_called()

    def test_non_cpm_firmware_never_opens_post_flash_serial(self) -> None:
        with (
            patch.object(flash, "require_free_port"),
            patch.object(flash, "disconnect_picocom"),
            patch.object(flash, "picotool") as tool,
            patch.object(flash, "open_supervisor") as open_port,
        ):
            flash.flash_firmware(self.arguments(stopped=True))
        self.assertEqual([call.args[0][0] for call in tool.call_args_list], ["load", "reboot"])
        open_port.assert_not_called()
        self.assertIn("flashing complete", self.output.getvalue())
        self.assertNotIn("CP/M", self.output.getvalue())

    def test_prepare_is_read_only_and_keeps_same_device(self) -> None:
        with (
            patch.object(flash.sys, "argv", ["pico_flash.py", "prepare", "--power-confirmed"]),
            patch.object(flash, "prepare_target", return_value="TEST"),
            patch.object(flash, "picotool") as tool,
            patch.object(flash, "check_journal") as journal,
        ):
            self.assertEqual(flash.main(), 0)
        tool.assert_called_once_with(["info", "-F"], "TEST")
        journal.assert_called_once_with("TEST")

    def test_confirmations_before_hardware(self) -> None:
        for options in (
            dict(power_confirmed=False), dict(bootsel=True),
            dict(bootsel=True, stopped=True, serial=None),
        ):
            with self.subTest(options=options), patch.object(flash, "picotool") as tool:
                with self.assertRaises(supervisor.FlashError):
                    flash.flash_firmware(self.arguments(**options))
                tool.assert_not_called()

    def test_bootsel_does_not_force_or_reboot_when_requested(self) -> None:
        with patch.object(flash, "picotool") as tool:
            flash.flash_firmware(
                self.arguments(bootsel=True, stopped=True, port=None, no_reboot=True)
            )
        tool.assert_called_once_with(["load", "-v", str(self.image)], "TEST")

    def test_picotool_selection_and_failure(self) -> None:
        with patch.object(
            flash.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 2, "verification failed", ""),
        ) as run:
            with self.assertRaises(supervisor.FlashError):
                flash.picotool(["load", "-v", "image.uf2"], "TEST")
        self.assertEqual(run.call_args.args[0][-2:], ["--ser", "TEST"])
        self.assertIn("verification failed", self.output.getvalue())

    def test_picotool_force_follows_serial_selection(self) -> None:
        with patch.object(
            flash.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 0, "OK\n", ""),
        ) as run:
            flash.picotool(["load", "-v", "-F", "image.uf2"], "TEST")
        self.assertEqual(
            run.call_args.args[0],
            ["picotool", "load", "-v", "image.uf2", "--ser", "TEST", "-F"],
        )

    def test_all_journal_headers_and_failure(self) -> None:
        def save(arguments: list[str], device: str) -> None:
            self.assertEqual(device, "TEST")
            self.assertEqual(arguments[0], "save")
            Path(arguments[4]).write_bytes(b"\xff" * 256)

        with patch.object(flash, "picotool", side_effect=save) as tool:
            flash.check_journal("TEST")
        self.assertEqual(tool.call_count, 8)
        self.assertEqual(tool.call_args.args[0][2], hex(flash.JOURNAL_BASE + 7 * 8192))

        for header in (b"\x00" * 256, b"\xff" * 255):
            def dirty(arguments: list[str], device: str) -> None:
                Path(arguments[4]).write_bytes(header)

            with patch.object(flash, "picotool", side_effect=dirty) as tool:
                with self.assertRaises(supervisor.FlashError):
                    flash.check_journal("TEST")
            self.assertEqual(tool.call_count, 1)


class SupervisorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.output = io.StringIO()
        stdout = redirect_stdout(self.output)
        stdout.__enter__()
        self.addCleanup(stdout.__exit__, None, None, None)

    def connection(self, chunks: list[bytes]) -> MagicMock:
        connection = MagicMock()
        connection.in_waiting = 1
        connection.read.side_effect = chunks
        connection.write.side_effect = len
        return connection

    def test_split_status_requires_complete_line(self) -> None:
        connection = self.connection([STATUS[:30].encode(), STATUS[30:-1].encode(), b"\n"])
        fields = supervisor.status(connection, 1)
        supervisor.require_healthy(fields)
        connection.write.assert_called_once_with(b"\x1ds")
        self.assertEqual(connection.read.call_count, 3)

    def test_short_command_write_is_not_success(self) -> None:
        connection = MagicMock()
        connection.write.return_value = 1
        with self.assertRaises(supervisor.FlashError):
            supervisor.send_command(connection, "x")
        connection.flush.assert_not_called()

    def test_incomplete_status_is_rejected(self) -> None:
        connection = self.connection([b"[diag] running=1 verified=1\n"])
        with self.assertRaises(supervisor.FlashError):
            supervisor.status(connection, 1)

    def test_boot_waits_for_prompt_before_status(self) -> None:
        connection = self.connection([b"[boot] SRAM load: 4096/65536\n", b"\nA>", STATUS.encode()])
        events: list[str] = []
        chunks = iter(connection.read.side_effect)

        def read(count: int) -> bytes:
            chunk = next(chunks)
            events.append(chunk.decode())
            return chunk

        connection.read.side_effect = read
        connection.write.side_effect = lambda data: (events.append("command"), len(data))[1]
        supervisor.wait_boot(connection, 1)
        self.assertGreater(events.index("command"), events.index("\nA>"))

    def test_stop_requires_flush_confirmation(self) -> None:
        connection = self.connection([
            STATUS.encode(),
            b"[diag] Stop requested; waiting for disk completion/flush.\n",
            b"[diag] Disk quiescent; RESET held LOW, PWM stopped.\n",
        ])
        supervisor.stop(connection, 1)
        self.assertEqual([call.args[0] for call in connection.write.call_args_list], [b"\x1ds", b"\x1dx"])

    def test_stopped_cpu_does_not_get_another_stop(self) -> None:
        connection = self.connection([STATUS.replace("running=1", "running=0").encode()])
        supervisor.stop(connection, 1)
        connection.write.assert_called_once_with(b"\x1ds")

    def test_held_cpu_with_storage_error_is_not_clean(self) -> None:
        connection = self.connection([
            STATUS.replace("running=1", "running=0").replace("disk=01", "disk=05").encode()
        ])
        with self.assertRaises(supervisor.FlashError):
            supervisor.stop(connection, 1)

    def test_timeout_has_no_retry_or_reset(self) -> None:
        connection = self.connection([])
        with patch.object(supervisor.time, "monotonic", side_effect=[0, 2]):
            with self.assertRaisesRegex(supervisor.FlashError, "No automatic retry"):
                supervisor.status(connection, 1)
        connection.write.assert_called_once_with(b"\x1ds")

    def test_boot_failure_and_unhealthy_status_fail(self) -> None:
        connection = self.connection([b"[boot] FAIL: SRAM mismatch\n"])
        with self.assertRaises(supervisor.FlashError):
            supervisor.wait_boot(connection, 1)
        for field, value in (
            ("fatal", "1"), ("BIOS", "0"), ("trap_timeouts", "1"), ("failure", "error"),
        ):
            fields = dict(item.split("=", 1) for item in STATUS.split()[1:])
            fields[field] = value
            with self.assertRaises(supervisor.FlashError):
                supervisor.require_healthy(fields)

    def test_unknown_owner_never_signalled(self) -> None:
        with (
            patch.object(supervisor, "serial_owners", return_value={123: "screen"}),
            patch.object(supervisor.os, "kill") as kill,
        ):
            with self.assertRaises(supervisor.FlashError):
                supervisor.disconnect_picocom("/dev/cu.test")
        kill.assert_not_called()

    def test_picocom_gets_only_targeted_sigterm(self) -> None:
        with (
            patch.object(supervisor, "serial_owners", side_effect=[{123: "picocom"}, {}]),
            patch.object(
                supervisor.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, "/opt/bin/picocom\n", ""),
            ),
            patch.object(supervisor.os, "kill") as kill,
        ):
            supervisor.disconnect_picocom("/dev/cu.test")
        kill.assert_called_once_with(123, signal.SIGTERM)

    def test_changed_process_identity_is_not_signalled(self) -> None:
        with (
            patch.object(supervisor, "serial_owners", return_value={123: "picocom"}),
            patch.object(
                supervisor.subprocess, "run",
                return_value=subprocess.CompletedProcess([], 0, "/bin/other\n", ""),
            ),
            patch.object(supervisor.os, "kill") as kill,
        ):
            with self.assertRaises(supervisor.FlashError):
                supervisor.disconnect_picocom("/dev/cu.test")
        kill.assert_not_called()

    def test_ownership_check_fails_closed(self) -> None:
        for result in (
            subprocess.CompletedProcess([], 0, "p123\n", ""),
            subprocess.CompletedProcess([], 1, "", "permission denied"),
            subprocess.CompletedProcess([], 0, "unexpected\n", ""),
        ):
            with patch.object(supervisor.subprocess, "run", return_value=result):
                with self.assertRaises(supervisor.FlashError):
                    supervisor.serial_owners("/dev/cu.test")

    def test_lsof_checks_both_device_aliases(self) -> None:
        with patch.object(
            supervisor.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 0, "p123\ncpicocom\n", ""),
        ) as run:
            self.assertEqual(supervisor.serial_owners("/dev/cu.test"), {123: "picocom"})
        self.assertEqual(run.call_args.args[0], [
            "lsof", "-Fpc", "/dev/cu.test", "/dev/tty.test",
        ])

    def test_macos_lsof_file_records_and_partial_match_exit(self) -> None:
        for code in (0, 1):
            with self.subTest(code=code), patch.object(
                supervisor.subprocess, "run",
                return_value=subprocess.CompletedProcess([], code, "p22210\ncpicocom\nf3\n", ""),
            ):
                self.assertEqual(supervisor.serial_owners("/dev/cu.test"), {22210: "picocom"})

    def test_unique_pico_is_discovered_without_fixed_port(self) -> None:
        port = argparse.Namespace(device="/dev/cu.changed", vid=0x2E8A, serial_number="BOARD")
        with patch.object(supervisor.list_ports, "comports", return_value=[port]):
            self.assertEqual(supervisor.resolve_device(), ("/dev/cu.changed", "BOARD"))
            self.assertEqual(supervisor.resolve_device(device="board"), ("/dev/cu.changed", "BOARD"))
            with self.assertRaises(supervisor.FlashError):
                supervisor.resolve_device(port="/dev/cu.old")

    def test_ambiguous_or_absent_picos_are_not_guessed(self) -> None:
        ports = [
            argparse.Namespace(device="/dev/cu.one", vid=0x2E8A, serial_number="ONE"),
            argparse.Namespace(device="/dev/cu.two", vid=0x2E8A, serial_number="TWO"),
        ]
        for connected in ([], ports):
            with patch.object(supervisor.list_ports, "comports", return_value=connected):
                with self.assertRaises(supervisor.FlashError):
                    supervisor.resolve_device()
        with patch.object(supervisor.list_ports, "comports", return_value=ports):
            self.assertEqual(supervisor.resolve_device(device="TWO"), ("/dev/cu.two", "TWO"))

    def test_reboot_tracks_serial_even_when_port_changes(self) -> None:
        port = argparse.Namespace(device="/dev/cu.new", vid=0x2E8A, serial_number="BOARD")
        with (
            patch.object(supervisor.list_ports, "comports", side_effect=[[], [port]]),
            patch.object(supervisor.time, "sleep"),
        ):
            self.assertEqual(supervisor.wait_port("BOARD", 1), "/dev/cu.new")

    def test_standalone_wait_boot_waits_for_selected_device(self) -> None:
        connection = MagicMock()
        with (
            patch.object(supervisor.sys, "argv", [
                "pico_supervisor.py", "wait-boot", "--serial", "BOARD", "--timeout", "1",
            ]),
            patch.object(supervisor, "wait_port", return_value="/dev/cu.new") as wait,
            patch.object(supervisor, "open_supervisor", return_value=connection) as open_port,
            patch.object(supervisor, "wait_boot") as boot,
            patch.object(supervisor.time, "sleep"),
        ):
            self.assertEqual(supervisor.main(), 0)
        wait.assert_called_once_with("BOARD", 1)
        open_port.assert_called_once_with("/dev/cu.new")
        boot.assert_called_once_with(connection.__enter__.return_value, 1)

    def test_serial_settings_and_failed_open_release(self) -> None:
        with (
            patch.object(supervisor, "require_free_port"),
            patch.object(supervisor.serial, "Serial") as serial_class,
        ):
            connection = supervisor.open_supervisor("/dev/cu.test")
            self.assertTrue(connection.dtr)
            self.assertFalse(connection.rts)
            self.assertTrue(serial_class.call_args.kwargs["exclusive"])
            self.assertFalse(serial_class.call_args.kwargs["rtscts"])
            connection.open.side_effect = OSError("disconnected")
            with self.assertRaises(OSError):
                supervisor.open_supervisor("/dev/cu.test")
            connection.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
