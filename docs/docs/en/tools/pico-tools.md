# Pico flashing and USB supervisor tools

Use these maintained Python command-line tools from the repository root:

| Tool | Purpose |
| --- | --- |
| [scripts/pico_flash.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/scripts/pico_flash.py) | Verified firmware updates, selective CP/M disk replacement, BOOTSEL preparation and journal checks |
| [scripts/pico_supervisor.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/scripts/pico_supervisor.py) | Serial discovery and standalone Stage 9 status, help, clean stop and optional boot checks |

For build targets, first-time full provisioning, or coordinated firmware/BIOS
changes, use [Firmware build and provisioning](../system/firmware-build.md).
Routine disk replacement needs no firmware rebuild.

## Safety and supported firmware

!!! warning "Save work and finish running hardware tests first"
    Flashing can disconnect picocom, stop the Z80 and reset the Pico. Do not
    interrupt an active soak or hardware test. Disk replacement overwrites the
    selected drives; this development workflow creates no backups.

Maintain the populated board's external +5 V throughout programming. Apply
external +5 V **before USB**; disconnect USB before removing external power.
Never power the populated board from USB alone. `--power-confirmed` records
the operator's confirmation; it does not measure the power rails.

The automatic pre-flash stop and standalone supervisor commands speak the
**currently running Stage 9 protocol**. They do not infer that protocol from
the target UF2. If another phase is running, obtain its stage-appropriate safe
stopped state first, then use `--stopped`. Do not send Stage 9 commands to
unknown firmware or treat a timeout as permission to force a reset.

## Environment setup

The tools require Python 3.9+, `picotool`, and the POSIX `lsof`/`ps` tools
(macOS or Linux). Create the flashing environment once if absent:

```sh
python3 -m venv .venv-flash &&
. .venv-flash/bin/activate &&
python -m pip install -r scripts/requirements-flash.txt
```

For each invocation, activate and check the environment **in the same shell
command** as the tool. Separate automation/tool calls do not retain activation.
The preflight prints the interpreter, which must be inside this repository's
`.venv-flash`, and confirms `pyserial` imports before any hardware access:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py --help
```

If that check fails, fix the interpreter/dependencies before proceeding.
Do not switch to system Python or run tests as a workaround for import errors.
An explicit `.venv-flash/bin/python` path normally selects the venv without
activation; a missing-module error alone does not establish which interpreter
or search path was actually used.

## Discover and select the Pico

List serial devices without opening a serial connection:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_supervisor.py ports
```

The listing includes port names, descriptions and USB serial numbers, including
non-Pico serial devices. Commands that connect automatically select a unique
Pico with Raspberry Pi USB vendor ID `0x2e8a`. They refuse absent or ambiguous
matches. Never assume a fixed port such as `/dev/cu.usbmodem1101`.

With multiple Picos, add `--serial DEVICE_SERIAL`, replacing `DEVICE_SERIAL`
with the intended board's USB serial number. An optional `--port` restricts the
initial selection. Serial-number selection can follow a board if its port
name changes.

## Firmware updates

To update an already matching Stage 9 installation, save CP/M work and keep
external +5 V applied. The firmware command **gracefully disconnects picocom
and stops/flushes the CPU by default**, then writes only the firmware:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py firmware \
  build/src/stage09_flash_storage/z80_stage09_cpm_loader.uf2 \
  --power-confirmed
```

Picocom disconnection verifies its PID and uses SIGTERM, not a forced kill.
Unknown serial owners, failed stops, and storage errors abort without resetting
the board. `--no-disconnect` instead refuses a busy port. `--stopped` skips the
stop request only when a clean stop was already confirmed by the operator.
For an already-BOOTSEL board, supply `--bootsel --stopped --serial DEVICE_SERIAL`;
the clean stop must have occurred before BOOTSEL entry.

UF2 payloads must be valid RP2350 ARM Secure firmware entirely below offset
`0x290000`. Loading includes picotool read-back verification. Failed loading or
verification never triggers an automatic reboot. `--no-reboot` deliberately
leaves the device in BOOTSEL after a verified write.

Firmware-only updates leave the boot package, journal, persistent environment
and all four disks unchanged. They do not provision those regions for a new
installation or make an incompatible BIOS/disk combination compatible.

**Flash success means the programmed bytes passed read-back verification.**
The command does not check what boots afterward: no CP/M prompt, firmware banner,
USB enumeration or supervisor health is required. This also applies when writing
non-CP/M phase firmware. The former `--check-boot` option is removed; boot checks
are separate from flashing. Serial ownership is released on exit.

## Selective disk updates without backups

The `disks` command handles one or more selected drives end-to-end. For example:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py disks \
  --drive B=src/disks/bdsc-v1.60.dsk \
  --drive C=src/disks/escape-posix.dsk \
  --power-confirmed
```

Repeat `--drive DRIVE=IMAGE` for each unique selected slot A-D. The standing
default for this non-production board is **no backups and no backup prompts**.
The command validates/snapshots all inputs before stopping hardware, gracefully
disconnects picocom, confirms stop/flush, enters BOOTSEL and checks all eight
journal headers. It writes only the selected slots with read-back verification,
then reboots once. `--no-reboot` keeps BOOTSEL instead. No post-flash boot check
is performed.

Native images must be exactly 327,680 bytes. Altair-framed B-D images are
validated for framing, checksums and sector IDs, then converted with the existing
converter; filenames/extensions do not determine format. Sources are untouched
and temporary snapshots are removed on success or failure.

Drive A must already be a board-native image, for example
`build/cpm-stage09-loader/drive_a_cpm63k-z80.img`. Framed Altair Drive A is rejected.
Before any selected disk is written, A's sequential CCP/BDOS warm-boot records
are compared with the installed, CRC-validated boot package. A mismatch aborts;
the tool does not replace the boot package or invent new system tracks.

To replace only Drive A with that matching image:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py disks \
  --drive A=build/cpm-stage09-loader/drive_a_cpm63k-z80.img \
  --power-confirmed
```

| Drive | Flash address | Exclusive end |
| --- | --- | --- |
| A | `0x102c0000` | `0x10310000` |
| B | `0x10310000` | `0x10360000` |
| C | `0x10360000` | `0x103b0000` |
| D | `0x103b0000` | `0x10400000` |

These addresses include the `0x10000000` XIP base. The tool selects the address
from the drive letter; do not substitute manual picotool writes.

Disk-only updates never write firmware, the boot package, journal, environment
or unselected disk slots. A pending journal, failed write or failed verification
aborts without further writes or reboot; recover/investigate before retrying.
Writes to multiple drives are not atomic: earlier selected drives may already
have been replaced when a later write fails.

### VS Code disk flashing tasks

Open **Tasks: Run Task** from the Command Palette and select one of:

| Task | Replaced drives | Prepared images in `build/cpm-stage09-loader/` |
| --- | --- | --- |
| **Flash Pico - All disks (A-D)** | A, B, C, D | All four images below |
| **Flash Pico - Drive A** | A only | `drive_a_cpm63k-z80.img` |
| **Flash Pico - Drive B** | B only | `drive_b_bdsc.img` (BDS C) |
| **Flash Pico - Drive C** | C only | `drive_c_escape.img` (Escape) |
| **Flash Pico - Drive D** | D only | `drive_d_blank.img` (blank) |

These tasks use existing prepared native images; they do not rebuild firmware
or regenerate disks. The images must already exist and contain the files you
intend to deploy. A fresh checkout needs the
[CP/M image-build workflow](../system/firmware-build.md#building-the-full-image-set)
first. Drive A must match the installed boot package; the flash tool checks this
before writing any selected disk.

The confirmation defaults to **Cancel**. Select **Replace selected disks** only
after saving CP/M work and applying external +5 V before USB. Each task activates
`.venv-flash`, checks its interpreter/serial dependency, then runs the maintained
`pico_flash.py disks` workflow described above. No backups are taken.

The all-disks task supplies four assignments to **one** invocation, so it stops,
checks the journal and reboots only once. Individual tasks write only their named
drive. Firmware, boot data, journal, ENV and unselected drives are preserved.
The tasks perform no postboot probes and do not bypass a failed clean stop,
pending journal replay or verification failure.

## Preparation and journal diagnostics

For diagnosis, prepare the target without programming any flash:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py prepare --power-confirmed
```

`prepare` still disconnects picocom, stops/flushes the Stage 9 CPU, enters
BOOTSEL and checks the journal. It leaves the Pico in BOOTSEL; it is not a
passive status query. Routine firmware/disk commands need no separate
preparation invocation.

To repeat only the read-only journal check on a device **already in BOOTSEL**:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py journal --serial DEVICE_SERIAL
```

The check reads eight 256-byte headers, one per 8 KiB journal pair starting at
`0x10290000`. All must be erased (`0xff`). A non-erased or short header fails
closed; never erase a journal to bypass recovery. Temporary journal reads and
Drive A compatibility reads are safety checks, not disk backups.

## Flash command options

Options follow the subcommand. The following apply to `firmware`, `disks`
and `prepare`:

| Option | Meaning |
| --- | --- |
| `--power-confirmed` | Required confirmation that external +5 V remains applied |
| `--serial DEVICE_SERIAL` | Select the intended Pico by USB serial number |
| `--port PORT` | Restrict initial USB serial-port discovery |
| `--timeout SECONDS` | Stage 9 response timeout; finite positive number, default 60 seconds |
| `--no-disconnect` | Refuse occupied ports rather than gracefully closing picocom |
| `--stopped` | Skip serial stop/flush only after the operator confirmed a safe clean stop |
| `--bootsel` | Target already in ROM BOOTSEL; requires `--stopped --serial DEVICE_SERIAL` and bypasses serial commands |

`firmware` takes one UF2 path. `disks` requires one to four unique
`--drive DRIVE=IMAGE` assignments. Both support `--no-reboot`; `prepare` does
not because it always leaves BOOTSEL. The `journal` subcommand takes only
the required `--serial DEVICE_SERIAL` selector.

## Standalone Stage 9 supervisor commands

Unlike flashing, standalone supervisor commands **refuse occupied ports**
instead of disconnecting picocom. Close the terminal first. Connections use
raw 115200 8N1 with no flow control, DTR asserted and RTS deasserted, and close
on completion or failure.

| Command | Behavior |
| --- | --- |
| `ports` | List serial devices without opening them; no Stage 9 protocol required |
| `status` | Send Ctrl+] then `s` and print the diagnostic status; not a full health qualification |
| `help` | Send Ctrl+] then `h` and print the firmware's supervisor menu |
| `stop` | Query status, then stop/flush if running; confirm clean storage and the CPU held stopped |
| `wait-boot` | Await a fresh CP/M prompt, then require a healthy Stage 9 status; does not reset the Pico |

For example, to query the current status:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_supervisor.py status
```

Replace `status` with `help` or `stop` when that action is intended.
**Stop is not reboot:** Ctrl+] then `x` stops/flushes the Z80; after stop
confirmation, Ctrl+] then `r` is the firmware's separate reboot action.

Supervisor commands accept `--serial DEVICE_SERIAL`, `--port PORT`, and
`--timeout SECONDS` (finite positive number, default 30 seconds).
For a separately requested boot check, `wait-boot --serial DEVICE_SERIAL`
can await USB serial reappearance, then a fresh prompt and healthy status.
Attaching to an already-idle prompt may time out; use `status` rather than
resetting. Do not add this check to routine flashing.

## Troubleshooting and host tests

- **Missing `serial` module:** check the printed interpreter and install/restore
  `scripts/requirements-flash.txt` in `.venv-flash` only if dependencies are
  missing. Keep activation, preflight and the command in one shell invocation.
- **No unique Pico:** run `ports`, confirm the USB connection, then select
  the intended serial number. An already-BOOTSEL device has no application
  serial port; use the explicit BOOTSEL path only after a confirmed clean stop.
- **Port owned:** close picocom for standalone supervisor access. Flashing can
  gracefully close verified picocom processes but never unknown owners.
- **Stop timeout or storage failure:** preserve the output and investigate the
  currently running firmware; no clean stop has been established.
- **Write/verification failure:** do not assume rollback or request a blind
  reboot. Inspect which writes completed and the actual device state.

Use `--help` after either script name or a flash subcommand for its CLI usage;
this does not access hardware. Supervisor `help` without the leading `--`
instead communicates with the running Stage 9 firmware.

Host regression tests do not stop or flash hardware:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python -m unittest discover -s scripts -p 'test_pico*.py' -v &&
python -m unittest discover -s src/disks -p 'test_*.py' -v
```
