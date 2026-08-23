# CP/M disk media

`source-altair/` contains byte-for-byte copies from:

`/Users/dave/GitHub/esp32/esp32-altair-8800`

The original source is recorded in that project's `disk_archive/README.md` as
<https://github.com/companje/Altair8800/tree/master/data>. The copied license is
in `source-altair/LICENSE`.

## Formats

The source `.dsk` files use the Altair 88-DCDD layout: 77 tracks, 32 sectors
per track, and 137 bytes per physical sector. Each sector contains a 3-byte
header, 128-byte CP/M record, and 6-byte trailer. Some system images append a
96-byte marker after the complete sector grid.

The Pico firmware does not consume that framing. Run:

```sh
python3 src/disks/convert_altair_disks.py
```

This writes four exact 327,680-byte files under `generated/`:

| Flash drive | Generated image | Source image |
| ---: | --- | --- |
| A | `drive_a_cpm63k.img` | `cpm63k.dsk` |
| B | `drive_b_bdsc.img` | `bdsc-v1.60.dsk` |
| C | `drive_c_escape.img` | `escape-posix.dsk` |
| D | `drive_d_blank.img` | `blank.dsk` |

The converter applies the source BIOS's 32-sector translation table, extracts
every 128-byte record, and pads the three additional tracks with `0xE5`. It
also writes `generated/manifest.json` with source and output SHA-256 values.
The plain generated Drive A is an intermediate image: its system tracks still
contain Altair-specific content, while its directory and file records from
track 2 onward are final.

Run the CP/M image builder to replace those two system tracks with sequential
CCP/BDOS records and the board-native BIOS:

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

Provision `build/cpm/drive_a_cpm63k.img`, not the same-named intermediate file
under `generated/`. Drives B-D are copied unchanged. Supplying Stage 10's
firmware binary also creates a complete fixed-layout flash image:

```sh
python3 src/cpm/build_images.py \
  --firmware build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.bin \
  --output-dir build/cpm
```

## BIOS compatibility

`source-altair/altair_88dskrom.h` and `altair_disk_loader.h` are retained as
references only. They are Altair 88-DCDD boot ROMs that use ports
`0x08`-`0x0A`; they are not a BIOS for this board and are not compiled into the
Pico firmware.

The source CP/M image contains an Altair BIOS and the DPB
`SPT=32, BSH=4, BLM=15, EXM=0, DSM=149, DRM=63, AL0=0xC0, CKS=16, OFF=2`.
The image builder installs a replacement BIOS for terminal ports `0x00`-`0x01`
and disk ports `0x10`-`0x14`, with the final geometry sourced from
`geometry.py`. See `src/cpm/README.md` for the recovered CP/M memory map and
boot-package details.
