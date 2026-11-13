#!/usr/bin/env python3
"""Build, flash and open the console for the selected Stage 11 board."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

from pico_supervisor import (
    FlashError, discover_bootsel_serial, require_free_port, resolve_device, wait_port,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
IMAGES = {
    "isp": ("stage11_flash_attiny85", "flash_ATTiny85", "pico2_w"),
    "test": ("stage11_test_attiny85", "test_ATTiny85", "pimoroni_pico_plus2_w_rp2350"),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=IMAGES)
    parser.add_argument("--serial", help="select a USB serial; otherwise discover a unique Pico")
    parser.add_argument("--sdk-path", type=Path, help="override the project's Pico SDK default")
    parser.add_argument(
        "--confirmed", action="store_true",
        help="confirm the isolated flashing/testing board, wiring, power and safe interruption",
    )
    args = parser.parse_args()
    try:
        if not args.confirmed:
            raise FlashError("Confirm the spare Pico setup before running this task")
        sdk_path = args.sdk_path.expanduser().resolve() if args.sdk_path else None
        if sdk_path is not None and not (sdk_path / "external/pico_sdk_import.cmake").is_file():
            raise FlashError(f"Not a Pico SDK directory: {sdk_path}")
        console = shutil.which("picocom")
        if console is None:
            raise FlashError("picocom is required to open the Stage 11 USB console")

        selected_serial = (args.serial or "").strip() or None
        device = discover_bootsel_serial(selected_serial)
        if device is None:
            _, device = resolve_device(device=selected_serial)
            # Tie serial-port discovery to the reset-capable RP2350 reported above.
            if selected_serial is None and discover_bootsel_serial(device) is not None:
                raise FlashError("Pico changed mode during discovery; rerun the task")
            flash_mode = ["--force"]
            print(f"Using running Stage 11 Pico {device}; its firmware will be "
                  "interrupted for flashing.", flush=True)
        else:
            flash_mode = ["--bootsel", "--stopped"]
            print(f"Using Stage 11 Pico USB serial {device} in BOOTSEL.", flush=True)
        target, directory, board = IMAGES[args.mode]
        build_dir = REPO_ROOT / "build/stage11" / board
        print(f"Building Stage 11 {args.mode} for {board}.", flush=True)
        build = [
            sys.executable, str(REPO_ROOT / "scripts/pico_build.py"),
            "--source-dir", "src/stage11_extension_board",
            "--board", board,
            "--build-dir", str(build_dir), "--target", target,
        ]
        if sdk_path is not None:
            build.extend(["--sdk-path", str(sdk_path)])
        subprocess.run(build, cwd=REPO_ROOT, check=True)
        subprocess.run(
            [sys.executable, str(REPO_ROOT / "scripts/pico_flash.py"),
             "firmware", str(build_dir / directory / f"{target}.uf2"),
             "--serial", device, *flash_mode, "--power-confirmed"],
            cwd=REPO_ROOT, check=True,
        )
        print(f"Waiting for Stage 11 USB console on Pico {device}...", flush=True)
        port = wait_port(device, 30)
        require_free_port(port)
        print(f"Opening {port}. Exit picocom with Ctrl-A, then Ctrl-X.", flush=True)
        if args.mode == "isp":
            print("Type uppercase P in the console to erase/program the ATtiny85.",
                  flush=True)
        else:
            print("The sound/status test starts automatically at power-up.", flush=True)
        subprocess.run(
            [console, "--baud", "115200", "--flow", "n", "--imap", "lfcrlf", port],
            cwd=REPO_ROOT, check=True,
        )
    except (FlashError, OSError) as error:
        print(f"Stage 11 error: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"Stage 11 command failed ({error.returncode}): {error.cmd}",
              file=sys.stderr)
        return error.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
