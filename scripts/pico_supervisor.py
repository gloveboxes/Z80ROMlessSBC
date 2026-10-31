#!/usr/bin/env python3
"""Stage 9 USB supervisor commands; never open beside another serial owner."""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from typing import Callable, Optional

import serial
from serial.tools import list_ports
from serial.tools.list_ports_common import ListPortInfo


class FlashError(RuntimeError):
    pass


def pico_ports(device: Optional[str] = None) -> list[ListPortInfo]:
    return [
        port for port in list_ports.comports()
        if port.vid == 0x2E8A
        and (device is None or (port.serial_number or "").casefold() == device.casefold())
    ]


def resolve_device(
    port: Optional[str] = None, device: Optional[str] = None
) -> tuple[str, str]:
    candidates = [
        candidate for candidate in pico_ports(device)
        if port is None or candidate.device == port
    ]
    if len(candidates) != 1:
        raise FlashError(
            f"Expected one matching Pico serial device, found {len(candidates)}. "
            "Use ports to inspect devices, then select --serial or --port explicitly."
        )
    candidate = candidates[0]
    if not candidate.serial_number:
        raise FlashError("Pico has no USB serial number; cannot track it safely across reboot")
    return candidate.device, candidate.serial_number


def positive_seconds(value: str) -> float:
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("timeout must be a finite positive number")
    return seconds


def serial_owners(port: str) -> dict[int, str]:
    paths = [port]
    if port.startswith("/dev/cu."):
        paths.append(port.replace("/dev/cu.", "/dev/tty.", 1))
    result = subprocess.run(
        ["lsof", "-Fpc", *paths], capture_output=True, text=True, timeout=5, check=False
    )
    if result.returncode == 1 and not result.stdout.strip() and not result.stderr.strip():
        return {}
    if result.returncode not in (0, 1) or result.stderr.strip():
        raise FlashError(
            f"Cannot establish serial ownership (lsof exit {result.returncode}): "
            f"{result.stderr.strip()}"
        )
    owners: dict[int, str] = {}
    pid = None
    for line in result.stdout.splitlines():
        if line.startswith("p") and line[1:].isdigit():
            pid = int(line[1:])
            owners[pid] = ""
        elif line.startswith("c") and pid is not None:
            owners[pid] = line[1:]
        elif pid is not None and re.fullmatch(r"f\d+", line):
            continue
        else:
            raise FlashError(f"Unexpected lsof ownership record: {line!r}")
    if not owners or any(not name for name in owners.values()):
        raise FlashError("Incomplete serial ownership information")
    return owners


def require_free_port(port: str) -> None:
    owners = serial_owners(port)
    if owners:
        raise FlashError(f"Serial port is owned; close the terminal first: {owners}")


def disconnect_picocom(port: str) -> None:
    owners = serial_owners(port)
    if any(name != "picocom" for name in owners.values()):
        raise FlashError(f"Refusing to disconnect a non-picocom serial owner: {owners}")
    for pid in owners:
        process = subprocess.run(
            ["ps", "-p", str(pid), "-o", "comm="],
            capture_output=True, text=True, timeout=5, check=False,
        )
        if process.returncode or Path(process.stdout.strip()).name != "picocom":
            raise FlashError(f"Cannot verify picocom process {pid}; no signal sent")
        print(f"Disconnecting picocom PID {pid} with SIGTERM", flush=True)
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            print(f"Picocom PID {pid} already exited")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        remaining = serial_owners(port)
        if not remaining:
            return
        if remaining != owners:
            raise FlashError(f"Serial ownership changed while disconnecting: {remaining}")
        time.sleep(0.1)
    raise FlashError("Picocom did not release the port; no forced kill/reset performed")


def open_supervisor(port: str) -> serial.Serial:
    require_free_port(port)
    connection = serial.Serial(
        port=None, baudrate=115200, timeout=0.1, write_timeout=2,
        bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE, xonxoff=False, rtscts=False,
        dsrdtr=False, exclusive=True,
    )
    connection.dtr = True
    connection.rts = False
    connection.port = port
    try:
        connection.open()
    except (OSError, serial.SerialException):
        connection.close()
        raise
    return connection


def read_until(
    connection: serial.Serial, predicate: Callable[[str], bool], timeout: float
) -> str:
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        chunk = connection.read(max(1, min(connection.in_waiting, 4096)))
        if not chunk:
            continue
        decoded = chunk.decode("utf-8", errors="replace")
        print(decoded, end="", flush=True)
        text += decoded
        if len(text) > 131072:
            raise FlashError("Serial response exceeded 128 KiB; no success inferred")
        if "[diag] FAIL:" in text or "[boot] FAIL:" in text:
            raise FlashError("Firmware reported a failure; preserve the output above")
        if predicate(text):
            return text
    raise FlashError(
        f"Timed out after {timeout:g}s; expected response not observed. "
        "No automatic retry/reset was performed."
    )


def send_command(connection: serial.Serial, command: str) -> None:
    data = b"\x1d" + command.encode("ascii")
    if connection.write(data) != len(data):
        raise FlashError("Incomplete supervisor command write")
    connection.flush()


STATUS_LINE = re.compile(r"\[diag\] running=[^\r\n]*\r?\n")


def status(connection: serial.Serial, timeout: float) -> dict[str, str]:
    send_command(connection, "s")
    text = read_until(connection, lambda value: STATUS_LINE.search(value) is not None, timeout)
    match = STATUS_LINE.search(text)
    if match is None:
        raise FlashError("Missing supervisor status line")
    fields = dict(re.findall(r"(\w+)=(\S+)", match.group()))
    required = {"running", "verified", "disk", "fatal", "failure"}
    if not required.issubset(fields) or fields["running"] not in {"0", "1"}:
        raise FlashError("Incomplete or invalid Stage 9 supervisor status")
    return fields


def require_healthy(fields: dict[str, str]) -> None:
    expected = {
        "running": "1", "verified": "1", "BIOS": "1", "CCP_handoff": "1",
        "fatal": "0", "trap_timeouts": "0", "control_errors": "0",
        "boot_error": "none", "failure": "none",
    }
    failures = [
        f"{key}={fields.get(key, '<missing>')}"
        for key, value in expected.items() if fields.get(key) != value
    ]
    if failures:
        raise FlashError("Supervisor is not healthy: " + ", ".join(failures))


def stop(connection: serial.Serial, timeout: float) -> None:
    fields = status(connection, timeout)
    if fields["fatal"] != "0":
        raise FlashError("Fatal storage error; clean stop cannot be confirmed")
    if fields["running"] == "0":
        if fields["disk"] != "01" or fields["failure"] != "none":
            raise FlashError("CPU held but storage is not confirmed clean; inspect status/journal")
        print("PASS: supervisor reports CPU stopped and disk ready")
        return
    send_command(connection, "x")
    read_until(
        connection,
        lambda text: "Disk quiescent; RESET held LOW" in text,
        timeout,
    )
    print("\nPASS: clean stop/flush confirmed")


def wait_boot(connection: serial.Serial, timeout: float) -> None:
    read_until(
        connection,
        lambda text: re.search(r"(?:^|[\r\n])[A-D]>", text) is not None,
        timeout,
    )
    require_healthy(status(connection, timeout))
    print("PASS: observed CP/M prompt and healthy supervisor status")


def wait_port(device: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        candidates = pico_ports(device)
        if len(candidates) > 1:
            raise FlashError(f"Multiple serial devices match Pico serial {device}")
        if candidates:
            return candidates[0].device
        time.sleep(0.1)
    raise FlashError(f"USB serial for Pico {device} did not reappear; rediscover with ports")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("ports", "status", "help", "stop", "wait-boot"))
    parser.add_argument("--port", help="optional explicit device; otherwise discover a unique Pico")
    parser.add_argument("--serial", help="select Pico by USB serial number")
    parser.add_argument("--timeout", type=positive_seconds, default=30.0)
    args = parser.parse_args()
    if args.command == "ports":
        for port in list_ports.comports():
            print(f"{port.device}\t{port.description}\tserial={port.serial_number}")
        return 0
    try:
        if args.command == "wait-boot" and args.serial and not args.port:
            port = wait_port(args.serial, args.timeout)
        else:
            port, _ = resolve_device(args.port, args.serial)
        print(f"Using serial port: {port}", flush=True)
        with open_supervisor(port) as connection:
            # Allow the foreground USB connection transition before sending an escape.
            time.sleep(0.25)
            if args.command == "status":
                status(connection, args.timeout)
            elif args.command == "stop":
                stop(connection, args.timeout)
            elif args.command == "wait-boot":
                wait_boot(connection, args.timeout)
            else:
                send_command(connection, "h")
                read_until(
                    connection,
                    lambda text: re.search(
                        r"(?:running CPU\.|safely reboots into CP/M\.|"
                        r"confirm BIOS banner and A>\.)\r?\n", text
                    ) is not None,
                    args.timeout,
                )
    except (FlashError, OSError, serial.SerialException, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
