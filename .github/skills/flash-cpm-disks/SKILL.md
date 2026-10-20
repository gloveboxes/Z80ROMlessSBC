---
name: flash-cpm-disks
description: 'Flash, write, update, provision, or replace one or more CP/M disks on the Z80ROMlessSBC Pico 2 W using the maintained Python tools. Use for .dsk/.img uploads, BDS C and Escape disks, Altair conversion, no-backup selective drive updates, journal checks, BOOTSEL, and read-back verification without rebuilding firmware.'
argument-hint: 'Disk image paths and target drives A-D'
user-invocable: true
---

# Flash CP/M Disks

Use this self-contained workflow for disk-only deployment. No engineering-doc
lookup or firmware build is needed for the established layout below. If source
constants or tools contradict this skill, stop and investigate the discrepancy.
Firmware, BIOS, geometry, or hardware changes are outside this workflow.

Run commands from the repository root. The disk command snapshots selected
inputs and converts them in a temporary directory, then cleans up automatically.
It never modifies the source files.

Use `scripts/pico_flash.py disks` for the complete deployment. Do not write
fresh inline serial clients, conversion snippets, journal readers or manual
picotool write sequences for routine disk flashing.
If the flashing environment is absent, set it up once:

```sh
python3 -m venv .venv-flash &&
. .venv-flash/bin/activate &&
python -m pip install -r scripts/requirements-flash.txt
```

### Required virtual environment preflight

Every tool call starts a fresh shell: activation in an earlier call does not
carry over. For **every** flash, preparation, journal or supervisor invocation,
activate `.venv-flash`, check `serial` imports and run the tool in the **same
shell command**, joined with `&&`, as shown below. Do not rely on the terminal
prompt, a previously activated environment, or another Python interpreter.
The printed interpreter must be inside the repository's `.venv-flash`.

If the preflight fails, do not touch hardware. Diagnose the interpreter and
dependency paths; restore `scripts/requirements-flash.txt` inside this environment
only if dependencies are missing. Do not run unit tests as a workaround to make
imports work, and do not bypass the check with system Python.

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

## 1. Identify Inputs; No Backups by Default

1. Resolve the exact files the user supplied. Fresh images may be directly in
   `src/disks/`, while older copies exist in `src/disks/source-altair/`.
   Do not silently substitute the older copies.
2. Map the selected images to drives using the table. Ask only if ambiguous.
3. This is a development/non-production board. The standing project preference
   is **no backups**: do not ask about backups or save existing disks for
   routine replacements. Only perform a backup when explicitly requested
   separately; the disk command itself has no backup step.
4. Check `git status --short`. Preserve unrelated changes and source images;
   deployment alone does not require modifying canonical sources, generated
   images, manifests, or documentation.

## 2. Prepare and Validate Native Images

The tool detects native versus framed Altair format by exact length, not
extension. Framed inputs are 337,568 or 337,664 bytes (77 x 32 x 137,
optionally followed by a 96-byte marker). B-D Altair inputs are automatically
validated and converted using the maintained `validate_source()` and `convert()`
helpers in `src/disks/convert_altair_disks.py`, including sector checksum,
framing and high-track sector-ID checks. Do not duplicate their sector mapping.

Run the existing converter tests:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python -m unittest discover -s src/disks -p 'test_*.py' -v
```

Native inputs must be exactly 327,680 bytes; no conversion is applied. The tool
checks every selected input before disconnecting/stopping hardware and logs each
resulting image's SHA-256. Duplicate or invalid drive assignments are rejected.
Drive A requires a board-native image: framed Altair A is rejected, and its
sequential warm-boot records must match the installed boot package. That
read-only compatibility check validates the package manifest/payload CRCs and
does not replace firmware or boot data.

## 3. Stop Safely and Enter BOOTSEL

- Maintain the populated board's external +5 V throughout programming.
  External +5 V must precede USB; disconnect USB before removing external
  power. Do not switch a populated board to USB-only power.
- Discover devices with `pico_supervisor.py ports`. Never assume a fixed port
  or board serial. A unique connected Pico is selected automatically; with
  multiple devices pass `--serial DEVICE_SERIAL`.
- For an authorized deployment, the project default is to gracefully close
  picocom, obtain exclusive serial access, and stop/flush the Stage 9 CPU.
  The tool verifies the picocom PID and uses SIGTERM, never name-based killing,
  SIGKILL, or disconnecting unknown owners. `--no-disconnect` opts out.
- The maintained tool asserts DTR and uses raw 115200 8N1 without flow control.
  It sends **Ctrl+] then `s`**, then **Ctrl+] then `x`** if running, and waits
  for `Disk quiescent; RESET held LOW, PWM stopped` before forcing BOOTSEL.
  An already-stopped status must report ready storage without a fatal error.
  A disconnected terminal alone is not evidence of a clean stop.
- If serial gives no output, do not repeat blind probes. Report that clean
  stop was not confirmed. For an explicitly authorized destructive replacement,
  enter BOOTSEL and inspect the journal before writing; otherwise ask before
  forcing a reset that could lose unflushed work.

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_supervisor.py ports
```

The `disks` command below handles disconnection, clean stop, BOOTSEL and journal
checking automatically. With multiple Picos add `--serial DEVICE_SERIAL`.
If already in BOOTSEL, add `--bootsel --stopped --serial DEVICE_SERIAL`, only
after a prior clean stop was confirmed. If software reboot is unavailable,
request physical BOOTSEL entry without violating the power sequence; do not
bypass an unconfirmed flush. For diagnosis only, `prepare --power-confirmed`
performs preparation/journal checks without writing any disk.

## 4. Check for Pending Journal Replay

The disk command always checks before selective writes.
Reading temporary journal headers is a safety check, not a disk backup.
There are eight 8 KiB pairs starting at `0x10290000`; each pair has a 256-byte
header at its start. Successfully completed writes erase the header sector.

`disks` and `prepare` check all eight headers. To repeat just this read-only check on the
selected device already in BOOTSEL:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py journal --serial "$DEVICE_SERIAL"
```

If a header is not erased, stop. Let the existing firmware recover the journal
and obtain a clean stop before retrying, or investigate the exact header with
the maintained journal implementation. Do not erase the journal as a shortcut:
it can contain recoverable writes for unselected drives.

## 5. Program Only Selected Slots and Verify

For B and C, using the exact supplied source paths (Altair or native):

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py disks \
  --drive B=src/disks/bdsc-v1.60.dsk \
  --drive C=src/disks/escape-posix.dsk \
  --power-confirmed
```

Repeat `--drive DRIVE=IMAGE` for each selected drive. Supply one to four unique
assignments; omitted drives are never written. Drive A must use a matching
board-native image such as `build/cpm-stage09-loader/drive_a_cpm63k-z80.img`.
The command writes verified native-sized images only at the fixed slot
addresses, stays in BOOTSEL between writes, then reboots once after all selected
slots pass read-back verification. `--no-reboot` leaves it in BOOTSEL instead.
No backups are created, and no boot/health checks run after programming.

On failure, preserve output and report the device's actual state; do not claim
success or reboot into a partially updated installation without resolving it.
If output is offloaded, inspect only completion/error lines rather than dumping
carriage-return progress logs.

After the requested reboot, leave serial free for picocom. Routine deployment
ends with verified writes and the requested reboot; do not add firmware boot
or supervisor-health checks unless the user separately requests them.
Report selected source-to-drive mappings, verified writes, that no backups were taken, and
that unselected flash regions were not written. Serial-port reappearance proves
USB availability, not a successful CP/M boot; claim boot success only when
observed. No firmware rebuild or full CP/M test suite is needed for disk-only
deployment using the established format.

For separately requested serial discovery/status after reboot, reuse `pico_supervisor.py ports` and
`pico_supervisor.py status --serial "$DEVICE_SERIAL"`; do not generate an
inline Node.js or Python serial probe. Run the tool tests with
the following when modifying the helpers:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python -m unittest discover -s scripts -p 'test_pico*.py' -v
```
