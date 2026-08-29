#!/usr/bin/env python3
"""Test native CP/M cold boot, DIR, and warm reload under DCC."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time


REPO_ROOT = Path(__file__).resolve().parents[2]
LOCAL_HOST = REPO_ROOT / "build" / "cpm" / "dcc_debug_host" / "dcc-debug-host"


class MISession:
    def __init__(self, host: Path, arguments: list[str]) -> None:
        self.process = subprocess.Popen(
            [str(host), "--interpreter=mi", *arguments],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.lines: queue.Queue[str] = queue.Queue()
        self.transcript: list[str] = []
        self.token = 1
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()
        self._wait(lambda line: line.strip() == "(gdb)")

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        for line in self.process.stdout:
            line = line.rstrip("\r\n")
            self.transcript.append(line)
            self.lines.put(line)

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for line in self.process.stderr:
            self.transcript.append("stderr: " + line.rstrip("\r\n"))

    def _wait(self, predicate, timeout: float = 30) -> str:
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=deadline - time.time())
            except queue.Empty:
                break
            if predicate(line):
                return line
        raise RuntimeError("MI timeout\n" + "\n".join(self.transcript[-100:]))

    def command(self, command: str, *, stop: bool = False) -> tuple[str, str] | str:
        token = self.token
        self.token += 1
        assert self.process.stdin is not None
        self.process.stdin.write(f"{token}{command}\n")
        self.process.stdin.flush()
        result = self._wait(lambda line: line.startswith(f"{token}^"))
        if "^error" in result:
            raise RuntimeError(result)
        if stop:
            return result, self._wait(lambda line: line.startswith("*stopped"))
        return result

    def target_text(self) -> str:
        return "".join(
            json.loads(line[1:])
            for line in self.transcript
            if line.startswith('@"')
        )

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                self.command("-gdb-exit")
            except (BrokenPipeError, RuntimeError):
                self.process.terminate()
        self.process.wait(timeout=5)


def parse_args() -> argparse.Namespace:
    default_dcc = REPO_ROOT.parent / "dcc"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dcc-root", type=Path, default=default_dcc)
    parser.add_argument("--host", type=Path)
    parser.add_argument("--assembler", default=shutil.which("z80asm"))
    parser.add_argument("--unoptimized", action="store_true")
    parser.add_argument("--first-pass-only", action="store_true")
    parser.add_argument("--git-revision")
    return parser.parse_args()


def build_adapter(dcc_root: Path, output: Path) -> None:
    del dcc_root
    cmake = shutil.which("cmake")
    if cmake is None:
        raise RuntimeError("cmake is required")
    source = REPO_ROOT / "src" / "cpm" / "dcc_io_adapter"
    build = REPO_ROOT / "build" / "cpm" / "dcc_io_adapter"
    subprocess.run(
        [cmake, "-S", str(source), "-B", str(build), "-DBUILD_TESTING=OFF"],
        check=True,
    )
    subprocess.run([cmake, "--build", str(build)], check=True)
    if sys.platform == "darwin":
        library = build / "libz80sbc-io-adapter.dylib"
    elif os.name == "nt":
        library = build / "z80sbc-io-adapter.dll"
    elif os.name == "posix":
        library = build / "libz80sbc-io-adapter.so"
    else:
        raise RuntimeError("adapter smoke test currently supports macOS and Linux")
    shutil.copy2(library, output)


def write_smoke_program(directory: Path) -> Path:
    source = directory / "smoke.c"
    program = directory / "SMOKE.COM"
    metadata = directory / "SMOKE.DBG"
    source.write_text("int main(void) { for (;;) {} }\n", encoding="ascii")
    program.write_bytes(bytes((0xC3, 0x00, 0x01)))
    metadata.write_text(
        "DCCDBG 2\n"
        'function-begin 0100 "_main" "main"\n'
        'line 0100 1 "smoke.c"\n'
        'function-end 0103 "_main" "main"\n',
        encoding="ascii",
    )
    return program


def resume_past_spurious_exit(
    session: MISession, stopped: str, max_resumes: int = 500
) -> str:
    """Resume past FullCpmHost's "exited-normally" checkpoint when it is not
    the specific transient-exit we are intentionally expecting.

    The DCC debug host's FullCpmHost model reports
    "*stopped,reason=exited-normally" whenever the emulated PC equals 0x0000
    or a `warm_boot_address_` value captured once from its own internal
    bootstrap fixture (see dcc_host_full_cpm.cpp::target_exited()). That
    address can coincidentally match a perfectly ordinary, correct
    instruction our own relocated CP/M image executes while doing legitimate
    work (for example a loop that legitimately iterates dozens of times
    while polling for console input during cold boot), long before any
    transient program has actually exited. When that coincidence happens the
    host stops the target once per loop iteration even though nothing is
    wrong; a single coincidental address can require tens of resumes before
    execution naturally moves past it. Simply resuming with another
    -exec-continue always lets the real (unaffected) CPU state keep running
    correctly; the generous max_resumes bound only guards against a genuine
    hang.
    """
    resumes = 0
    while 'reason="exited-normally"' in stopped and resumes < max_resumes:
        resumes += 1
        _, stopped = session.command("-exec-continue", stop=True)
    return stopped


def write_memory(session: MISession, image: bytes) -> None:
    chunk_size = 4096
    for address in range(0, len(image), chunk_size):
        chunk = image[address : address + chunk_size]
        session.command(f"-data-write-memory-bytes 0x{address:04x} {chunk.hex()}")


def main() -> None:
    args = parse_args()
    dcc_root = args.dcc_root.resolve()
    host = (args.host or LOCAL_HOST).resolve()
    if not host.is_file():
        raise SystemExit(f"DCC debug host not found: {host}")
    if args.assembler is None:
        raise SystemExit("z80asm was not found")

    sys.path.insert(0, str(Path(__file__).parent))
    import build_images

    with tempfile.TemporaryDirectory(prefix="z80sbc-dcc-host-") as temporary:
        directory = Path(temporary)
        output = directory / "images"
        subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("build_images.py")),
                "--assembler",
                args.assembler,
                "--output-dir",
                str(output),
            ],
            check=True,
        )
        if args.unoptimized or args.first_pass_only or args.git_revision:
            import disassemble_cpm64

            if args.git_revision:
                source = subprocess.check_output(
                    [
                        "git",
                        "show",
                        f"{args.git_revision}:src/cpm/cpm64_z80.asm",
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                )
            else:
                reference = build_images.load_cpm_system()
                instructions, targets = disassemble_cpm64.discover(reference)
                source = disassemble_cpm64.emit_z80_port(
                    reference,
                    instructions,
                    targets,
                    optimize=not args.unoptimized,
                    relocate=True,
                    deep_optimize=not args.first_pass_only,
                )
            system = build_images.assemble_cpm_source(args.assembler, source)
            bios = (output / "bios.bin").read_bytes()
            (output / "z80boot.img").write_bytes(
                build_images.build_z80_image(system, bios)
            )
            drive_a = bytearray(
                (output / "drive_a_cpm63k-z80.img").read_bytes()
            )
            drive_a[: len(system)] = system
            (output / "drive_a_cpm63k-z80.img").write_bytes(drive_a)
        extension = ".dylib" if sys.platform == "darwin" else ".so"
        adapter = directory / f"libz80sbc-dcc-adapter{extension}"
        build_adapter(dcc_root, adapter)

        drive_names = (
            "drive_a_cpm63k-z80.img",
            "drive_b_bdsc.img",
            "drive_c_escape.img",
            "drive_d_blank.img",
        )
        scratch_drive = directory / "drive_d_scratch.img"
        scratch_drive.write_bytes(
            bytes([0xE5]) * (output / drive_names[3]).stat().st_size
        )
        environment = directory / "z80sbc.env"
        environment.write_text(
            "".join(
                f"DRIVE_{chr(ord('A') + index)}="
                f"{scratch_drive if index == 3 else output / name}\n"
                for index, name in enumerate(drive_names)
            ),
            encoding="utf-8",
        )
        program = write_smoke_program(directory)
        package = (output / "z80boot.pkg").read_bytes()
        image = package[build_images.BOOT_PAYLOAD_OFFSET:]
        if len(image) != 65536:
            raise RuntimeError("generated boot image is not 64 KiB")

        host_arguments = [
            "--io-adapter", str(adapter),
            "--env-file", str(environment),
        ]
        if host == LOCAL_HOST.resolve():
            host_arguments.insert(0, "--direct-loader")
        session = MISession(host, host_arguments)
        try:
            session.command(f'-file-exec-and-symbols "{program}"')
            session.command("-break-insert -t *0x100")
            _, stopped = session.command("-exec-run", stop=True)
            if 'reason="breakpoint-hit"' not in stopped:
                raise RuntimeError(f"dummy target did not stop at 0x0100: {stopped}")
            write_memory(session, image)
            bios_base = int.from_bytes(image[1:3], byteorder="little")
            trampoline = bytes((0x3E, 0xA5, 0xD3, 0x15, 0xC3,
                                bios_base & 0xFF, bios_base >> 8))
            session.command(
                f"-data-write-memory-bytes 0x0100 {trampoline.hex()}"
            )
            session.command(f"-break-insert -i 1 *0x{bios_base:04x}")
            _, stopped = session.command("-exec-continue", stop=True)
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="breakpoint-hit"' in stopped and 'bkptno="2"' in stopped:
                page_zero = session.command("-data-read-memory-bytes 0x0000 16")
                stack = session.command("-data-read-memory-bytes 0xee00 96")
                registers = session.command("-data-list-register-values x")
                raise RuntimeError(
                    "SBC CP/M unexpectedly re-entered cold boot; "
                    f"page zero: {page_zero}; stack: {stack}; registers: {registers}"
                )
            if 'reason="end-stepping-range"' not in stopped:
                terminal = session.target_text()
                raise RuntimeError(
                    "SBC CP/M did not wait for console input; "
                    f"stop: {stopped}; terminal: {terminal!r}"
                )
            boot_text = session.target_text()
            if "64K CP/M 2.2 - Burcon Z80 Edition" not in boot_text or "A>" not in boot_text:
                raise RuntimeError("SBC CP/M banner or A> prompt was not observed")

            _, stopped = session.command(
                '-interpreter-exec console "input DIR"', stop=True
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"DIR did not return to console input: {stopped}")
            directory_text = session.target_text()[len(boot_text) :]
            if ("A>" not in directory_text or "DUMP" not in directory_text or
                    "BDOS ERR" in directory_text):
                raise RuntimeError(
                    f"DIR did not return the expected listing: {directory_text!r}"
                )

            before_warm_boot = session.target_text()
            # Run a real transient program (LS.COM) to its natural exit
            # instead of injecting a fabricated jump straight to
            # warm_boot_entry. A hand-crafted jump only proves the BIOS
            # warm_boot routine works when entered directly; it never
            # exercises the actual CP/M exit path a transient program takes
            # (RET/JP to the CP/M warm-boot vector at 0x0000), which is what
            # every real transient does and what previously hung under the
            # interactive PTY launcher.
            _, stopped = session.command(
                '-interpreter-exec console "input LS"', stop=True
            )
            # The DCC debug host's FullCpmHost model reports
            # "*stopped,reason=exited-normally" the instant the emulated PC
            # equals 0x0000 (or a coincidentally matching bootstrap address).
            # That is by design for its single-program run-to-completion
            # harness, and it can fire on perfectly ordinary code -- not just
            # this transient's real exit. Resuming with -exec-continue lets
            # the real (unaffected) CPU state, including the BIOS warm_boot
            # routine, keep running for real.
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"BIOS warm boot did not return: {stopped}")
            warm_boot_text = session.target_text()[len(before_warm_boot) :]
            if "A>" not in warm_boot_text or "BDOS ERR" in warm_boot_text:
                raise RuntimeError(
                    f"warm boot did not reload CP/M: {warm_boot_text!r}"
                )

            _, stopped = session.command(
                '-interpreter-exec console "input C:"', stop=True
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"drive C selection did not return: {stopped}")
            before_attnc11 = session.target_text()
            _, stopped = session.command(
                '-interpreter-exec console "input ATTNC11"', stop=True
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"ATTNC11 did not return to console input: {stopped}")
            attnc11_text = session.target_text()[len(before_attnc11) :]
            if "accuracy  13/13" not in attnc11_text or "C>" not in attnc11_text:
                raise RuntimeError(
                    f"ATTNC11 did not load its second extent: {attnc11_text!r}"
                )

            _, stopped = session.command(
                '-interpreter-exec console '
                '"input A:PIP D:ATTNC11.COM=C:ATTNC11.COM"',
                stop=True,
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"multi-track PIP copy did not return: {stopped}")
            _, stopped = session.command(
                '-interpreter-exec console '
                '"input A:PIP D:ATTN.WTS=C:ATTN.WTS"',
                stop=True,
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"weights-file PIP copy did not return: {stopped}")
            _, stopped = session.command(
                '-interpreter-exec console "input D:"', stop=True
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"drive D selection did not return: {stopped}")
            before_copy = session.target_text()
            _, stopped = session.command(
                '-interpreter-exec console "input ATTNC11"', stop=True
            )
            stopped = resume_past_spurious_exit(session, stopped)
            if 'reason="end-stepping-range"' not in stopped:
                raise RuntimeError(f"copied ATTNC11 did not return: {stopped}")
            copied_text = session.target_text()[len(before_copy) :]
            if "accuracy  10/10" not in copied_text or "D>" not in copied_text:
                raise RuntimeError(
                    f"multi-track copied ATTNC11 failed: {copied_text!r}"
                )
            variant = (
                f"source from {args.git_revision}"
                if args.git_revision
                else "unoptimized SBC CP/M image"
                if args.unoptimized
                else "first-pass-only SBC CP/M image"
                if args.first_pass_only
                else "optimized SBC CP/M image"
            )
            print(f"PASS: DCC debug host booted the {variant}")
            print("PASS: DIR completed through the SBC disk-port model")
            print("PASS: LS.COM ran to natural completion and BIOS warm boot")
            print("      reloaded CP/M and restored the A> prompt")
            print("PASS: multi-extent ATTNC11.COM completed on drive C")
            print("PASS: PIP copied ATTNC11.COM across track 6 and the copy ran")
        finally:
            session.close()


if __name__ == "__main__":
    main()