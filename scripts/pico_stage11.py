#!/usr/bin/env python3
"""Build, flash and open the console for the selected Stage 11 board."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

from pico_supervisor import FlashError, require_free_port, resolve_device, wait_port


REPO_ROOT = Path(__file__).resolve().parents[1]
IMAGES = {
    "isp": ("stage11_flash_attiny85", "flash_ATTiny85", "pico2_w"),
    "test": ("stage11_test_attiny85", "test_ATTiny85", "pimoroni_pico_plus2_w_rp2350"),
}


def discover_bootsel_serial(serial: str | None = None) -> str | None:
    command = ["picotool", "info", "--device"]
    if serial:
        command.extend(["--ser", serial])
    try:
        result = subprocess.run(
            command, capture_output=True,
            text=True, timeout=15, check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise FlashError("BOOTSEL discovery timed out; no reset or flash performed") from error
    if result.returncode:
        details = (result.stdout + result.stderr).strip()
        # ERROR_NO_DEVICE is -7 (249 as a POSIX exit status). Only a reported
        # reset-capable RP2350 permits runtime discovery; access errors stay fatal.
        summary = " ".join(details.split())
        if (
            result.returncode in (-7, 249)
            and summary.startswith("No accessible RP-series devices in BOOTSEL mode")
            and re.search(
                r"RP2350 device at bus \d+, address \d+ "
                r"appears to have a USB serial connection, so",
                summary,
            )
            and "appears to be in BOOTSEL mode" not in summary
            and "unable to connect" not in summary
        ):
            return None
        raise FlashError(
            "BOOTSEL discovery failed. Hold BOOTSEL (BOOT on Pimoroni) while "
            f"connecting the intended board's USB cable. No device was reset.\n{details}"
        )
    fields = [line.strip().partition(":") for line in result.stdout.splitlines()]
    types = [value.strip() for key, _, value in fields if key == "type"]
    if types != ["RP2350"]:
        raise FlashError(
            f"Expected exactly one RP2350 device in BOOTSEL; found {len(types)} "
            f"device(s): {', '.join(types) or 'none'}. "
            "Disconnect other BOOTSEL devices or use --serial SERIAL."
        )
    identifiers = [value.strip() for key, _, value in fields if key == "chipid"]
    match = re.fullmatch(r"(?:0x)?([0-9a-fA-F]{16})", identifiers[0]) \
        if len(identifiers) == 1 else None
    if match is None:
        raise FlashError("picotool did not report one valid RP2350 chip ID; no flash performed")
    # RP2350 BOOTSEL and SDK USB serials use the same 16-digit chip ID.
    return match.group(1).upper()


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
            print("The LED/tone test starts when the console connects.", flush=True)
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
