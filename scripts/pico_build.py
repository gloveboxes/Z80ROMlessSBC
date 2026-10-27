#!/usr/bin/env python3
"""Configure and build Pico firmware with an installed Arm GNU/Newlib toolchain."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]


def complete_toolchain(bin_dir: Path) -> bool:
    for tool in ("gcc", "g++", "objcopy", "objdump"):
        if not os.access(bin_dir / f"arm-none-eabi-{tool}", os.X_OK):
            return False
    # Ask the compiler where its libraries live; installation layouts vary.
    for compiler, library in (("gcc", "libc.a"), ("gcc", "libg.a"),
                              ("g++", "libstdc++.a")):
        try:
            result = subprocess.run(
                [str(bin_dir / f"arm-none-eabi-{compiler}"),
                 "-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=softfp",
                 f"-print-file-name={library}"],
                check=True, capture_output=True, text=True, timeout=10,
            )
        except (OSError, subprocess.SubprocessError):
            return False
        location = result.stdout.strip()
        if location == library or not Path(location).is_file():
            return False
    return True


def cached_compiler(build_dir: Path) -> Path | None:
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache.read_text(), re.M)
        if match:
            return Path(match.group(1))
    return None


def select_toolchain(cached: Path | None) -> Path:
    override = os.environ.get("ARM_GNU_TOOLCHAIN_BIN")
    if override:
        selected = Path(override).expanduser().resolve()
        if not complete_toolchain(selected):
            raise RuntimeError(f"ARM_GNU_TOOLCHAIN_BIN is not a complete Arm GNU/Newlib toolchain: {selected}")
        if cached and cached.resolve() != (selected / "arm-none-eabi-gcc").resolve():
            raise RuntimeError(
                f"This build already uses {cached}. Use a new build directory "
                "or reconfigure it with cmake --fresh to change compilers."
            )
        return selected
    if cached:
        if complete_toolchain(cached.parent):
            return cached.parent
        raise RuntimeError(
            f"The cached compiler is missing or lacks the required runtime: {cached}. "
            "Use a new build directory or reconfigure it with cmake --fresh."
        )

    candidates = [Path(entry or os.curdir).resolve()
                  for entry in os.get_exec_path()]
    # Search versioned local installs without pinning a release. Prefer the
    # newest directory version when PATH has no complete toolchain.
    local_installs = (Path.home() / ".local/share").glob("arm-gnu-toolchain*/bin")
    candidates.extend(sorted(
        local_installs,
        key=lambda entry: tuple(int(n) for n in re.findall(r"\d+", entry.parent.name)),
        reverse=True,
    ))
    seen = set()
    for candidate in candidates:
        if candidate not in seen and complete_toolchain(candidate):
            return candidate
        seen.add(candidate)
    raise RuntimeError(
        "No complete Arm GNU bare-metal toolchain with Newlib was found. "
        "Add its bin directory to PATH or set ARM_GNU_TOOLCHAIN_BIN."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--target", required=True)
    args = parser.parse_args()
    banked_pio = args.target == "z80_stage10_websocket_terminal"
    build_dir = args.build_dir
    if not build_dir.is_absolute():
        build_dir = REPO_ROOT / build_dir
    try:
        toolchain_bin = select_toolchain(cached_compiler(build_dir))
        print(f"Using Arm toolchain: {toolchain_bin}", flush=True)
        env = os.environ.copy()
        env["PATH"] = str(toolchain_bin) + os.pathsep + env.get("PATH", "")
        env["PICO_TOOLCHAIN_PATH"] = str(toolchain_bin.parent)
        subprocess.run(
            ["cmake", "-S", str(REPO_ROOT), "-B", str(build_dir),
             "-G", "Ninja", "-DPICO_BOARD=pico2_w",
             "-DZ80_ENABLE_BANKED_PIO=" + ("ON" if banked_pio else "OFF")],
            cwd=REPO_ROOT, env=env, check=True,
        )
        subprocess.run(
            ["cmake", "--build", str(build_dir), "--target", args.target, "-j"],
            cwd=REPO_ROOT, env=env, check=True,
        )
    except (RuntimeError, OSError) as error:
        print(f"Build error: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        return error.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
