import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).with_name("z80_container.py")
SPEC = importlib.util.spec_from_file_location("z80_container", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
launcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launcher)


class ContainerLauncherTest(unittest.TestCase):
    def test_prepare_builds_checkout_images_and_copies_drive_a(self):
        with mock.patch.object(launcher, "require_container"), \
             mock.patch.object(launcher, "command") as command, \
             mock.patch.object(Path, "mkdir"), \
             mock.patch.object(launcher.shutil, "copyfile") as copy:
            launcher.prepare()
        self.assertIn(str(launcher.CONTAINER / "docker/prepare.py"), command.call_args_list[0].args)
        invocation = command.call_args_list[1].args
        self.assertIn(f"type=bind,source={launcher.ROOT},target=/workspace,readonly", invocation)
        self.assertIn("/workspace/src/cpm/build_images.py", invocation)
        copy.assert_called_once_with(launcher.SESSION / "drive_a_cpm63k-z80.img",
                                     launcher.SESSION / "drive_a_cpm63k.img")

    def test_test_action_delegates_to_regression_runner(self):
        with mock.patch.object(launcher, "require_container"), \
             mock.patch.object(launcher, "run_tests") as run_tests, \
             mock.patch("sys.argv", ["z80_container.py", "test",
                                    "--test", "emulator_z80_test.py"]):
            launcher.main()
        run_tests.assert_called_once_with(["--test", "emulator_z80_test.py"])

    def test_regressions_build_host_fixtures_and_mount_workspace(self):
        with mock.patch.dict("os.environ", {"DCC_DIR": "/example/dcc"}), \
             mock.patch.object(Path, "is_file", return_value=True), \
             mock.patch.object(launcher.z80_host_fixtures, "build") as build, \
             mock.patch.object(launcher, "ensure_test_image", return_value="z80-cpm-tests:cached"), \
             mock.patch.object(launcher, "command") as command:
            launcher.run_tests([])
        invocation = command.call_args_list[-1].args
        build.assert_called_once()
        self.assertEqual(build.call_args.args[1:], (Path("/example/dcc"), None))
        self.assertTrue(any("target=/workspace" in item for item in invocation))
        self.assertFalse(any("dcc-source" in item for item in invocation))
        self.assertIn("/workspace/scripts/z80_container_tests.py", invocation)
        self.assertIn("z80-cpm-tests:cached", invocation)

    def test_test_image_reuses_matching_image_and_rebuilds_on_request(self):
        image = "z80-cpm-tests:abc"
        image_list = json.dumps([{"configuration": {"name": f"docker.io/library/{image}"}}])
        result = subprocess.CompletedProcess([], 0, stdout=image_list)
        with mock.patch.object(launcher, "test_image_tag", return_value=image), \
             mock.patch.object(launcher.subprocess, "run", return_value=result), \
             mock.patch.object(launcher, "command") as command:
            self.assertEqual(launcher.ensure_test_image(), image)
            command.assert_not_called()
            self.assertEqual(launcher.ensure_test_image(rebuild=True), image)
            self.assertEqual(command.call_count, 2)

    def test_missing_image_is_built_and_inspection_failure_is_reported(self):
        result = subprocess.CompletedProcess([], 0, stdout="[]")
        with mock.patch.object(launcher, "test_image_tag", return_value="z80-cpm-tests:abc"), \
             mock.patch.object(launcher.subprocess, "run", return_value=result) as inspect, \
             mock.patch.object(launcher, "command") as command:
            launcher.ensure_test_image()
            self.assertEqual(command.call_count, 2)
            inspect.side_effect = subprocess.CalledProcessError(1, "container")
            with self.assertRaises(subprocess.CalledProcessError):
                launcher.ensure_test_image()

    def test_image_key_tracks_container_inputs_not_project_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            container = root / "container"
            (root / "scripts").mkdir()
            container.mkdir()
            (root / "scripts/z80-tests.Dockerfile").write_text("FROM example\n")
            (container / "Dockerfile").write_text("FROM base\n")
            (container / "runtime").mkdir()
            (container / "runtime/run.py").write_text("one")
            with mock.patch.object(launcher, "ROOT", root), \
                 mock.patch.object(launcher, "CONTAINER", container):
                first = launcher.test_image_tag()
                (container / "runtime/run.py").write_text("two")
                second = launcher.test_image_tag()
                self.assertNotEqual(first, second)
                (root / "scripts/z80-tests.Dockerfile").write_text("FROM changed\n")
                self.assertNotEqual(second, launcher.test_image_tag())

    def test_rebuild_flag_is_not_forwarded_to_container_runner(self):
        with mock.patch.dict("os.environ", {"DCC_DIR": "/example/dcc"}), \
             mock.patch.object(Path, "is_file", return_value=True), \
             mock.patch.object(launcher.z80_host_fixtures, "build") as build, \
             mock.patch.object(launcher, "ensure_test_image", return_value="test:image") as ensure, \
             mock.patch.object(launcher, "command") as command:
            launcher.run_tests(["--rebuild", "--test", "emulator_z80_test.py"])
        ensure.assert_called_once_with(True)
        self.assertNotIn("--rebuild", command.call_args.args)
        self.assertIn("--test", command.call_args.args)
        build.assert_not_called()

    def test_focused_bios_test_needs_neither_dcc_nor_ft(self):
        with mock.patch.dict("os.environ", {"DCC_DIR": "/missing/dcc",
                                            "Z80_FT_SOURCE": "/missing/FT.ASM"}), \
             mock.patch.object(launcher.z80_host_fixtures, "build") as build, \
             mock.patch.object(launcher, "ensure_test_image", return_value="test:image"), \
             mock.patch.object(launcher, "command") as command:
            launcher.run_tests(["--test", "cpm_boot_z80_test.py"])
        build.assert_not_called()
        self.assertNotIn("--ft-source", command.call_args.args)

    def test_all_disk_flash_builds_from_same_sources_as_emulator(self):
        configuration = json.loads((launcher.ROOT / ".vscode/tasks.json").read_text())
        task = next(task for task in configuration["tasks"]
                    if task["label"] == "Flash Pico - All disks (A-D)")
        command = task["command"]
        self.assertIn("scripts/z80_container.py images --output-dir build/cpm-disk-update", command)
        self.assertIn(" && python scripts/pico_flash.py disks", command)
        self.assertIn("--boot-package build/cpm-disk-update/z80boot.pkg", command)
        for drive, (image, _) in zip("ABCD", launcher.DISKS):
            self.assertIn(f"--drive {drive}=build/cpm-disk-update/{image}", command)
        self.assertTrue(all(not task["label"].startswith("Import ")
                            for task in configuration["tasks"]))

    def test_image_builder_mounts_output_and_firmware_without_writing_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"firmware")
            output = root / "output"
            with mock.patch.object(launcher, "ensure_test_image", return_value="test:image"), \
                 mock.patch.object(launcher, "command") as command:
                launcher.build_images(["--output-dir", str(output), "--firmware", str(firmware)])
                invocation = command.call_args.args
                self.assertIn(f"type=bind,source={launcher.ROOT},target=/workspace,readonly", invocation)
                self.assertIn(f"type=bind,source={output},target=/output", invocation)
                self.assertIn(f"type=bind,source={root},target=/firmware,readonly", invocation)
                self.assertIn("/firmware/firmware.bin", invocation)
                command.reset_mock()
                launcher.build_images(["--output-dir", str(output)])
                self.assertNotIn("--firmware", command.call_args.args)
                self.assertFalse(any("target=/firmware" in arg for arg in command.call_args.args))
                command.reset_mock()
                with self.assertRaisesRegex(SystemExit, "Firmware not found"):
                    launcher.build_images(["--output-dir", str(output),
                                           "--firmware", str(root / "missing.bin")])
                command.assert_not_called()
                with self.assertRaisesRegex(SystemExit, "cannot contain commas"):
                    launcher.build_images(["--output-dir", str(root / "bad,path")])
                command.assert_not_called()

    def test_individual_disk_tasks_validate_before_flashing_prepared_images(self):
        configuration = json.loads((launcher.ROOT / ".vscode/tasks.json").read_text())
        for drive, (image, _) in zip("ABCD", launcher.DISKS):
            with self.subTest(drive=drive):
                task = next(task for task in configuration["tasks"]
                            if task["label"] == f"Flash Pico - Drive {drive}")
                command = task["command"]
                self.assertIn(
                    "scripts/z80_container.py images --output-dir build/cpm-disk-update"
                    " && python scripts/pico_flash.py disks",
                    command,
                )
                self.assertIn(f"--drive {drive}=build/cpm-disk-update/{image}", command)

    def test_images_action_dispatches_arguments(self):
        with mock.patch.object(launcher, "require_container"), \
             mock.patch.object(launcher, "build_images") as build, \
             mock.patch("sys.argv", ["z80_container.py", "images", "--output-dir", "out"]):
            launcher.main()
        build.assert_called_once_with(["--output-dir", "out"])

    def test_assemble_mounts_checkout_read_only_and_output_writable(self):
        source = launcher.ROOT / "src/stage06_sram_dma/math_soak.asm"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve() / "result.bin"
            with mock.patch.object(launcher, "ensure_test_image", return_value="test:image"), \
                 mock.patch.object(launcher, "command") as command:
                launcher.assemble(["-I", str(source.parent), "-o", str(output), str(source)])
                invocation = command.call_args.args
                self.assertIn(f"type=bind,source={launcher.ROOT},target=/workspace,readonly", invocation)
                self.assertIn(f"type=bind,source={output.parent},target=/output", invocation)
                self.assertEqual(invocation[-5:], (
                    "-I", "/workspace/src/stage06_sram_dma", "-o", "/output/result.bin",
                    "/workspace/src/stage06_sram_dma/math_soak.asm"))
                command.reset_mock()
                with self.assertRaisesRegex(SystemExit, "Assembly input not found"):
                    launcher.assemble(["-o", str(output), str(output.parent / "missing.asm")])
                outside = output.parent / "outside.asm"
                outside.write_text("nop\n")
                with self.assertRaisesRegex(SystemExit, "inside the checkout"):
                    launcher.assemble(["-o", str(output), str(outside)])
                command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
