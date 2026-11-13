# CP/M disk media

`native/` holds the four checked-in drive images. Each is exactly 524,288
bytes (128 tracks of 32 records of 128 bytes), the same format the Pico flash
slots and the host emulator use.

| Flash drive | Image | Contents |
| ---: | --- | --- |
| A | `drive_a_cpm63k.img` | CP/M 2.2 files; `build_images.py` replaces its system tracks and `AUTORUN.COM` |
| B | `drive_b_bdsc.img` | BDS C tools, banked-memory examples and `FT.COM` |
| C | `drive_c_escape.img` | Escape applications, games and `FT.COM` |
| D | `drive_d_blank.img` | DOCTOR/DOCGEN ISAM demonstration database |

Both `FT.COM` copies use [HTTP shared-service group 0x70](../../docs/docs/en/system/io-ports.md#http-and-https-file-transfer).
Rebuild and validate the assembly client with
[`test_http_ft.py`](../cpm/test_http_ft.py), then replace only `FT.COM` on B/C
session images and save them with `save_session_disk.py`.

The CP/M files originate from the Altair 8800 disk archive at
<https://github.com/companje/Altair8800/tree/master/data>; its license is in
`native/LICENSE`. Drive A's system tracks are rebuilt from this checkout before
use; provision `build/cpm/drive_a_cpm63k-z80.img`, never the checked-in image.

## Copying ESP32 application disks

From the ESP32 Altair project's `Apps` directory, build its applications and
copy the four disks directly into this project's `native/` directory:

```sh
pwsh ./build-all.ps1 -c /path/to/Z80ROMlessSBC/src/disks/native
```

Use `-CloneOnly -c ...` to copy the current disks without rebuilding them.
The destination directory must exist. Copying replaces all four checked-in
images, including D's database; keep wanted file changes before copying.
Stop the ESP32 and SBC emulators before building or copying disks.

`native/` is the single disk source for **Run interactive CP/M emulator**,
**Refresh CP/M container and session disks**, and every Pico image/flash task.
After copying, run the emulator or **Flash Pico - All disks (A-D)** directly.
Both use the same builder: validate the four disks, patch A with this checkout's
CP/M, BDOS, BIOS and AUTORUN, and copy B-D unchanged. No import task is needed.
Applications must use the SBC port map and supported services.

The emulator writes prepared copies in `build/cpm-session/`; the all-disks
flash task writes prepared copies in `build/cpm-disk-update/` before deployment.
Neither preparation changes `native/`. Flashing requires explicit confirmation
and safe power sequencing; it replaces A-D and their matching boot package
without backups, preserving Pico firmware and ENV.

## Building images

Run the CP/M image builder to replace Drive A's two system tracks with
sequential CCP/BDOS records, the board-native BIOS, and a disabled native
auto-run configuration record. The builder also replaces user 0's `AUTORUN.COM`
with the native utility. On macOS, the cached Apple container supplies
`z80asm` and `cpmtools` (`cpmcp` and `cpmrm`):

```sh
python3 scripts/z80_container.py images --output-dir build/cpm
```

Drives B, C and D are copied unchanged. Supplying Stage 10's firmware binary
also creates a complete fixed-layout flash image:

```sh
python3 scripts/z80_container.py images \
  --firmware build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.bin \
  --output-dir build/cpm
```

On macOS CMake runs the image builder in Apple Container. Other hosts require
Python 3.9+, `z80asm` and `cpmtools`. CMake rebuilds the CP/M images when
any `native/` image, the builder, or the geometry changes.
The builder checks exact disk sizes and uses `fsck.cpm -n` to validate source
and prepared filesystems before writing outputs. Validation failure stops the
task before output replacement or flashing. Filesystem checks do not establish
application behavior.

Native auto-run settings occupy only drive A's final reserved system record
(LBA 63, image offset `0x1F80`), outside the filesystem and boot code.
Replacing the whole drive A image resets these settings to disabled. See
[native auto-run](../cpm/README.md#native-auto-run) for the menu, record
format, and boot-time recovery bypass.

## Keeping the checked-in images current

Interactive emulator sessions write to `build/cpm-session/drive_*.img`, which
the container launcher regenerates from `native/`. After the session ends, run
`python3 src/disks/save_session_disk.py D` (or `A`, `B`, `C`) to store the
modified image in `native/`, then commit it. Drive A keeps only its file
changes, because its system tracks are rebuilt.

## Selective flashing without backups

From the repository root, use the maintained tool rather than manual write
commands:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py disks \
  --drive C=src/disks/native/drive_c_escape.img \
  --drive D=src/disks/native/drive_d_blank.img \
  --power-confirmed
```

Repeat `--drive DRIVE=IMAGE` for one or more unique A-D slots. The development
workflow makes no backups: it disconnects picocom, confirms stop/flush, checks
the journal, and writes only selected slots with read-back verification. It
reboots once after all selected writes, without testing the booted firmware;
`--no-reboot` leaves BOOTSEL instead.

Images must be exactly 524,288 bytes. Drive A must be a matching board-native
image built by `build_images.py`; its warm-boot CCP/BDOS records are checked
against the installed, CRC-validated boot package before any selected disk is
written. Sources and unselected flash regions are unchanged.

See the [flashing-tool setup and safety workflow](../../docs/docs/en/system/firmware-build.md#72-flash-provisioning).

Selective writes require the installed firmware's exact `z80sbc-storage:`
layout metadata, checked before journal reads or storage writes. An existing
512 KiB installation without this metadata needs a firmware-only update first;
its boot package, ENV, and disks are preserved. Drive A and boot packages are
checked for the current BIOS geometry and allocation-map layout as well as
warm-boot records.

## BIOS

The image builder installs the current BIOS for terminal ports `0x00`-`0x01`
and disk ports `0x10`-`0x14`, using the native geometry in `geometry.py`.
See `src/cpm/README.md` for the CP/M memory map and boot-package details.
