#!/usr/bin/env python3
from pathlib import Path
import queue
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src/cpm"))
from test_dcc_debug_host import MISession, write_memory

session_dir = Path(__file__).resolve().parent
host = ROOT / "build/cpm/dcc_debug_host/dcc-debug-host"
if sys.platform == "darwin":
    adapter_name = "libz80sbc-io-adapter.dylib"
elif sys.platform == "win32":
    adapter_name = "z80sbc-io-adapter.dll"
else:
    adapter_name = "libz80sbc-io-adapter.so"
adapter = ROOT / "build/cpm/dcc_io_adapter" / adapter_name
environment = session_dir / "session.env"
endpoint = session_dir / "terminal.endpoint"
if not adapter.is_file():
    raise SystemExit(f"project I/O adapter was not built: {adapter}")
package = (ROOT / "build/cpm/z80boot.pkg").read_bytes()
sys.path.insert(0, str(ROOT / "src/cpm"))
import build_images
image = package[build_images.BOOT_PAYLOAD_OFFSET:]

with tempfile.TemporaryDirectory(prefix="z80sbc-interactive-") as directory_name:
    directory = Path(directory_name)
    environment = directory / "session.env"
    adapter_assets = ROOT / "src/cpm/dcc_io_adapter/assets"
    local_environment = adapter_assets / "altair_env.txt"
    default_environment = adapter_assets / "altair_env.example.txt"
    adapter_environment = (
        local_environment if local_environment.is_file() else default_environment
    ).read_text(encoding="ascii")
    if adapter_environment and not adapter_environment.endswith("\n"):
        adapter_environment += "\n"
    program = directory / "loader.COM"
    program.write_bytes(bytes((0xC3, 0x00, 0x01)))
    (directory / "loader.dbg").write_text(
        "DCCDBG 2\n"
        'function-begin 0100 "_main" "main"\n'
        'line 0100 1 "loader.c"\n'
        'function-end 0103 "_main" "main"\n',
        encoding="ascii",
    )
    environment.write_text(
        adapter_environment
        + "".join(
            f"DRIVE_{letter}={ROOT / 'build/cpm' / filename}\n"
            for letter, filename in zip(
                "ABCD",
                (
                    "drive_a_cpm63k-z80.img",
                    "drive_b_bdsc.img",
                    "drive_c_escape.img",
                    "drive_d_blank.img",
                ),
            )
        ),
        encoding="ascii",
    )

    session = MISession(host, [
        "--direct-loader",
        "--io-adapter", str(adapter),
        "--env-file", str(environment),
        "--terminal-endpoint-file", str(endpoint),
    ])
    try:
        session.command(f'-file-exec-and-symbols "{program}"')
        session.command("-break-insert -t *0x0100")
        _, stopped = session.command("-exec-run", stop=True)
        if 'reason="breakpoint-hit"' not in stopped:
            raise RuntimeError(f"loader did not stop at 0x0100: {stopped}")
        write_memory(session, image)
        bios = int.from_bytes(image[1:3], byteorder="little")
        trampoline = bytes((0x3E, 0xA5, 0xD3, 0x15, 0xC3, bios & 0xFF, bios >> 8))
        session.command(f"-data-write-memory-bytes 0x0100 {trampoline.hex()}")
        session.command("-exec-continue")

        # The host's normal-exit checkpoint can coincide with a real warm boot.
        while session.process.poll() is None:
            try:
                line = session.lines.get(timeout=1.0)
            except queue.Empty:
                continue
            if line.startswith("*stopped") and 'reason="exited-normally"' in line:
                session.command("-exec-continue")
    finally:
        session.close()
