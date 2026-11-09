# CP/M disk media

`native/` holds the four checked-in drive images. Each is exactly 524,288
bytes (128 tracks of 32 records of 128 bytes), the same format the Pico flash
slots and the host emulator use, so no conversion step exists.

| Flash drive | Image | Contents |
| ---: | --- | --- |
| A | `drive_a_cpm63k.img` | CP/M 2.2 files; `build_images.py` replaces its system tracks and `AUTORUN.COM` |
| B | `drive_b_bdsc.img` | BDS C tools plus the current banked-memory examples |
| C | `drive_c_escape.img` | Escape applications and games |
| D | `drive_d_blank.img` | DOCTOR/DOCGEN ISAM demonstration database |

The CP/M files originate from the Altair 8800 disk archive at
<https://github.com/companje/Altair8800/tree/master/data>; its license is in
`native/LICENSE`. Drive A's system tracks still carry Altair-specific content,
so provision `build/cpm/drive_a_cpm63k-z80.img`, never the checked-in image.

## Building images

Run the CP/M image builder to replace Drive A's two system tracks with
sequential CCP/BDOS records, the board-native BIOS, and a disabled native
auto-run configuration record. The builder also replaces user 0's `AUTORUN.COM`
with the native utility; it requires Python 3.9+, `z80asm`, and `cpmtools`
(`cpmcp` and `cpmrm`):

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

Drives C and D are copied unchanged. Drive B receives the current
banked-memory example binaries (`ALLOCPRF`, `BANKED`, `MEMPERF`, `PRIMEBK1`,
and `DATA.LIB`/`DATATEST.COM` when both are built). Supplying Stage 10's
firmware binary also creates a complete fixed-layout flash image:

```sh
python3 src/cpm/build_images.py \
  --firmware build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.bin \
  --output-dir build/cpm
```

CMake runs the same builder and rebuilds the CP/M images when any `native/`
image, the builder, or the geometry changes.

Native auto-run settings occupy only drive A's final reserved system record
(LBA 63, image offset `0x1F80`), outside the filesystem and boot code.
Replacing the whole drive A image resets these settings to disabled. See
[native auto-run](../cpm/README.md#native-auto-run) for the menu, record
format, and boot-time recovery bypass.

## Keeping the checked-in images current

Interactive emulator sessions write to `build/cpm/drive_*.img`, which
`build_images.py` regenerates from `native/`. After the session ends, run
`python3 src/disks/save_session_disk.py D` (or `A`, `B`, `C`) to store the
modified image in `native/`, then commit it. Drive A keeps only its file
changes, because its system tracks are rebuilt.

After rebuilding the banked-memory examples, run
`python3 src/disks/refresh_drive_b.py` to install them into
`native/drive_b_bdsc.img` and remove the retired `SPREADS`, `SPRDHOST` and
`LIBHOST` files. `test_native_disks.py` fails if drive B and the examples
differ.

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
