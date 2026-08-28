#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[3]
session_dir = Path(__file__).resolve().parent
endpoint = session_dir / "terminal.endpoint"
bridge_script = Path(__file__).resolve().with_name("terminal_bridge.py")
controller_script = session_dir / "controller.py"
adapter = session_dir / "libz80sbc-interactive.dylib"
compiler = "cc"
subprocess.run([
    compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-dynamiclib",
    "-Isrc/cpm/dcc_debug_host/include", "-o", str(adapter),
    "src/cpm/dcc_debug_io_adapter.c",
], cwd=root, check=True)

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
    adapter.unlink(missing_ok=True)
