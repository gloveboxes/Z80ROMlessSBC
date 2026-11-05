from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import pico_build
import pico_stage11 as stage11
from pico_supervisor import FlashError


DEVICE_INFO = """Device Information
 type:       RP2350
 package:    QFN60
 chipid:     0x0123456789abcdef
 flash size: 4096K
"""
RUNNING_DEVICE = """No accessible RP-series devices in BOOTSEL mode were found.

but:

RP2350 device at bus 0, address 1 appears to have a USB serial connection, so
    consider -f (or -F) to force reboot in order to run the command.
"""


class BootselDiscoveryTests(unittest.TestCase):
    def test_running_sdk_device_can_be_selected_for_force_flash(self) -> None:
        with patch.object(stage11.subprocess, "run", return_value=subprocess.CompletedProcess(
            ["picotool"], 249, stdout=RUNNING_DEVICE, stderr="",
        )):
            self.assertIsNone(stage11.discover_bootsel_serial())

    def test_explicit_serial_filters_read_only_query(self) -> None:
        with patch.object(stage11.subprocess, "run", return_value=subprocess.CompletedProcess(
            ["picotool"], 0, stdout=DEVICE_INFO, stderr="",
        )) as run:
            self.assertEqual(stage11.discover_bootsel_serial("SELECTED"), "0123456789ABCDEF")
            self.assertEqual(
                run.call_args.args[0],
                ["picotool", "info", "--device", "--ser", "SELECTED"],
            )

    def test_no_device_or_permission_errors_never_enable_force(self) -> None:
        for output in (
            "No accessible RP-series devices in BOOTSEL mode were found.",
            RUNNING_DEVICE + "\nRP2350 device appears to be in BOOTSEL mode, "
            "but picotool was unable to connect.",
            RUNNING_DEVICE + "\nUSB serial device: unable to connect.",
            RUNNING_DEVICE.replace("RP2350", "RP2040"),
        ):
            with self.subTest(output=output), patch.object(
                stage11.subprocess, "run", return_value=subprocess.CompletedProcess(
                    ["picotool"], 249, stdout=output, stderr="",
                ),
            ):
                with self.assertRaises(FlashError):
                    stage11.discover_bootsel_serial()

    def test_discovers_chip_id_as_usb_serial(self) -> None:
        with patch.object(stage11.subprocess, "run", return_value=subprocess.CompletedProcess(
            ["picotool"], 0, stdout=DEVICE_INFO, stderr="",
        )) as run:
            self.assertEqual(stage11.discover_bootsel_serial(), "0123456789ABCDEF")
            run.assert_called_once_with(
                ["picotool", "info", "--device"], capture_output=True,
                text=True, timeout=15, check=False,
            )

    def test_refuses_missing_multiple_or_unrecognised_devices(self) -> None:
        for output in (
            "", DEVICE_INFO * 2, DEVICE_INFO.replace("RP2350", "RP2040"),
            DEVICE_INFO.replace("chipid:", "unknown:"),
            DEVICE_INFO.replace("0123456789abcdef", "invalid"),
        ):
            with self.subTest(output=output), patch.object(
                stage11.subprocess, "run", return_value=subprocess.CompletedProcess(
                    ["picotool"], 0, stdout=output, stderr="",
                ),
            ):
                with self.assertRaises(FlashError):
                    stage11.discover_bootsel_serial()

    def test_query_errors_do_not_select_a_partial_result(self) -> None:
        with patch.object(stage11.subprocess, "run", return_value=subprocess.CompletedProcess(
            ["picotool"], 1, stdout=DEVICE_INFO, stderr="USB access denied",
        )):
            with self.assertRaisesRegex(FlashError, "USB access denied"):
                stage11.discover_bootsel_serial()
        with patch.object(stage11.subprocess, "run", side_effect=subprocess.TimeoutExpired(
            ["picotool"], 15,
        )):
            with self.assertRaisesRegex(FlashError, "timed out"):
                stage11.discover_bootsel_serial()


class Stage11Tests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.sdk = Path(directory.name)
        (self.sdk / "external").mkdir()
        (self.sdk / "external/pico_sdk_import.cmake").touch()
        for redirect in (redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO())):
            redirect.__enter__()
            self.addCleanup(redirect.__exit__, None, None, None)
        discovery = patch.object(stage11, "discover_bootsel_serial", return_value="SPARE")
        self.discover = discovery.start()
        self.addCleanup(discovery.stop)

    def invoke(self, mode: str = "isp", confirmed: bool = True,
               serial: str | None = None, sdk_path: Path | None = None) -> int:
        arguments = ["pico_stage11.py", mode]
        if serial is not None:
            arguments += ["--serial", serial]
        if sdk_path is not None:
            arguments += ["--sdk-path", str(sdk_path)]
        if confirmed:
            arguments.append("--confirmed")
        with patch("sys.argv", arguments):
            return stage11.main()

    def test_build_flash_console_sequence(self) -> None:
        for mode, (target, directory, board) in stage11.IMAGES.items():
            with self.subTest(mode=mode), \
                    patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                    patch.object(stage11.subprocess, "run") as run, \
                    patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered") as wait, \
                    patch.object(stage11, "require_free_port") as free:
                self.assertEqual(self.invoke(mode), 0)
                self.assertEqual(run.call_count, 3)
                build, flash, console = [call.args[0] for call in run.call_args_list]
                self.assertIn("--source-dir", build)
                self.assertIn("src/stage11_extension_board", build)
                self.assertNotIn("--sdk-path", build)
                expected_board = (
                    "pico2_w" if mode == "isp" else "pimoroni_pico_plus2_w_rp2350"
                )
                self.assertEqual(board, expected_board)
                self.assertEqual(build[build.index("--board") + 1], expected_board)
                build_dir = stage11.REPO_ROOT / "build/stage11" / expected_board
                self.assertEqual(build[build.index("--build-dir") + 1], str(build_dir))
                self.assertEqual(build[-1], target)
                self.assertEqual(flash[3], str(build_dir / directory / f"{target}.uf2"))
                self.assertEqual(
                    flash[-5:],
                    ["--serial", "SPARE", "--bootsel", "--stopped", "--power-confirmed"],
                )
                self.assertNotIn("--force", flash)
                wait.assert_called_once_with("SPARE", 30)
                free.assert_called_once_with("/dev/cu.rediscovered")
                self.assertEqual(console[-1], "/dev/cu.rediscovered")
                self.assertTrue(all(call.kwargs["check"] for call in run.call_args_list))

    def test_rejects_unconfirmed_and_missing_tools(self) -> None:
        with patch.object(stage11.subprocess, "run") as run:
            self.assertEqual(self.invoke(confirmed=False), 1)
            with patch.object(stage11.shutil, "which", return_value=None):
                self.assertEqual(self.invoke(), 1)
            self.assertEqual(self.invoke(sdk_path=self.sdk / "missing"), 1)
            run.assert_not_called()
            self.discover.assert_not_called()

    def test_blank_serial_uses_discovery(self) -> None:
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11.subprocess, "run"), \
                patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered"), \
                patch.object(stage11, "require_free_port"):
            self.assertEqual(self.invoke(serial=" "), 0)
            self.discover.assert_called_once_with(None)

    def test_explicit_serial_filters_discovery(self) -> None:
        self.discover.return_value = "SELECTED"
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11.subprocess, "run") as run, \
                patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered") as wait, \
                patch.object(stage11, "require_free_port"):
            self.assertEqual(self.invoke(serial=" SELECTED "), 0)
            self.discover.assert_called_once_with("SELECTED")
            self.assertIn("SELECTED", run.call_args_list[1].args[0])
            wait.assert_called_once_with("SELECTED", 30)

    def test_running_device_uses_existing_force_flash_workflow(self) -> None:
        self.discover.return_value = None
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11, "resolve_device",
                             return_value=("/dev/cu.running", "RUNNING")) as resolve, \
                patch.object(stage11.subprocess, "run") as run, \
                patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered") as wait, \
                patch.object(stage11, "require_free_port"):
            self.assertEqual(self.invoke(), 0)
            resolve.assert_called_once_with(device=None)
            flash = run.call_args_list[1].args[0]
            self.assertIn("--force", flash)
            self.assertIn("RUNNING", flash)
            self.assertNotIn("--bootsel", flash)
            self.assertNotIn("--stopped", flash)
            wait.assert_called_once_with("RUNNING", 30)
            self.assertEqual(
                [call.args for call in self.discover.call_args_list],
                [(None,), ("RUNNING",)],
            )

    def test_running_serial_must_match_picotool_rp2350_discovery(self) -> None:
        for result in (FlashError("Selected serial is not a reset-capable RP2350"), "BOOTSEL"):
            self.discover.reset_mock()
            self.discover.side_effect = [None, result]
            with self.subTest(result=result), \
                    patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                    patch.object(stage11, "resolve_device",
                                 return_value=("/dev/cu.running", "RUNNING")), \
                    patch.object(stage11.subprocess, "run") as run:
                self.assertEqual(self.invoke(), 1)
                self.assertEqual(
                    [call.args for call in self.discover.call_args_list],
                    [(None,), ("RUNNING",)],
                )
                run.assert_not_called()

    def test_ambiguous_running_devices_stop_before_build_or_reset(self) -> None:
        self.discover.return_value = None
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11, "resolve_device",
                             side_effect=FlashError("Expected one matching Pico")) as resolve, \
                patch.object(stage11.subprocess, "run") as run:
            self.assertEqual(self.invoke(), 1)
            resolve.assert_called_once_with(device=None)
            run.assert_not_called()

    def test_explicit_running_serial_is_preserved(self) -> None:
        self.discover.return_value = None
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11, "resolve_device",
                             return_value=("/dev/cu.running", "SELECTED")) as resolve, \
                patch.object(stage11.subprocess, "run") as run, \
                patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered"), \
                patch.object(stage11, "require_free_port"):
            self.assertEqual(self.invoke(serial="SELECTED"), 0)
            self.discover.assert_called_once_with("SELECTED")
            resolve.assert_called_once_with(device="SELECTED")
            flash = run.call_args_list[1].args[0]
            self.assertIn("--force", flash)
            self.assertIn("SELECTED", flash)

    def test_discovery_failure_stops_before_build_or_flash(self) -> None:
        self.discover.side_effect = FlashError("Multiple devices; select --serial")
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11.subprocess, "run") as run:
            self.assertEqual(self.invoke(), 1)
            run.assert_not_called()

    def test_explicit_sdk_override_is_forwarded(self) -> None:
        with patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                patch.object(stage11.subprocess, "run") as run, \
                patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered"), \
                patch.object(stage11, "require_free_port"):
            self.assertEqual(self.invoke(sdk_path=self.sdk), 0)
            build = run.call_args_list[0].args[0]
            self.assertEqual(build[build.index("--sdk-path") + 1], str(self.sdk.resolve()))

    def test_build_or_flash_failure_stops_workflow(self) -> None:
        for successful_calls in (0, 1):
            results = [None] * successful_calls + [
                subprocess.CalledProcessError(2, ["failed-command"]),
            ]
            with self.subTest(successful_calls=successful_calls), \
                    patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                    patch.object(stage11.subprocess, "run", side_effect=results) as run, \
                    patch.object(stage11, "wait_port") as wait:
                self.assertEqual(self.invoke(), 2)
                self.assertEqual(run.call_count, successful_calls + 1)
                wait.assert_not_called()

    def test_missing_console_and_busy_port_are_errors(self) -> None:
        for wait_error in (True, False):
            with self.subTest(wait_error=wait_error), \
                    patch.object(stage11.shutil, "which", return_value="/bin/picocom"), \
                    patch.object(stage11.subprocess, "run") as run, \
                    patch.object(stage11, "wait_port", return_value="/dev/cu.rediscovered") as wait, \
                    patch.object(stage11, "require_free_port") as free:
                if wait_error:
                    wait.side_effect = FlashError("USB did not reappear")
                else:
                    free.side_effect = FlashError("Port busy")
                self.assertEqual(self.invoke(), 1)
                self.assertEqual(run.call_count, 2)

    def test_build_helper_defaults_and_standalone_source(self) -> None:
        for standalone in (False, True):
            arguments = ["pico_build.py", "--build-dir", str(self.sdk / "build"),
                         "--target", "test_target"]
            if standalone:
                arguments += ["--source-dir", "src/stage11_extension_board",
                              "--sdk-path", str(self.sdk),
                              "--board", "pimoroni_pico_plus2_w_rp2350"]
            with self.subTest(standalone=standalone), \
                    patch("sys.argv", arguments), \
                    patch.object(pico_build, "select_toolchain", return_value=Path("/toolchain/bin")), \
                    patch.object(pico_build.subprocess, "run") as run:
                self.assertEqual(pico_build.main(), 0)
                configure = run.call_args_list[0].args[0]
                source = pico_build.REPO_ROOT
                if standalone:
                    source /= "src/stage11_extension_board"
                    self.assertIn(f"-DPICO_SDK_PATH={self.sdk.resolve()}", configure)
                    self.assertIn("-DPICO_BOARD=pimoroni_pico_plus2_w_rp2350", configure)
                else:
                    self.assertIn("-DPICO_BOARD=pico2_w", configure)
                self.assertEqual(configure[2], str(source))
                self.assertEqual(run.call_count, 2)

    def test_task_definitions_match_workflow(self) -> None:
        config = json.loads((stage11.REPO_ROOT / ".vscode/tasks.json").read_text())
        tasks = [task for task in config["tasks"] if "Stage 11" in task["label"]]
        self.assertEqual(len(tasks), 2)
        inputs = {item["id"]: item for item in config["inputs"]}
        self.assertNotIn("picoStage11SdkPath", inputs)
        self.assertNotIn("picoStage11Serial", inputs)
        for task, mode in zip(tasks, ("isp", "test")):
            self.assertIn(f"pico_stage11.py {mode}", task["command"])
            self.assertIn("import sys, serial", task["command"])
            self.assertIn(". .venv-flash/bin/activate", task["command"])
            self.assertIn("--confirmed", task["command"])
            self.assertNotIn("--sdk-path", task["command"])
            self.assertNotIn("--serial", task["command"])
            self.assertNotIn("PICO_SDK_PATH", task["options"]["env"])
            self.assertNotIn("STAGE11_SERIAL", task["options"]["env"])
            safety = task["options"]["env"]["STAGE11_CONFIRM"]
            safety_id = safety.removeprefix("${input:").removesuffix("}")
            self.assertEqual(inputs[safety_id]["default"], "Cancel")
            self.assertIn("safe to interrupt", inputs[safety_id]["description"])
            if mode == "test":
                self.assertIn("Pimoroni Pico Plus 2 W", task["detail"])
                self.assertIn("Pimoroni Pico Plus 2 W", inputs[safety_id]["description"])


if __name__ == "__main__":
    unittest.main()
