#!/usr/bin/env python3
"""Build FT.ASM with CP/M MAC/LOAD, then exercise HTTP(S) and MBASIC.

Requires the project z80-debug-host, curl-enabled native adapter, and cpmtools.
All disk writes use a dedicated generated image set; no Pico is accessed.
"""
from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import json
import subprocess
import sys
import threading
import urllib.request
from pathlib import Path

import build_images as images
from test_z80_debug_host import MISession, write_memory, write_smoke_program

ROOT = Path(__file__).resolve().parents[2]
LOVE_BASE = "https://raw.githubusercontent.com/AzureSphereCloudEnabledAltair8800/RetroGames/refs/heads/main/"
BINARY = bytes(range(256)) * 257 + b"\x00\xffEND"
MENU = b"10 REM HTTP text fixture\n20 END\r\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ft-source", type=Path,
                        default=ROOT.parent / "esp32/esp32-altair-8800/Apps/FT/FT.ASM")
    parser.add_argument("--host", type=Path, default=ROOT / "build/cpm/z80_debug_host/z80-debug-host")
    parser.add_argument("--adapter", type=Path, default=ROOT / "build/cpm/dcc_io_adapter" /
                        ("libz80sbc-io-adapter.dylib" if sys.platform == "darwin" else "libz80sbc-io-adapter.so"))
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/cpm-http-ft")
    parser.add_argument("--https", action="store_true", help="also download GitHub LOVE.BAS and run MBASIC")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = args.ft_source.read_bytes()
    (output / "FT.ASM").write_bytes(source)
    disk_dir = output / "images"
    subprocess.run([sys.executable, str(ROOT / "src/cpm/build_images.py"),
                    "--output-dir", str(disk_dir)], check=True)
    drive_names = ("drive_a_cpm63k-z80.img", "drive_b_bdsc.img",
                   "drive_c_escape.img", "drive_d_blank.img")
    disks = [disk_dir / name for name in drive_names]
    (output / "diskdefs").write_text(images.cpm_diskdefs())

    def copy_out(drive: int, name: str) -> bytes:
        run("SYNC")
        path = output / name
        subprocess.run(["cpmcp", "-f", "z80sbc", str(disks[drive]), f"0:{name}", str(path)],
                       cwd=output, check=True)
        return path.read_bytes()

    # MAC expects CP/M text records; retain the original source alongside the
    # test's line-ending conversion, without changing any assembly instructions.
    cpm_source = output / "FTCPM.ASM"
    cpm_source.write_bytes(source.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n") + b"\x1a")
    original_files = subprocess.check_output(
        ["cpmls", "-f", "z80sbc", str(disks[3])], cwd=output, text=True).splitlines()[1:]
    subprocess.run(["cpmcp", "-f", "z80sbc", str(disks[3]), str(cpm_source), "0:FT.ASM"],
                   cwd=output, check=True)
    subprocess.run(["cpmrm", "-f", "z80sbc", str(disks[3]),
                    *("0:" + name for name in original_files if name)], cwd=output, check=True)
    # Flush the modeled device before inspecting its image from a second
    # process. CP/M returns to CCP without necessarily performing a warm boot.
    sync_program = output / "SYNC.COM"
    sync_program.write_bytes(bytes.fromhex("3e05d310c9"))  # LD A,5; OUT (16),A; RET
    subprocess.run(["cpmcp", "-f", "z80sbc", str(disks[3]), str(sync_program), "0:SYNC.COM"],
                   cwd=output, check=True)
    served = output / "server"
    served.mkdir(exist_ok=True)
    (served / "BINARY.BIN").write_bytes(BINARY)
    (served / "EMPTY.BIN").write_bytes(b"")
    (served / "MENU.BAS").write_bytes(MENU)
    (served / "MiXeD.Bas").write_bytes(b"10 PRINT \"case preserved\"\n20 END\n")

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), functools.partial(Handler, directory=str(served)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}/"
    env_path = output / "test.env"
    env_path.write_text("".join(f"DRIVE_{chr(65+i)}={disk}\n" for i, disk in enumerate(disks)) +
                        f"HTTP_BASE_URL={base}\n")
    smoke = write_smoke_program(output)
    session = MISession(args.host.resolve(), ["--direct-loader", "--io-adapter", str(args.adapter.resolve()),
                                             "--env-file", str(env_path)])
    results = {"source_sha256": hashlib.sha256(source).hexdigest(), "checks": []}

    def run(text: str, expected: str = "D>") -> str:
        before = len(session.target_text())
        _, stopped = session.command(f'-interpreter-exec console "input {text}"', stop=True)
        for _ in range(1000):
            result = session.target_text()[before:]
            if expected in result and 'reason="end-stepping-range"' in stopped:
                return result
            _, stopped = session.command("-exec-continue", stop=True)
        raise RuntimeError(f"command did not finish: {text}: {stopped}\n{session.target_text()[before:]}")

    def run_prompted(filename: str, expected: str) -> str:
        before = len(session.target_text())
        session.command('-interpreter-exec console "input ft -g"', stop=True)
        for _ in range(1000):
            output_text = session.target_text()[before:]
            if "Remote filename/path:" in output_text:
                break
            _, stopped = session.command("-exec-continue", stop=True)
        else:
            raise RuntimeError("FT did not prompt for the remote filename")
        _, stopped = session.command(f'-interpreter-exec console "input {filename}"', stop=True)
        for _ in range(1000):
            output_text = session.target_text()[before:]
            if expected in output_text and "reason=\"end-stepping-range\"" in stopped:
                return output_text
            _, stopped = session.command("-exec-continue", stop=True)
        raise RuntimeError(f"prompted FT download did not finish: {filename}\n{output_text}")

    def check_file(name: str, expected: bytes) -> None:
        actual = copy_out(3, name)
        padded = expected + b"\0" * (-len(expected) % 128)
        assert actual == padded, f"{name}: {len(actual)} bytes differs from expected {len(padded)}"
        results["checks"].append({"file": name, "payload_bytes": len(expected),
                                  "cpm_bytes": len(actual), "sha256": hashlib.sha256(actual).hexdigest()})
        print(f"PASS: CP/M {name}: all {len(expected)} bytes and record padding match", flush=True)

    try:
        session.command(f'-file-exec-and-symbols "{smoke}"')
        session.command("-break-insert -t *0x100")
        session.command("-exec-run", stop=True)
        image = (disk_dir / "z80boot.pkg").read_bytes()[images.BOOT_PAYLOAD_OFFSET:]
        write_memory(session, image)
        bios = int.from_bytes(image[1:3], "little")
        trampoline = bytes((0x3e, 0xa5, 0xd3, 0x15, 0xc3, bios & 255, bios >> 8))
        session.command(f"-data-write-memory-bytes 0x100 {trampoline.hex()}")
        _, stopped = session.command("-exec-continue", stop=True)
        for _ in range(1000):
            if "A>" in session.target_text() and 'reason="end-stepping-range"' in stopped:
                break
            _, stopped = session.command("-exec-continue", stop=True)
        assert "64K CP/M 2.2 - Burcon Z80 Edition" in session.target_text()
        run("D:")
        print(run("DIR"), flush=True)
        text = run("B:MAC FT")
        print(text, flush=True)
        assert "ERROR" not in text.upper() and "NO SOURCE FILE" not in text.upper(), text
        text = run("B:LOAD FT")
        assert "ERROR" not in text.upper(), text
        program = copy_out(3, "FT.COM")
        assert len(program) > 1000
        results["ft_com_sha256"] = hashlib.sha256(program).hexdigest()
        print("PASS: Altair project FT.ASM assembled by MAC and linked by LOAD under CP/M", flush=True)
        text = run("FT -GB BINARY.BIN")
        assert f"Done ({len(BINARY)} bytes)" in text, text
        check_file("BINARY.BIN", BINARY)
        assert "Done (0 bytes)" in run("FT -GB EMPTY.BIN")
        check_file("EMPTY.BIN", b"")
        assert "Failed - file not found or server error" in run("FT -G MISSING.BAS")
        run("SYNC")
        listing = subprocess.check_output(["cpmls", "-f", "z80sbc", str(disks[3])], cwd=output, text=True)
        assert "missing.bas" not in listing.lower()
        print("PASS: HTTP 404 removes the partial CP/M file", flush=True)
        # CLOSE/new filename after error must recover on the next transfer.
        assert "Done (0 bytes)" in run("FT -GB EMPTY.BIN")
        text = run("FT -G MENU.BAS")
        assert f"Done ({len(MENU)} bytes)" in text, text
        check_file("MENU.BAS", MENU.replace(b"\r", b"").replace(b"\n", b"\r\n") + b"\x1a")
        mixed = b'10 PRINT "case preserved"\n20 END\n'
        text = run_prompted("MiXeD.Bas", f"Done ({len(mixed)} bytes)")
        assert "Downloading 'MiXeD.Bas'" in text, text
        check_file("MIXED.BAS", mixed.replace(b"\r", b"").replace(b"\n", b"\r\n") + b"\x1a")
        configured_env = env_path.read_text()
        env_path.write_text("".join(line for line in configured_env.splitlines(keepends=True)
                                    if not line.startswith("HTTP_BASE_URL=")))
        text = run("FT -GB EMPTY.BIN")
        assert "Missing HTTP_BASE_URL in ENV." in text, text
        assert "Run ENV, choose A to add or its number to edit." in text, text
        env_path.write_text(configured_env)
        assert "Done (0 bytes)" in run("FT -GB EMPTY.BIN")
        if args.https:
            with urllib.request.urlopen(LOVE_BASE + "LOVE.BAS", timeout=20) as response:
                love = response.read()
            env_path.write_text(env_path.read_text().replace(f"HTTP_BASE_URL={base}", f"HTTP_BASE_URL={LOVE_BASE}"))
            text = run("FT -G LOVE.BAS")
            assert f"Done ({len(love)} bytes)" in text, text
            text_bytes = love.replace(b"\r", b"").replace(b"\n", b"\r\n") + b"\x1a"
            check_file("LOVE.BAS", text_bytes)
            text = run("A:MBASIC LOVE", "YOUR MESSAGE, PLEASE")
            assert "SYNTAX ERROR" not in text.upper(), text
            text = run("LOVE", "Ok")
            assert "SYNTAX ERROR" not in text.upper() and "FILE NOT FOUND" not in text.upper(), text
            assert "LOVELOVE" in text, text
            run("SYSTEM")
            print("PASS: HTTPS LOVE.BAS loads and executes in MBASIC", flush=True)
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    finally:
        (output / "transcript.mi").write_text("\n".join(session.transcript) + "\n")
        (output / "transcript.txt").write_text(session.target_text())
        session.close()
        server.shutdown()
        server.server_close()
        thread.join()
    print(f"PASS: CP/M FT integration; artifacts in {output}")


if __name__ == "__main__":
    main()
