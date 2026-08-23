#!/usr/bin/env python3
"""Build the native CP/M boot package, disks, and optional flash image."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "src" / "disks"))

from geometry import (  # noqa: E402
    DPB_AL0,
    DPB_AL1,
    DPB_BLM,
    DPB_BSH,
    DPB_CKS,
    DPB_DRM,
    DPB_DSM,
    DPB_EXM,
    DRIVE_COUNT,
    IMAGE_BYTES as DISK_IMAGE_BYTES,
    RECORD_BYTES,
    RESERVED_TRACKS,
    SECTORS_PER_TRACK,
    SYSTEM_BYTES,
    TRACKS,
)


CCP_BASE = 0xDF00
BDOS_ENTRY = 0xE706
BIOS_BASE = 0xF500
CPM_SYSTEM_RECORD_FIRST = 3
CPM_SYSTEM_RECORDS = 44
CPM_SYSTEM_BYTES = CPM_SYSTEM_RECORDS * RECORD_BYTES
CPM_SYSTEM_SHA256 = (
    "0bae910ea014d3b36442e3b76fa7754731faafe77be9d90f695e1e8bb6592e7e"
)

SOURCE_TRACKS = 77
SOURCE_SECTOR_BYTES = 137
SOURCE_RECORD_OFFSET = 3
SOURCE_GRID_BYTES = SOURCE_TRACKS * SECTORS_PER_TRACK * SOURCE_SECTOR_BYTES

BOOT_MAGIC = 0x5442385A
BOOT_VERSION = 1
BOOT_HEADER_BYTES = 20
BOOT_PAYLOAD_OFFSET = 0x1000
Z80_IMAGE_BYTES = 65536

FLASH_BYTES = 4 * 1024 * 1024
FLASH_LINK_LIMIT = 0x290000
FLASH_JOURNAL_OFFSET = 0x290000
FLASH_JOURNAL_BYTES = 0x10000
FLASH_BOOT_OFFSET = 0x2A0000
FLASH_BOOT_BYTES = 0x20000
FLASH_DISK_OFFSETS = (0x2C0000, 0x310000, 0x360000, 0x3B0000)

DISK_NAMES = (
    "drive_a_cpm63k.img",
    "drive_b_bdsc.img",
    "drive_c_escape.img",
    "drive_d_blank.img",
)

assert FLASH_LINK_LIMIT == FLASH_JOURNAL_OFFSET
assert FLASH_JOURNAL_OFFSET + FLASH_JOURNAL_BYTES == FLASH_BOOT_OFFSET
assert FLASH_BOOT_OFFSET + FLASH_BOOT_BYTES == FLASH_DISK_OFFSETS[0]
assert all(
    left + DISK_IMAGE_BYTES == right
    for left, right in zip(FLASH_DISK_OFFSETS, FLASH_DISK_OFFSETS[1:])
)
assert FLASH_DISK_OFFSETS[-1] + DISK_IMAGE_BYTES == FLASH_BYTES


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extract_cpm_system(source: bytes) -> bytes:
    if len(source) not in {SOURCE_GRID_BYTES, SOURCE_GRID_BYTES + 96}:
        raise ValueError(f"unexpected CP/M source size: {len(source)}")

    records = []
    for record in range(
        CPM_SYSTEM_RECORD_FIRST,
        CPM_SYSTEM_RECORD_FIRST + CPM_SYSTEM_RECORDS,
    ):
        offset = record * SOURCE_SECTOR_BYTES + SOURCE_RECORD_OFFSET
        records.append(source[offset : offset + RECORD_BYTES])
    system = b"".join(records)
    if sha256(system) != CPM_SYSTEM_SHA256:
        raise ValueError("CP/M CCP/BDOS fingerprint does not match cpm63k.dsk")
    return system


def assembler_definitions() -> str:
    definitions = {
        "SYSTEM_RECORDS": CPM_SYSTEM_RECORDS,
        "DRIVE_COUNT": DRIVE_COUNT,
        "DPB_SPT": SECTORS_PER_TRACK,
        "DPB_BSH": DPB_BSH,
        "DPB_BLM": DPB_BLM,
        "DPB_EXM": DPB_EXM,
        "DPB_DSM": DPB_DSM,
        "DPB_DRM": DPB_DRM,
        "DPB_AL0": DPB_AL0,
        "DPB_AL1": DPB_AL1,
        "DPB_CKS": DPB_CKS,
        "DPB_OFF": RESERVED_TRACKS,
    }
    return "".join(
        f"{name}: equ {value}\n" for name, value in definitions.items()
    )


def build_bios(assembler: str) -> bytes:
    source_path = REPO_ROOT / "src" / "cpm" / "bios.asm"
    with tempfile.TemporaryDirectory(prefix="z80sbc-cpm-") as directory:
        temporary = Path(directory)
        combined_path = temporary / "bios.asm"
        binary_path = temporary / "bios.bin"
        combined_path.write_text(
            assembler_definitions() + source_path.read_text(encoding="ascii"),
            encoding="ascii",
        )
        subprocess.run(
            [assembler, "-o", str(binary_path), str(combined_path)],
            check=True,
        )
        bios = binary_path.read_bytes()

    if not bios or bios[0] != 0xC3:
        raise ValueError("assembled BIOS does not start with JP")
    if BIOS_BASE + len(bios) > Z80_IMAGE_BYTES:
        raise ValueError("assembled BIOS exceeds Z80 address space")
    if CPM_SYSTEM_BYTES + len(bios) > SYSTEM_BYTES:
        raise ValueError("CCP, BDOS, and BIOS exceed the reserved tracks")
    return bios


def build_z80_image(cpm_system: bytes, bios: bytes) -> bytes:
    if CCP_BASE + len(cpm_system) != BIOS_BASE:
        raise ValueError("CP/M system does not end at the BIOS base")

    image = bytearray(Z80_IMAGE_BYTES)
    image[0:3] = bytes((0xC3, BIOS_BASE & 0xFF, BIOS_BASE >> 8))
    image[CCP_BASE:BIOS_BASE] = cpm_system
    image[BIOS_BASE : BIOS_BASE + len(bios)] = bios
    return bytes(image)


def build_boot_package(image: bytes) -> bytes:
    image_crc = binascii.crc32(image) & 0xFFFFFFFF
    header = struct.pack(
        "<IHHII",
        BOOT_MAGIC,
        BOOT_VERSION,
        BOOT_HEADER_BYTES,
        len(image),
        image_crc,
    )
    header += struct.pack("<I", binascii.crc32(header) & 0xFFFFFFFF)
    return header + bytes([0xFF]) * (BOOT_PAYLOAD_OFFSET - len(header)) + image


def build_native_drive(source: bytes, cpm_system: bytes, bios: bytes) -> bytes:
    if len(source) != DISK_IMAGE_BYTES:
        raise ValueError(f"drive A must be {DISK_IMAGE_BYTES} bytes")
    system_tracks = cpm_system + bios
    system_tracks += bytes([0xE5]) * (SYSTEM_BYTES - len(system_tracks))
    return system_tracks + source[SYSTEM_BYTES:]


def build_flash_image(firmware: bytes, package: bytes, disks: list[bytes]) -> bytes:
    if not firmware or len(firmware) > FLASH_LINK_LIMIT:
        raise ValueError("firmware binary is empty or crosses the flash link limit")
    if len(package) > FLASH_BOOT_BYTES:
        raise ValueError("boot package exceeds its flash region")

    image = bytearray([0xFF]) * FLASH_BYTES
    image[: len(firmware)] = firmware
    image[FLASH_BOOT_OFFSET : FLASH_BOOT_OFFSET + len(package)] = package
    for offset, disk in zip(FLASH_DISK_OFFSETS, disks, strict=True):
        image[offset : offset + len(disk)] = disk
    return bytes(image)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--assembler",
        default=shutil.which("z80asm"),
        help="z80asm executable (default: search PATH)",
    )
    parser.add_argument(
        "--source-disk",
        type=Path,
        default=REPO_ROOT / "src" / "disks" / "source-altair" / "cpm63k.dsk",
    )
    parser.add_argument(
        "--disk-dir",
        type=Path,
        default=REPO_ROOT / "src" / "disks" / "generated",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=REPO_ROOT / "build" / "cpm",
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        help="Stage 10 .bin to include in a complete 4 MiB flash image",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.assembler is None:
        raise SystemExit("z80asm was not found; install it or pass --assembler")

    cpm_system = extract_cpm_system(args.source_disk.read_bytes())
    bios = build_bios(args.assembler)
    z80_image = build_z80_image(cpm_system, bios)
    package = build_boot_package(z80_image)

    generated = [(args.disk_dir / name).read_bytes() for name in DISK_NAMES]
    disks = [build_native_drive(generated[0], cpm_system, bios), *generated[1:]]
    if any(len(disk) != DISK_IMAGE_BYTES for disk in disks):
        raise ValueError("all disk images must be exactly 320 KiB")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    artifacts: dict[str, bytes] = {
        "bios.bin": bios,
        "z80boot.img": z80_image,
        "z80boot.pkg": package,
    }
    artifacts.update(dict(zip(DISK_NAMES, disks, strict=True)))

    if args.firmware is not None:
        artifacts["z80romless-flash.bin"] = build_flash_image(
            args.firmware.read_bytes(), package, disks
        )

    for name, data in artifacts.items():
        (args.output_dir / name).write_bytes(data)

    manifest = {
        "geometry": {
            "tracks": TRACKS,
            "sectors_per_track": SECTORS_PER_TRACK,
            "record_bytes": RECORD_BYTES,
            "reserved_tracks": RESERVED_TRACKS,
            "dpb": {
                "bsh": DPB_BSH,
                "blm": DPB_BLM,
                "exm": DPB_EXM,
                "dsm": DPB_DSM,
                "drm": DPB_DRM,
                "al0": DPB_AL0,
                "al1": DPB_AL1,
                "cks": DPB_CKS,
            },
        },
        "memory": {
            "ccp": CCP_BASE,
            "bdos_entry": BDOS_ENTRY,
            "bios": BIOS_BASE,
            "bios_bytes": len(bios),
        },
        "artifacts": {
            name: {"bytes": len(data), "sha256": sha256(data)}
            for name, data in artifacts.items()
        },
    }
    manifest_path = args.output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="ascii",
    )
    print(f"wrote {len(artifacts)} images and {manifest_path}")


if __name__ == "__main__":
    main()