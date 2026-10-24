# Pico build, flashing, and USB tools

Run these tools from the **Z80ROMlessSBC project folder**. Use the VS Code
tasks for routine builds and deployment; use the scripts directly for device
selection or diagnostics.

## Setup

Follow [Toolchain](../toolchain.md) for the complete Arm GNU toolchain, Pico
SDK, Python 3.11+, and `picotool`. Flashing uses macOS/Linux shell tools
(`lsof` and `ps`) and a Python environment:

```sh
python3 -m venv .venv-flash
.venv-flash/bin/python -m pip install -r scripts/requirements-flash.txt
```

!!! warning "Before flashing"
    Save CP/M work and finish any running hardware test. Apply external
    **+5 V before USB** and keep it applied. Disk updates overwrite selected
    drives without backups.

## VS Code build and firmware tasks

Open the Command Palette, choose **Tasks: Run Task**, then select:

| Exact task name | Action |
| --- | --- |
| **Build and flash Pico - Phase 9 firmware** | Select diagnostic or automatic loader; preserves disks and ENV |
| **Build and flash Pico - Phase 9 firmware and disks** | Select variant; replaces complete flash image, including disks and ENV |
| **Flash Pico - Phase 6 20 MHz scope diagnostic** | Builds and force-flashes the isolated CPU/SRAM scope diagnostic |
| **Build and flash Pico - Phase 10 firmware** | Builds and force-flashes firmware; preserves disks and ENV |
| **Build and flash Pico - Phase 10 firmware and disks** | Builds and writes the complete flash image; replaces disks and ENV |
| **Prepare interactive CP/M emulator** | Builds the host emulator, I/O adapter, and CP/M images; no Pico required |

Phase 9 and 10 tasks both build and deploy. They force-update without a
previous-firmware handshake, verify written bytes, and request reboot.
Firmware-only tasks preserve disks and ENV; firmware-and-disks tasks replace
the complete flash image. Save work before using them.
Phase 9 tasks prompt for **Diagnostic** (bring-up) or **Automatic loader**
(normal operation after tests pass).

For a new board or coordinated BIOS/boot-package changes, follow
[Firmware build and provisioning](../system/firmware-build.md).
Use the [Phase 9](../implementation/phase-9-flash-storage.md) and
[Phase 10](../implementation/phase-10-websocket.md) guides in bring-up order.

## Deployment task requirements

Build/flash pairs are available for Phases 1–6 and 8, including optional
Phase 6 memory screens and soak tests. Each phase page lists its exact tasks.
Phase 0 has no firmware.

- **Build Pico - …** builds only.
- **Flash Pico - …** tasks build, force BOOTSEL entry, verify firmware, and
  reboot without checking or handshaking with the previous phase firmware.
- Save work before flashing and follow each phase's power/device requirements.
  Initial Phase 1 programming uses the Pico off the breadboard.
- If firmware cannot respond to picotool's USB boot request, enter BOOTSEL
  manually. This does not require a particular previous phase.

### Firmware and disks

The **Build and flash Pico - Phase 9 firmware and disks** and
**Build and flash Pico - Phase 10 firmware and disks** tasks build and write
the complete flash image. They replace firmware, boot package, disks,
journal, and ENV without backups. They require USB serial selection and
explicit confirmation, and force BOOTSEL entry without a firmware handshake.
Use firmware-only tasks to preserve existing disks and settings.

## VS Code disk flashing tasks

Choose **Tasks: Run Task**, then one of:

| Exact task name | Source in `src/disks/source-altair/` |
| --- | --- |
| **Flash Pico - All disks (A-D)** | All four sources below |
| **Flash Pico - Drive A** | `cpm63k.dsk` |
| **Flash Pico - Drive B** | `bdsc-v1.60.dsk` |
| **Flash Pico - Drive C** | `escape-posix.dsk` |
| **Flash Pico - Drive D** | `blank.dsk` |

- Select **Replace selected disks** at the confirmation prompt.
- A: is regenerated with native CP/M system tracks and `AUTORUN.COM` under
  `build/cpm-disk-update/`; this requires `z80asm` and `cpmtools`.
- B:–D: are validated and converted at flash time.
- Tasks force BOOTSEL entry without a firmware handshake, check the journal,
  verify selected writes, and reboot
  once. They preserve firmware, the boot package, ENV, and unselected drives.
- A: must match the installed boot package; an incompatibility aborts the update.

These tasks do not compile applications. Update the source disk first; see
[Related projects](../related-projects.md) for building and importing apps.

## Maintained scripts

| Script | Purpose |
| --- | --- |
| `scripts/pico_build.py` | Finds a complete Arm GNU/Newlib toolchain, configures CMake, and builds the requested target |
| `scripts/pico_flash.py` | Verified firmware/disk writes, BOOTSEL preparation, and journal checks |
| `scripts/pico_supervisor.py` | Serial discovery, status, help, clean stop, and optional boot checks |
| `src/disks/convert_altair_disks.py` | Converts validated Altair disk images to native images |
| `src/cpm/build_images.py` | Builds board-native CP/M disks, boot packages, and optional complete flash images |

Use `--help` for current options. For example:

```sh
python3 scripts/pico_build.py --build-dir build --target z80_stage09_flash_storage
.venv-flash/bin/python scripts/pico_supervisor.py ports
.venv-flash/bin/python scripts/pico_flash.py --help
```

## Firmware updates

For a matching, already provisioned installation:

```sh
.venv-flash/bin/python scripts/pico_flash.py firmware \
  build/src/stage09_flash_storage/z80_stage09_flash_storage.uf2 \
  --power-confirmed
```

The CLI normally disconnects picocom and requests a clean stop/flush before
flashing. `--force` skips that handshake for firmware and disk updates, as the VS Code
deployment tasks do. Unknown serial owners or failed verification
abort the update.

Firmware writes must stay below flash offset `0x290000`. They preserve disks
and boot data. Verification confirms written bytes, not a successful CP/M
boot; check operation separately using the phase guide.

## Selective disk updates without backups

For custom disk assignments:

```sh
.venv-flash/bin/python scripts/pico_flash.py disks \
  --drive B=src/disks/source-altair/bdsc-v1.60.dsk \
  --drive C=src/disks/source-altair/escape-posix.dsk \
  --power-confirmed
```

Repeat `--drive DRIVE=IMAGE` for unique A:–D: slots. Native images must be
327,680 bytes. Altair images are accepted for B:–D:; A: requires a prepared
native image matching the installed boot package. The VS Code A: task handles
that preparation automatically.

A pending journal or failed verification stops the update. Multiple-drive
writes are not atomic; earlier drives may already be replaced if a later
write fails.

## Device selection and diagnostics

The tools select a unique Pico automatically. Run `pico_supervisor.py ports`
to list devices; use `--serial DEVICE_SERIAL` when several are connected.
Standalone supervisor commands require the serial terminal to be closed.

| Command after `scripts/pico_supervisor.py` | Purpose |
| --- | --- |
| `ports` | List serial devices without opening them |
| `status` | Read supervisor status |
| `help` | Read the running firmware's command menu |
| `stop` | Stop/flush CP/M and confirm clean storage |
| `wait-boot` | Await a fresh CP/M prompt and check status; does not reset |

The supervisor uses the Phase 9-style USB protocol implemented by the running
firmware. For other bring-up firmware, follow its phase instructions.

Additional `pico_flash.py` options:

| Option or command | Purpose |
| --- | --- |
| `--port PORT` | Restrict serial-port discovery |
| `--no-disconnect` | Refuse a busy port instead of closing picocom |
| `--stopped` | Skip stop/flush after an operator-confirmed clean stop |
| `--bootsel --stopped --serial DEVICE_SERIAL` | Select a device already in BOOTSEL |
| `--no-reboot` | Leave BOOTSEL after firmware/disk writes |
| `prepare --power-confirmed` | Stop/flush, enter BOOTSEL, and check the journal without writing |
| `journal --serial DEVICE_SERIAL` | Read-check journal headers on a device already in BOOTSEL |

`prepare` changes device state; it is not a passive status query. Investigate
a failed stop or pending journal rather than bypassing it.

## Troubleshooting

- **Missing `serial`:** install `scripts/requirements-flash.txt` in `.venv-flash`.
- **No unique Pico:** list ports and select the intended USB serial number.
- **Busy port:** close the terminal; flashing can close picocom, but not unknown owners.
- **Failed stop or write:** retain the output and investigate before retrying.

For host-only script checks:

```sh
.venv-flash/bin/python -m unittest discover -s scripts -p 'test_pico*.py' -v
.venv-flash/bin/python -m unittest discover -s src/disks -p 'test_*.py' -v
```
