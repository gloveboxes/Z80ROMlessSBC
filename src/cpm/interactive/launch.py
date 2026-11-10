#!/usr/bin/env python3
"""Build everything the interactive banked CP/M emulator needs, then run it.

Steps: emulator library, dcc I/O adapter, banked examples (when a dcc checkout
is available), then the CP/M and BIOS images, which replaces build/cpm/drive_*.img
so every launch starts from src/disks/native/. Use --prepare-only to build
without starting a session.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

session_dir = Path(__file__).resolve().parent
root = session_dir.parents[2]
sys.path.insert(0, str(root / "src" / "cpm"))

import build_images

endpoint = session_dir / "terminal.endpoint"
bridge_script = session_dir / "terminal_bridge.py"
controller_script = session_dir / "controller.py"

# Each example's dccmake output, copied to the path build_images.py installs.
EXAMPLE_BUILDS = (
    ("ALLOCPRF", "build/ALLOCPRF.COM", "ALLOCPRF.COM"),
    ("BANKED", "build/BANKED.COM", "BANKED.COM"),
    ("MEMPERF", "build/MEMPERF.COM", "MEMPERF.COM"),
    ("PRIMEBK1", "build/PRIMEBK1.COM", "PRIMEBK1.COM"),
)


def run(command: list[str], **kwargs) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=root, check=True, **kwargs)


def find_dcc() -> Path | None:
    candidates = [os.environ.get("DCC_DIR"), root.parent / "dcc"]
    for candidate in candidates:
        if candidate and (Path(candidate) / "dccmake").is_file():
            return Path(candidate)
    return None


def find_dxsvc() -> Path | None:
    common = Path("esp32-altair-8800") / "Apps" / "COMMON"
    candidates = [os.environ.get("DXSVC_DIR"), root.parent / common,
                  root.parent / "esp32" / common]
    for candidate in candidates:
        if candidate and (Path(candidate) / "DXSVC.C").is_file():
            return Path(candidate)
    return None


def build_examples(dcc_dir: Path) -> None:
    environment = {**os.environ, "DCC_DIR": str(dcc_dir)}
    dccmake = str(dcc_dir / "dccmake")
    builds = list(EXAMPLE_BUILDS)
    dxsvc_dir = find_dxsvc()
    if dxsvc_dir is None:
        print("DXSVC SDK not found (set DXSVC_DIR): using the existing SOUND.COM.")
    else:
        environment["DXSVC_DIR"] = str(dxsvc_dir)
        builds.append(("SOUND", "build/SOUND.COM", "SOUND.COM"))
    for name, built, installed in builds:
        example = root / "examples" / name
        print(f"+ dccmake {name}", flush=True)
        subprocess.run([dccmake], cwd=example, env=environment, check=True,
                       stdout=subprocess.DEVNULL)
        shutil.copyfile(example / built, example / installed)
    print("+ dccmake LIBDEMO", flush=True)
    subprocess.run([dccmake], cwd=root / "examples" / "LIBDEMO", env=environment,
                   check=True, stdout=subprocess.DEVNULL)


def prepare(skip_examples: bool) -> None:
    host = root / "src" / "cpm" / "z80_debug_host"
    adapter = root / "src" / "cpm" / "dcc_io_adapter"
    run(["cmake", "-S", str(host), "-B", "build/cpm/z80_debug_host",
         "-DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF"])
    run(["cmake", "--build", "build/cpm/z80_debug_host"])
    run(["cmake", "-S", str(adapter), "-B", "build/cpm/dcc_io_adapter",
         "-DBUILD_TESTING=OFF"])
    run(["cmake", "--build", "build/cpm/dcc_io_adapter"])

    dcc_dir = None if skip_examples else find_dcc()
    if dcc_dir is not None:
        build_examples(dcc_dir)
    elif not skip_examples:
        print("dcc checkout not found (set DCC_DIR): using the existing example binaries.")

    run([sys.executable, str(root / "src" / "cpm" / "build_images.py"),
         "--assembler", "z80asm", "--output-dir", "build/cpm"])


def run_session() -> None:
    endpoint.unlink(missing_ok=True)
    bridge = subprocess.Popen([
        sys.executable,
        str(bridge_script),
        "--endpoint-file",
        str(endpoint),
    ], cwd=root)
    controller = None
    try:
        while not endpoint.exists():
            if bridge.poll() is not None:
                raise RuntimeError("terminal bridge exited before publishing its endpoint")
            time.sleep(0.05)
        controller = subprocess.Popen([sys.executable, str(controller_script)], cwd=root)
        while True:
            controller_exit = controller.poll()
            bridge_exit = bridge.poll()
            if controller_exit is not None:
                if controller_exit:
                    raise SystemExit(
                        f"interactive CP/M controller exited with status {controller_exit}"
                    )
                if bridge_exit is None:
                    bridge.terminate()
                    bridge.wait()
                break
            if bridge_exit is not None:
                if bridge_exit:
                    raise SystemExit(
                        f"interactive terminal bridge exited with status {bridge_exit}"
                    )
                controller.terminate()
                controller.wait()
                break
            time.sleep(0.1)
    finally:
        if controller is not None and controller.poll() is None:
            controller.terminate()
            controller.wait()
        if bridge.poll() is None:
            bridge.terminate()
            bridge.wait()
        endpoint.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true",
                        help="build the emulator, examples and images without starting a session")
    parser.add_argument("--skip-examples", action="store_true",
                        help="do not rebuild the banked examples with dccmake")
    args = parser.parse_args()
    prepare(args.skip_examples)
    if not args.prepare_only:
        run_session()


if __name__ == "__main__":
    main()
