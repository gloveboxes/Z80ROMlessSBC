#!/usr/bin/env python3
"""Verified firmware or selected CP/M disk flashing with picotool."""

from __future__ import annotations

import argparse
import binascii
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from src.cpm import build_images as cpm_images
from src.disks.convert_altair_disks import SOURCE_GRID_BYTES, convert, validate_source

from pico_supervisor import (
    FlashError, disconnect_picocom, open_supervisor, positive_seconds,
    require_free_port, resolve_device, stop,
)

FLASH_BASE = 0x10000000
FIRMWARE_END = FLASH_BASE + 0x290000
JOURNAL_BASE = FLASH_BASE + 0x290000
ARM_S_FAMILY = 0xE48BFF59
DISK_ADDRESSES = {
    drive: FLASH_BASE + offset
    for drive, offset in zip("ABCD", cpm_images.FLASH_DISK_OFFSETS)
}


def validate_firmware(path: Path) -> None:
    data = path.read_bytes()
    if not data or len(data) % 512:
        raise FlashError("Firmware must be a nonempty, complete UF2 file")
    addresses: list[int] = []
    numbers: list[int] = []
    totals: set[int] = set()
    ignored = 0
    for offset in range(0, len(data), 512):
        block = data[offset:offset + 512]
        magic0, magic1, flags, address, size, number, total, family = struct.unpack_from(
            "<8I", block
        )
        if (magic0, magic1, struct.unpack_from("<I", block, 508)[0]) != (
            0x0A324655, 0x9E5D5157, 0x0AB16F30
        ):
            raise FlashError(f"Invalid UF2 magic at block {offset // 512}")
        # SDK RP2350 UF2s prepend a non-programmed absolute-family ignore block.
        if (
            flags == 0xA000 and family == 0xE48BFF57 and address == 0x10FFFF00
            and size == 256 and number == 0 and total == 2
            and struct.unpack_from("<I", block, 288)[0] == 0x9957E304
            and block[292:508] == bytes(216)
        ):
            ignored += 1
            if ignored > 1:
                raise FlashError("Duplicate SDK ignore block")
            continue
        if flags != 0x2000 or family != ARM_S_FAMILY or size != 256:
            raise FlashError("Only RP2350 ARM Secure firmware UF2 payloads are accepted")
        if address % 256 or not FLASH_BASE <= address < address + size <= FIRMWARE_END:
            raise FlashError(f"UF2 writes outside firmware region at {address:#x}")
        addresses.append(address)
        numbers.append(number)
        totals.add(total)
    if not addresses or totals != {len(addresses)}:
        raise FlashError("UF2 payload block count mismatch")
    if sorted(numbers) != list(range(len(numbers))):
        raise FlashError("UF2 has missing or duplicate block numbers")
    if sorted(addresses) != list(range(FLASH_BASE, FLASH_BASE + len(addresses) * 256, 256)):
        raise FlashError("UF2 firmware must start at flash base with no gaps or overlaps")
    print(f"Firmware: {path} SHA256={hashlib.sha256(data).hexdigest()}")
    print(f"Validated firmware range: {FLASH_BASE:#x}..{max(addresses) + 256:#x}")


def picotool(arguments: list[str], device: str) -> None:
    # picotool requires serial selection before the force/reboot options.
    force = [argument for argument in arguments if argument in ("-f", "-F")]
    options = [argument for argument in arguments if argument not in ("-f", "-F")]
    command = ["picotool", *options, "--ser", device, *force]
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
    for line in (result.stdout + result.stderr).splitlines():
        if line.strip() and (
            not line.startswith(("Loading into Flash:", "Verifying Flash:")) or "100%" in line
        ):
            print(line)
    if result.returncode:
        raise FlashError(
            f"picotool failed ({result.returncode}); no automatic reboot/retry. "
            "Inspect device state before proceeding."
        )


def check_journal(device: str) -> None:
    with tempfile.TemporaryDirectory(prefix="z80-journal-") as directory:
        for pair in range(8):
            address = JOURNAL_BASE + pair * 8192
            path = Path(directory) / f"header-{pair}.bin"
            picotool(
                ["save", "-r", hex(address), hex(address + 256), str(path), "-t", "bin"],
                device,
            )
            if path.read_bytes() != b"\xff" * 256:
                raise FlashError(
                    f"Journal pair {pair} is not erased; recover/investigate before writing. "
                    "Do not erase the journal."
                )
    print("PASS: all eight journal headers erased; no pending replay")


def prepare_target(args: argparse.Namespace) -> str:
    if not args.power_confirmed:
        raise FlashError("Require --power-confirmed: external +5 V must remain applied")
    if args.bootsel:
        if not args.stopped:
            raise FlashError("Already in BOOTSEL: require --stopped to confirm the prior clean stop")
        if not args.serial:
            raise FlashError("BOOTSEL requires explicit --serial to select the intended Pico")
        return args.serial
    port, device = resolve_device(args.port, args.serial)
    print(f"Using Pico {device} on {port}", flush=True)
    if not args.no_disconnect:
        disconnect_picocom(port)
    require_free_port(port)
    if not args.stopped and not getattr(args, "force", False):
        with open_supervisor(port) as connection:
            time.sleep(0.25)
            stop(connection, args.timeout)
    return device


def prepare_storage(args: argparse.Namespace) -> str:
    device = prepare_target(args)
    picotool(["info"] if args.bootsel else ["info", "-F"], device)
    check_journal(device)
    return device


def drive_image(value: str) -> tuple[str, Path]:
    drive, separator, source = value.partition("=")
    drive = drive.upper()
    if drive not in DISK_ADDRESSES or not separator or not source:
        raise argparse.ArgumentTypeError("use DRIVE=IMAGE, with DRIVE A, B, C or D")
    return drive, Path(source).expanduser()


def load_disk_image(drive: str, source: Path) -> bytes:
    size = source.stat().st_size
    if size not in {cpm_images.DISK_IMAGE_BYTES, SOURCE_GRID_BYTES, SOURCE_GRID_BYTES + 96}:
        raise FlashError(
            f"{source}: unexpected disk size {size}; expected {cpm_images.DISK_IMAGE_BYTES} "
            "native bytes or a complete framed Altair image"
        )
    data = source.read_bytes()
    if size != cpm_images.DISK_IMAGE_BYTES:
        if drive == "A":
            raise FlashError("Drive A requires a matching board-native image, not a framed Altair disk")
        try:
            validate_source(data)
            data = convert(data)
        except ValueError as error:
            raise FlashError(f"{source}: {error}") from error
    if len(data) != cpm_images.DISK_IMAGE_BYTES:
        raise FlashError(f"{source}: native disk must contain exactly {cpm_images.DISK_IMAGE_BYTES} bytes")
    return data


def check_drive_a(image: bytes, device: str, directory: Path) -> None:
    address = FLASH_BASE + cpm_images.FLASH_BOOT_OFFSET
    size = cpm_images.BOOT_PAYLOAD_OFFSET + cpm_images.Z80_IMAGE_BYTES
    path = directory / "installed-boot.pkg"
    picotool(["save", "-r", hex(address), hex(address + size), str(path), "-t", "bin"], device)
    package = path.read_bytes()
    if len(package) != size:
        raise FlashError("Short installed boot-package read; Drive A compatibility not confirmed")
    magic, version, header_bytes, image_bytes, image_crc, header_crc = struct.unpack_from(
        "<IHHIII", package
    )
    if (
        (magic, version, header_bytes, image_bytes) != (
            cpm_images.BOOT_MAGIC, cpm_images.BOOT_VERSION,
            cpm_images.BOOT_HEADER_BYTES, cpm_images.Z80_IMAGE_BYTES,
        )
        or binascii.crc32(package[:16]) & 0xFFFFFFFF != header_crc
    ):
        raise FlashError("Invalid installed boot manifest; Drive A compatibility not confirmed")
    memory = package[cpm_images.BOOT_PAYLOAD_OFFSET:]
    if (
        binascii.crc32(memory) & 0xFFFFFFFF != image_crc
        or memory[:3] != bytes((0xC3, cpm_images.BIOS_BASE & 0xFF, cpm_images.BIOS_BASE >> 8))
    ):
        raise FlashError("Invalid installed boot image; Drive A compatibility not confirmed")
    # The native BIOS reloads these sequential CCP/BDOS records on warm boot.
    if image[:cpm_images.CPM_SYSTEM_BYTES] != memory[cpm_images.CCP_BASE:cpm_images.BIOS_BASE]:
        raise FlashError(
            "Drive A warm-boot records do not match the installed boot package; "
            "use a matching board-native image"
        )
    print("PASS: Drive A warm-boot records match the installed boot package")


def flash_disks(args: argparse.Namespace) -> None:
    if (
        not args.drive or any(drive not in DISK_ADDRESSES for drive, _ in args.drive)
        or len({drive for drive, _ in args.drive}) != len(args.drive)
    ):
        raise FlashError("Select at least one A-D drive, with no duplicate drive assignments")
    with tempfile.TemporaryDirectory(prefix="z80-disks-") as directory:
        temporary = Path(directory)
        images: list[tuple[str, Path, bytes]] = []
        for drive, source in args.drive:
            data = load_disk_image(drive, source)
            path = temporary / f"drive_{drive.lower()}.img"
            path.write_bytes(data)
            images.append((drive, path, data))
            print(f"Drive {drive} <- {source}: {len(data)} bytes SHA256={hashlib.sha256(data).hexdigest()}")
        print("No backups: only selected disk slots will be written")
        device = prepare_storage(args)
        for drive, _, data in images:
            if drive == "A":
                check_drive_a(data, device, temporary)
        for drive, path, _ in images:
            picotool(
                ["load", "-v", str(path), "-t", "bin", "-o", hex(DISK_ADDRESSES[drive])],
                device,
            )
            print(f"PASS: Drive {drive} write and read-back verification")
        print("PASS: selected disks verified; firmware, boot, journal, ENV and unselected drives not written")
        if args.no_reboot:
            print("Device left in BOOTSEL")
            return
        picotool(["reboot"], device)
        print("Reboot requested; disk flashing complete. Serial port released.")


def flash_firmware(args: argparse.Namespace) -> None:
    validate_firmware(args.image)
    device = prepare_target(args)
    load = ["load", "-v"]
    if not args.bootsel:
        load.append("-F")
    picotool([*load, str(args.image)], device)
    print("PASS: firmware write and read-back verification; protected storage not written")
    if args.no_reboot:
        print("Device left in BOOTSEL")
        return
    picotool(["reboot"], device)
    print("Reboot requested; flashing complete. Serial port released.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    firmware = commands.add_parser(
        "firmware", help="write only a bounded firmware UF2",
        description="Verify programmed firmware bytes; do not inspect what boots afterward.",
    )
    firmware.add_argument("image", type=Path)
    firmware.add_argument("--no-reboot", action="store_true")
    firmware.add_argument(
        "--force", action="store_true",
        help="skip the Phase 9 stop/flush handshake and force picotool into BOOTSEL",
    )
    disks = commands.add_parser(
        "disks", help="flash one or more selected disk slots without backups",
        description="Validate/convert disk images, stop/flush, check journal and verify selected writes.",
    )
    disks.add_argument(
        "--drive", action="append", type=drive_image, required=True,
        help="repeat DRIVE=IMAGE for each selected A-D slot; native images or Altair B-D sources",
    )
    disks.add_argument("--no-reboot", action="store_true")
    disks.add_argument(
        "--force", action="store_true",
        help="skip the running firmware stop/flush handshake; retain journal and image checks",
    )
    prepare = commands.add_parser(
        "prepare", help="disconnect picocom, stop/flush, enter BOOTSEL and check journal; no writes"
    )
    for target in (firmware, disks, prepare):
        target.add_argument("--serial", help="select Pico serial; auto-discover if uniquely connected")
        target.add_argument("--port", help="optional explicit port; otherwise discover by Pico serial")
        target.add_argument(
            "--stopped", action="store_true",
            help="operator already confirmed clean stop; skip the default stop/flush command",
        )
        target.add_argument(
            "--no-disconnect", action="store_true",
            help="refuse a busy serial port instead of gracefully disconnecting picocom",
        )
        target.add_argument("--power-confirmed", action="store_true", help="external +5 V maintained")
        target.add_argument("--bootsel", action="store_true", help="already in BOOTSEL, do not force")
        target.add_argument("--timeout", type=positive_seconds, default=60.0)
    journal = commands.add_parser("journal", help="check all eight headers; device already in BOOTSEL")
    journal.add_argument("--serial", required=True)
    args = parser.parse_args()
    try:
        if args.command == "journal":
            check_journal(args.serial)
        elif args.command == "prepare":
            device = prepare_storage(args)
            print(f"Ready for selective disk writes; Pico {device} left in BOOTSEL")
        elif args.command == "disks":
            flash_disks(args)
        else:
            flash_firmware(args)
    except (FlashError, OSError, serial.SerialException, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
