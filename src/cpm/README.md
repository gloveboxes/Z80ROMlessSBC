# Native CP/M boot image

The board runs a 64K CP/M 2.2 system generated from the preserved
`src/disks/source-altair/cpm63k.dsk`, but replaces its Altair controller BIOS.
The memory map is:

| Region | Z80 address | Size |
| --- | ---: | ---: |
| CCP | `0xE600` | 2,048 bytes |
| BDOS | `0xEE00` | 3,584 bytes |
| BIOS | `0xFC00` | 698 bytes currently |

The checked-in `cpm64_system.bin` was produced by the source disk's matching
`MOVCPM 64 *` and `SYSGEN` utilities. It contains the 44 relocated CCP/BDOS
records and is protected by a SHA-256 check in `build_images.py`. Page zero
uses BDOS entry `0xEE06`, and the custom BIOS jump table begins at `0xFC00`.
The TPA is `0x0100`-`0xE5FF` (58,624 bytes), 1,792 bytes larger than the
previous 63K-derived configuration.

`bios.asm` is assembled at `0xFC00`. It uses terminal data/status ports
`0x00`/`0x01` and disk command, drive, LBA-low, LBA-high, and data ports
`0x10`-`0x14`. Warm boot reloads 44 sequential records from native Drive A.
The BIOS uses an identity `SECTRAN` because disk conversion has already removed
the Altair skew.

Disk geometry lives once in `src/disks/geometry.py`. The host builder converts
it to assembler equates, assembles the BIOS, builds the reset-ready SRAM image,
adds the firmware manifest and CRCs, replaces Drive A's reserved tracks, and
optionally composes the complete 4 MiB flash image.

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

`z80boot.pkg` has a 20-byte little-endian manifest at offset zero, `0xFF`
padding through offset `0x0FFF`, and a 65,536-byte SRAM image at offset
`0x1000`. The SRAM image starts with `JP 0xFC00`; cold boot then installs the
normal CP/M warm-boot and BDOS vectors before entering the CCP.
