#!/usr/bin/env python3
"""Build the native CP/M boot package, disks, and optional flash image."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import os
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


CCP_BASE = 0xE700
BDOS_BASE = 0xEF00
BDOS_ENTRY = 0xEF06
BIOS_BASE = 0xFD00
CPM_SYSTEM_RECORDS = 44
CPM_SYSTEM_BYTES = CPM_SYSTEM_RECORDS * RECORD_BYTES
CPM_SYSTEM_PATH = REPO_ROOT / "src" / "cpm" / "cpm64_system.bin"
CPM_Z80_SOURCE_PATH = REPO_ROOT / "src" / "cpm" / "cpm64_z80.asm"
CPM_SYSTEM_SHA256 = (
    "2897f0ecf91048c753ea6a09f26fd28f20a607dddbbaca0c96a6943178115d0e"
)
BIOS_DIRECTORY_BUFFER = 0xFC80
AUTORUN_RECORD = RESERVED_TRACKS * SECTORS_PER_TRACK - 1
AUTORUN_MAX_COMMAND = 80
AUTORUN_MAGIC = b"ZAR1"
AUTORUN_OFFSET = AUTORUN_RECORD * RECORD_BYTES

BOOT_MAGIC = 0x5442385A
BOOT_VERSION = 1
BOOT_HEADER_BYTES = 20
BOOT_PAYLOAD_OFFSET = 0x1000
Z80_IMAGE_BYTES = 65536

FLASH_BYTES = 4 * 1024 * 1024
FLASH_LINK_LIMIT = 0x1D0000
FLASH_JOURNAL_OFFSET = 0x1D0000
FLASH_JOURNAL_BYTES = 0x10000
FLASH_BOOT_OFFSET = 0x1E0000
FLASH_BOOT_BYTES = 0x20000
FLASH_ENV_OFFSET = 0x1FE000
FLASH_ENV_BYTES = 0x2000
FLASH_DISK_OFFSETS = (0x200000, 0x280000, 0x300000, 0x380000)

SOURCE_DISK_NAMES = (
    "drive_a_cpm63k.img",
    "drive_b_bdsc.img",
    "drive_c_escape.img",
    "drive_d_blank.img",
)
OUTPUT_DISK_NAMES = (
    "drive_a_cpm63k-z80.img",
    *SOURCE_DISK_NAMES[1:],
)

assert FLASH_LINK_LIMIT == FLASH_JOURNAL_OFFSET
assert FLASH_JOURNAL_OFFSET + FLASH_JOURNAL_BYTES == FLASH_BOOT_OFFSET
assert FLASH_BOOT_OFFSET + FLASH_BOOT_BYTES == FLASH_DISK_OFFSETS[0]
assert FLASH_BOOT_OFFSET + BOOT_PAYLOAD_OFFSET + Z80_IMAGE_BYTES <= FLASH_ENV_OFFSET
assert FLASH_ENV_OFFSET + FLASH_ENV_BYTES == FLASH_DISK_OFFSETS[0]
assert all(
    left + DISK_IMAGE_BYTES == right
    for left, right in zip(FLASH_DISK_OFFSETS, FLASH_DISK_OFFSETS[1:])
)
assert FLASH_DISK_OFFSETS[-1] + DISK_IMAGE_BYTES == FLASH_BYTES


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_cpm_system() -> bytes:
    system = CPM_SYSTEM_PATH.read_bytes()
    if len(system) != CPM_SYSTEM_BYTES:
        raise ValueError(f"unexpected CP/M system size: {len(system)}")
    if sha256(system) != CPM_SYSTEM_SHA256:
        raise ValueError("CP/M 64K CCP/BDOS fingerprint does not match")
    return system


def assemble_cpm_source(assembler: str, source: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="z80sbc-system-") as directory:
        temporary = Path(directory)
        source_path = temporary / "cpm64_z80.asm"
        binary_path = temporary / "cpm64_z80.bin"
        source_path.write_text(source, encoding="ascii")
        subprocess.run(
            [assembler, "-o", str(binary_path), str(source_path)],
            check=True,
        )
        system = binary_path.read_bytes()

    if len(system) != CPM_SYSTEM_BYTES:
        raise ValueError(f"unexpected Z80 CP/M system size: {len(system)}")
    if system[0] != 0xC3:
        raise ValueError("Z80 CCP does not start with JP")
    if system[BDOS_BASE - CCP_BASE + 1] != 22:
        raise ValueError("Z80 BDOS version is not CP/M 2.2")
    return system


def build_cpm_system(assembler: str) -> bytes:
    return assemble_cpm_source(
        assembler, CPM_Z80_SOURCE_PATH.read_text(encoding="ascii")
    )


def assembler_definitions() -> str:
    definitions = {
        "SYSTEM_RECORDS": CPM_SYSTEM_RECORDS,
        "DRIVE_COUNT": DRIVE_COUNT,
        "DPB_SPT": SECTORS_PER_TRACK,
        "DPB_ALLOCATION_BYTES": (DPB_DSM + 8) // 8,
        "DPB_BSH": DPB_BSH,
        "DPB_BLM": DPB_BLM,
        "DPB_EXM": DPB_EXM,
        "DPB_DSM": DPB_DSM,
        "DPB_DRM": DPB_DRM,
        "DPB_AL0": DPB_AL0,
        "DPB_AL1": DPB_AL1,
        "DPB_CKS": DPB_CKS,
        "DPB_OFF": RESERVED_TRACKS,
        "BIOS_DIRECTORY_BUFFER": BIOS_DIRECTORY_BUFFER,
        "AUTORUN_TRACK": AUTORUN_RECORD // SECTORS_PER_TRACK,
        "AUTORUN_SECTOR": AUTORUN_RECORD % SECTORS_PER_TRACK,
        "AUTORUN_MAX_COMMAND": AUTORUN_MAX_COMMAND,
        "AUTORUN_MAGIC_LOW": int.from_bytes(AUTORUN_MAGIC[:2], "little"),
        "AUTORUN_MAGIC_HIGH": int.from_bytes(AUTORUN_MAGIC[2:], "little"),
        "NATIVE_WBOOT": BIOS_BASE + 3,
        "AUTORUN_BIOS_SIGNATURE": BIOS_BASE + 17 * 3,
    }
    return "".join(
        f"{name}: equ {value}\n" for name, value in definitions.items()
    )


def build_bios(assembler: str) -> bytes:
    source_path = REPO_ROOT / "src" / "cpm" / "z80_bios.asm"
    with tempfile.TemporaryDirectory(prefix="z80sbc-cpm-") as directory:
        temporary = Path(directory)
        combined_path = temporary / "z80_bios.asm"
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
    if CPM_SYSTEM_BYTES + len(bios) > AUTORUN_OFFSET:
        raise ValueError("CCP, BDOS, and BIOS overlap the autorun record")
    return bios


def build_autorun(assembler: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="z80sbc-autorun-") as directory:
        source = Path(directory) / "autorun.asm"
        binary = Path(directory) / "AUTORUN.COM"
        source.write_text(
            assembler_definitions()
            + (REPO_ROOT / "src/cpm/autorun.asm").read_text(encoding="ascii"),
            encoding="ascii",
        )
        subprocess.run([assembler, "-o", str(binary), str(source)], check=True)
        return binary.read_bytes()


def cpm_diskdefs() -> str:
    """Native flat-image variant of the Altair project's diskdefs/altair88.

    Preserve its explicit logicalextents 1 (EXM=0). This board's converted
    images have 128 tracks and already-decoded sector order, hence skew 1.
    Numeric parameters come from the shared native geometry used by the BIOS.
    """
    return (
        "diskdef z80sbc\n"
        f"  seclen {RECORD_BYTES}\n"
        f"  tracks {TRACKS}\n"
        f"  sectrk {SECTORS_PER_TRACK}\n"
        f"  blocksize {RECORD_BYTES << DPB_BSH}\n"
        f"  maxdir {DPB_DRM + 1}\n"
        f"  logicalextents {DPB_EXM + 1}\n"
        "  skew 1\n"
        f"  boottrk {RESERVED_TRACKS}\n"
        "  os 2.2\nend\n"
    )


def install_autorun(disk: bytes, utility: bytes, cpmcp: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="z80sbc-cpmcp-") as directory:
        temporary = Path(directory)
        (temporary / "diskdefs").write_text(
            cpm_diskdefs(),
            encoding="ascii",
        )
        image = temporary / "drive.img"
        image.write_bytes(disk)
        program = temporary / "AUTORUN.COM"
        program.write_bytes(utility)
        environment = {**os.environ, "DISKDEFS": str(temporary / "diskdefs")}
        entries = (
            disk[offset:offset + 32]
            for offset in range(SYSTEM_BYTES, SYSTEM_BYTES + (DPB_DRM + 1) * 32, 32)
        )
        if any(
            entry[0] == 0
            and bytes(value & 0x7F for value in entry[1:12]) == b"AUTORUN COM"
            for entry in entries
        ):
            resolved = shutil.which(cpmcp)
            if resolved is None:
                raise ValueError(f"cpmcp executable not found: {cpmcp}")
            cpmrm = Path(resolved).with_name("cpmrm")
            subprocess.run(
                [str(cpmrm), "-f", "z80sbc", str(image), "0:AUTORUN.COM"],
                cwd=temporary, env=environment, check=True,
            )
        subprocess.run(
            [cpmcp, "-f", "z80sbc", str(image), str(program), "0:AUTORUN.COM"],
            cwd=temporary,
            env=environment,
            check=True,
        )
        return image.read_bytes()


def autorun_record(mode: int = 0, command: str = "") -> bytes:
    encoded = command.encode("ascii")
    if mode not in range(4) or len(encoded) > AUTORUN_MAX_COMMAND:
        raise ValueError("invalid autorun mode or command length")
    if any(value < 0x20 or value > 0x7E for value in encoded):
        raise ValueError("autorun command must contain printable ASCII")
    if mode and not encoded:
        raise ValueError("enabled autorun requires a command")
    record = bytearray(RECORD_BYTES)
    record[:4] = AUTORUN_MAGIC
    record[4:6] = bytes((mode, len(encoded)))
    record[6:6 + len(encoded)] = encoded.upper()
    record[-1] = -sum(record) & 0xFF
    return bytes(record)


def build_z80_image(cpm_system: bytes, bios: bytes) -> bytes:
    if CCP_BASE + len(cpm_system) != BIOS_BASE:
        raise ValueError("CP/M system does not end at the BIOS base")
    if any(cpm_system[BIOS_DIRECTORY_BUFFER - CCP_BASE:]):
        raise ValueError("native BIOS requires 128 bytes of reserved BDOS padding")

    image = bytearray(Z80_IMAGE_BYTES)
    image[0:3] = bytes((0xC3, BIOS_BASE & 0xFF, BIOS_BASE >> 8))
    image[CCP_BASE:BIOS_BASE] = cpm_system
    image[BIOS_BASE : BIOS_BASE + len(bios)] = bios
    return bytes(image)


def build_boot_package(image: bytes) -> bytes:
    if len(image) != Z80_IMAGE_BYTES:
        raise ValueError("CP/M boot image must contain exactly 65536 bytes")
    if image[:3] != bytes((0xC3, BIOS_BASE & 0xFF, BIOS_BASE >> 8)):
        raise ValueError("CP/M reset vector must jump to the BIOS at FD00h")
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
    if len(system_tracks) > AUTORUN_OFFSET:
        raise ValueError("system image overlaps the autorun record")
    system_tracks += bytes([0xE5]) * (AUTORUN_OFFSET - len(system_tracks))
    system_tracks += autorun_record()
    return system_tracks + source[SYSTEM_BYTES:]


def build_flash_image(firmware: bytes, package: bytes, disks: list[bytes]) -> bytes:
    if not firmware or len(firmware) > FLASH_LINK_LIMIT:
        raise ValueError("firmware binary is empty or crosses the flash link limit")
    if len(package) > FLASH_ENV_OFFSET - FLASH_BOOT_OFFSET:
        raise ValueError("boot package overlaps persistent environment storage")
    if len(disks) != len(FLASH_DISK_OFFSETS):
        raise ValueError("complete flash image requires exactly four disks")

    image = bytearray([0xFF]) * FLASH_BYTES
    image[: len(firmware)] = firmware
    image[FLASH_BOOT_OFFSET : FLASH_BOOT_OFFSET + len(package)] = package
    for offset, disk in zip(FLASH_DISK_OFFSETS, disks):
        image[offset : offset + len(disk)] = disk
    return bytes(image)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cpmcp",
        default=shutil.which("cpmcp"),
        help="cpmtools cpmcp executable (default: search PATH)",
    )
    parser.add_argument(
        "--assembler",
        default=shutil.which("z80asm"),
        help="z80asm executable (default: search PATH)",
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
    if args.cpmcp is None:
        raise SystemExit("cpmcp was not found; install cpmtools or pass --cpmcp")

    cpm_system = build_cpm_system(args.assembler)
    bios = build_bios(args.assembler)
    utility = build_autorun(args.assembler)
    z80_image = build_z80_image(cpm_system, bios)
    package = build_boot_package(z80_image)

    generated = [
        (args.disk_dir / name).read_bytes() for name in SOURCE_DISK_NAMES
    ]
    native = build_native_drive(generated[0], cpm_system, bios)
    disks = [install_autorun(native, utility, args.cpmcp), *generated[1:]]
    if any(len(disk) != DISK_IMAGE_BYTES for disk in disks):
        raise ValueError("all disk images must be exactly 512 KiB")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    artifacts: dict[str, bytes] = {
        "bios.bin": bios,
        "AUTORUN.COM": utility,
        "z80boot.img": z80_image,
        "z80boot.pkg": package,
    }
    artifacts.update({name: disks[index] for index, name in enumerate(OUTPUT_DISK_NAMES)})

    if args.firmware is not None:
        artifacts["z80romless-flash.bin"] = build_flash_image(
            args.firmware.read_bytes(), package, disks
        )

    for name, data in artifacts.items():
        (args.output_dir / name).write_bytes(data)

    manifest = {
        "autorun": {
            "drive": "A",
            "record": AUTORUN_RECORD,
            "magic": AUTORUN_MAGIC.decode("ascii"),
            "max_command": AUTORUN_MAX_COMMAND,
            "provisioning": "initializes disabled; replaces AUTORUN.COM on drive A",
        },
        "environment": {
            "offset": FLASH_ENV_OFFSET,
            "bytes": FLASH_ENV_BYTES,
            "full_provisioning": "erases variables; selective firmware/boot updates preserve them",
        },
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
