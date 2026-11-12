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
`native/LICENSE`. Drive A's system tracks still carry Altair-specific content,
so provision `build/cpm/drive_a_cpm63k-z80.img`, never the checked-in image.

## Importing Altair application disks

From the ESP32 Altair project's `Apps` directory, build its applications and
clone the four disks into this project's existing `source-altair/` directory:

```sh
pwsh ./build-all.ps1 -c /path/to/Z80ROMlessSBC/src/disks/source-altair
```

Use `-CloneOnly -c ...` to copy the current disks without rebuilding them.
Stop the emulator before rebuilding or copying the disks. The cloned `.img`
files are local inputs and are not tracked by Git.

In VS Code, run **Tasks: Run Task → Import Altair disks - validate and stage
images**. This validates and copies all four clones into `build/altair-disks/`,
leaving native sources and Pico flash untouched. Validation and image
preparation use `fsck.cpm`, `cpmtools` and `z80asm` in the cached Apple
container. Alternatively, run from the Z80ROMlessSBC root:

```sh
python3 scripts/z80_container.py disk stage
```

The staging tool verifies the exact 524,288-byte size and runs `fsck.cpm -n`
on each image using the native CP/M geometry. It records source and staged
SHA-256 hashes in `manifest.json`. If any check fails, the task fails and leaves
existing staged images and manifest unchanged; those files then represent the
last successful import, not the failed one. Filesystem validation does not
test application behavior.
Use `--drive B` (repeat for other drives) to stage selected disks;
`--source-dir` and `--staged-dir` select other directories.

| Drive | Cloned source | Staged image |
| --- | --- | --- |
| A | `drive_a_cpm63k.img` | `drive_a_cpm63k.img` |
| B | `drive_b_bdsc.img` | `drive_b_bdsc.img` |
| C | `drive_c_escape.img` | `drive_c_escape.img` |
| D | `drive_d_blank.img` | `drive_d_blank.img` |

Run **Tasks: Run Task → Import Altair disks - install selected native drive**,
select the drive, and confirm replacement. Repeat for each desired drive.
The task checks that the staged image matches its current clone and manifest
and rechecks the filesystem before writing. Importing A also rebuilds its
system tracks and `AUTORUN.COM` from the current sources. Individual-drive flash tasks use
the native sources. Alternatively, to import C from the terminal:

```sh
python3 scripts/z80_container.py disk install --drive C --confirm "Replace selected native disk"
```

This overwrites the selected native image. In particular, importing D replaces
the DOCTOR/DOCGEN database. Build the board-native A image and matching boot
package before provisioning it.
The image builder also replaces A's `AUTORUN.COM`. Applications that depend on
specific hardware or services need a board-compatible build.

Run **Import Altair disks - prepare Pico images** to prepare without flashing,
or run `python3 scripts/z80_container.py disk prepare`. It rechecks the staged
images against their current clones, regenerates A's CP/M, BDOS, BIOS and
matching boot package, and validates the resulting filesystems. Outputs are
written to `build/cpm-disk-update/`; B–D are byte-for-byte copies.

**Flash Pico - All disks (A-D)** instead uses all four staged imports directly:
it revalidates the clone, manifest, and filesystems, then builds the native
boot package and A image. It preserves B–D byte-for-byte from the same validated
snapshot.
It replaces the Pico's A-D and boot package without backups, including its D
database, and leaves the checked-in native sources unchanged. The task
requires explicit confirmation and safe power sequencing.

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

The checked-in Drive A files were created under an Altair BIOS with the DPB
`SPT=32, BSH=4, BLM=15, EXM=0, DSM=149, DRM=63, AL0=0xC0, CKS=16, OFF=2`.
The image builder installs a replacement BIOS for terminal ports `0x00`-`0x01`
and disk ports `0x10`-`0x14`, with the final geometry sourced from
`geometry.py`. See `src/cpm/README.md` for the recovered CP/M memory map and
boot-package details.
