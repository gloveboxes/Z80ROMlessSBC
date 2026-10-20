---
name: flash-cpm-disks
description: 'Flash, write, update, provision, or replace CP/M disk images on the Z80ROMlessSBC Pico 2 W using picotool. Use for .dsk/.img uploads, BDS C and Escape disks, Altair image conversion, selective drive updates, journal safety checks, backup preferences, BOOTSEL, and read-back verification without rebuilding firmware.'
argument-hint: 'Disk image paths, target drives, and whether backups are required'
user-invocable: true
---

# Flash CP/M Disks

Use this self-contained workflow for disk-only deployment. No engineering-doc
lookup or firmware build is needed for the established layout below. If source
constants or tools contradict this skill, stop and investigate the discrepancy.
Firmware, BIOS, geometry, or hardware changes are outside this workflow.

Run commands from the repository root. Store converted deployment images in
the current session's artifact directory, not a hard-coded user's home.

## Fixed Layout

The Pico 2 W has 4 MiB onboard flash. Every native disk is exactly **327,680
bytes**: 80 tracks x 32 records x 128 bytes. Addresses passed to `picotool`
include the `0x10000000` XIP base.

| Drive | Typical input | Native image | Flash address | Exclusive end |
| --- | --- | --- | --- | --- |
| A | `cpm63k.dsk` | `drive_a_cpm63k-z80.img` | `0x102C0000` | `0x10310000` |
| B | `bdsc-v1.60.dsk` | `drive_b_bdsc.img` | `0x10310000` | `0x10360000` |
| C | `escape-posix.dsk` | `drive_c_escape.img` | `0x10360000` | `0x103B0000` |
| D | `blank.dsk` | `drive_d_blank.img` | `0x103B0000` | `0x10400000` |

Protected regions for disk-only work:

- Firmware: offsets below `0x290000`.
- Journal: `0x10290000` to `0x102A0000`.
- Boot package: `0x102A0000` to `0x102C0000`.
- All unselected disk slots.

Never use a complete flash image, mass erase, or firmware UF2 to update disks.
Never write a framed Altair `.dsk` directly into a native disk slot.
Drive A requires the board-native system tracks; the plain converter output
`drive_a_cpm63k.img` is **not** a provisionable system disk. For Drive A, use
an already matching board-native image or switch to the CP/M image-build
workflow rather than inventing BIOS/build steps here.

## 1. Identify Inputs and Backup Policy

1. Resolve the exact files the user supplied. Fresh images may be directly in
   `src/disks/`, while older copies exist in `src/disks/source-altair/`.
   Do not silently substitute the older copies.
2. Map the selected images to drives using the table. Ask only if ambiguous.
3. Honor an explicit "no backups, just write" request: do not save existing
   disk contents. This permission applies to the current request, not all
   future deployments.
4. Otherwise ask whether to back up the selected slots before overwriting.
   Do not expand a selected-drive backup into a full-flash backup.
5. Check `git status --short`. Preserve unrelated changes and source images;
   deployment alone does not require modifying canonical sources, generated
   images, manifests, or documentation.

## 2. Prepare and Validate Native Images

Reuse `src/disks/convert_altair_disks.py`; do not duplicate its sector mapping.
Framed Altair inputs are 337,568 or 337,664 bytes (77 x 32 x 137,
optionally followed by a 96-byte marker).

Run the existing converter tests:

```sh
python3 -m unittest discover -s src/disks -p 'test_*.py' -v
```

Convert each supplied Altair input explicitly. Set `SOURCE` and `OUTPUT` to
the selected source and session output paths:

```sh
python3 - "$SOURCE" "$OUTPUT" <<'PY'
from pathlib import Path
import hashlib
import sys

sys.path.insert(0, str(Path("src/disks").resolve()))
from convert_altair_disks import SOURCE_GRID_BYTES, convert

source_path, output_path = map(Path, sys.argv[1:])
source = source_path.read_bytes()
if len(source) not in (SOURCE_GRID_BYTES, SOURCE_GRID_BYTES + 96):
    raise SystemExit(f"Unexpected framed image size: {len(source)}")
for track in range(77):
    sector_ids = []
    for slot in range(32):
        offset = (track * 32 + slot) * 137
        sector = source[offset:offset + 137]
        if track < 6:
            valid = sector[131] == 255 and sector[132] == sum(sector[3:131]) & 255
        else:
            valid = (
                sector[0] & 127 == track
                and sector[135:137] == b"\xff\x00"
                and sector[4] == (
                    sum(sector[7:135]) + sector[2] + sector[3]
                    + sector[5] + sector[6]
                ) & 255
            )
            sector_ids.append(sector[1])
        if not valid:
            raise SystemExit(f"Invalid sector framing/checksum: {track=}, {slot=}")
    if track >= 6 and sorted(sector_ids) != list(range(32)):
        raise SystemExit(f"Invalid sector IDs: {track=}")
image = convert(source)
if len(image) != 327680:
    raise SystemExit(f"Unexpected native image size: {len(image)}")
output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_bytes(image)
print(f"{output_path}: {len(image)} bytes; SHA256 {hashlib.sha256(image).hexdigest()}")
PY
```

For an already native `.img`, check exact size and SHA-256 without applying
Altair conversion. Extension alone is not proof of format. Existing converter
tests cover repository fixtures; the checks above validate the actual supplied
Altair inputs. Stop on failures rather than flashing a plausible-looking image.

## 3. Stop Safely and Enter BOOTSEL

- Maintain the populated board's external +5 V throughout programming.
  External +5 V must precede USB; disconnect USB before removing external
  power. Do not switch a populated board to USB-only power.
- Discover the actual port with `ls /dev/cu.usbmodem*`; a common port is
  `/dev/cu.usbmodem1101`, but it is not guaranteed.
- Check `lsof` on that port. Never open a second serial owner or kill a user's
  terminal. A disconnected picocom client does not prove the CPU is stopped.
- On the Stage 9 CP/M loader, send **Ctrl+] then `s`** for status, then
  **Ctrl+] then `x`** to stop/flush. Wait for
  `Disk quiescent; RESET held LOW, PWM stopped` and check for storage errors.
  If already stopped, confirm status and still inspect the journal below.
- Use an available serial client/library; assert DTR and use raw 115200 8N1,
  no flow control or local echo. Close the client before `picotool`.
  Do not send plain `x` to CP/M or assume all firmware has the same commands.
- If serial gives no output, do not repeat blind probes. Report that clean
  stop was not confirmed. For an explicitly authorized destructive replacement,
  enter BOOTSEL and inspect the journal before writing; otherwise ask before
  forcing a reset that could lose unflushed work.

```sh
picotool reboot -u -f
picotool info
```

If already in BOOTSEL, skip the forced reboot. If multiple devices are present,
select the intended Pico and apply the same device-selection arguments to every
subsequent command. If software reboot is unavailable, request physical BOOTSEL
entry without violating the power sequence.

## 4. Check for Pending Journal Replay

Always check before selective writes, including when backups were waived.
Reading temporary journal headers is a safety check, not a disk backup.
There are eight 8 KiB pairs starting at `0x10290000`; each pair has a 256-byte
header at its start. Successfully completed writes erase the header sector.

The following conservative check requires all eight headers to be erased.
For multiple devices, add the established selection arguments to the
`picotool` invocation inside this snippet too.

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix="z80-journal-") as directory:
    for pair in range(8):
        address = 0x10290000 + pair * 8192
        path = Path(directory) / f"header-{pair}.bin"
        subprocess.run([
            "picotool", "save", "-r", hex(address), hex(address + 256),
            str(path), "-t", "bin",
        ], check=True)
        header = path.read_bytes()
        if header != b"\xff" * 256:
            raise SystemExit(
                f"Journal pair {pair} is not erased; recover/investigate before flashing"
            )
print("PASS: all journal headers erased; no pending replay")
PY
```

If a header is not erased, stop. Let the existing firmware recover the journal
and obtain a clean stop before retrying, or investigate the exact header with
the maintained journal implementation. Do not erase the journal as a shortcut:
it can contain recoverable writes for unselected drives.

If backups were requested, save only the selected drive ranges now, before
loading anything. For example, Drive B:

```sh
picotool save -r 0x10310000 0x10360000 "$BACKUP_B" -t bin
```

## 5. Program Only Selected Slots and Verify

For B and C, using the prepared native image paths:

```sh
picotool load -v "$IMAGE_B" -t bin -o 0x10310000 &&
picotool load -v "$IMAGE_C" -t bin -o 0x10360000 &&
picotool reboot
```

Omit unselected drives. Use the table for A or D. `-v` performs read-back
verification; require successful exit status and completed verification for
each selected slot. Do not use `-f` on each load: it would restart the application
between writes. Keep the Pico in BOOTSEL until all selected images are verified.

On failure, preserve output and report the device's actual state; do not claim
success or reboot into a partially updated installation without resolving it.
If output is offloaded, inspect only completion/error lines rather than dumping
carriage-return progress logs.

After reboot, wait for USB serial to reappear and leave it free for picocom.
Report selected source-to-drive mappings, verified writes, backup policy, and
that unselected flash regions were not written. Serial-port reappearance proves
USB availability, not a successful CP/M boot; claim boot success only when
observed. No firmware rebuild or full CP/M test suite is needed for disk-only
deployment using the established format.
