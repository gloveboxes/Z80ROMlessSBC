#!/usr/bin/env python3
"""Run Z80 regressions against the isolated host-prepared workspace."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

WORKSPACE = Path("/workspace")
EMULATOR = "/opt/altair/build/bin/libz80sbc-emulator.so"
HOST = "/opt/altair/build/bin/z80-debug-host"
TESTS = (
    "emulator_z80_test.py",
    "banked_memory_z80_test.py",
    "banked_allocator_z80_test.py",
    "bank1_bdos_z80_test.py",
    "bank0_bdos_shim_z80_test.py",
    "bank1_system_boot_z80_test.py",
    "bank_sdk_z80_test.py",
    "cpm_boot_z80_test.py",
    "banked_heap_z80_test.py",
    "memperf_z80_test.py",
    "allocperf_z80_test.py",
)


def run(args, cwd, env):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(args, cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ft-source", type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--test", choices=TESTS, help="run one execution test")
    selection.add_argument("--benchmark", choices=("sdk", "transfer", "services"))
    args = parser.parse_args()
    if not args.test and not args.benchmark and args.ft_source is None:
        parser.error("--ft-source is required for the full suite")
    with tempfile.TemporaryDirectory(prefix="z80-regressions-") as directory:
        root = Path(directory)
        workspace = WORKSPACE
        env = {**os.environ, "Z80SBC_EMULATOR": EMULATOR}
        tests = workspace / "src/common/tests"
        run([sys.executable, tests / "emulator_z80_test.py"], workspace, env)
        if args.benchmark:
            script = ("banked_transfer_benchmark.py" if args.benchmark == "transfer"
                      else "banked_sdk_benchmark.py")
            options = ["--services"] if args.benchmark == "services" else []
            run([sys.executable, tests / script, *options], workspace, env)
            return
        if args.test:
            if args.test != TESTS[0]:
                run([sys.executable, tests / args.test], workspace, env)
            return
        run(["cmake", "-S", tests, "-B", root / "control", "-G", "Ninja"],
            workspace, env)
        run(["cmake", "--build", root / "control", "--target", "math_soak_test", "scope_test"],
            workspace, env)
        for name, builder, extra in (
            ("math soak", "math_soak_test", []),
            ("scope", "scope_test", ["--scope"]),
        ):
            print(f"Running {name} execution tests", flush=True)
            run([sys.executable, tests / "math_soak_z80_test.py", "--image-builder",
                 root / "control" / builder, *extra], workspace, env)
        for name in TESTS[1:]:
            run([sys.executable, tests / name], workspace, env)
        run([sys.executable, workspace / "src/cpm/test_z80_debug_host.py", "--host", HOST],
            workspace, env)
        run([sys.executable, workspace / "src/cpm/test_http_ft.py",
             "--ft-source", args.ft_source], workspace, env)


if __name__ == "__main__":
    main()
