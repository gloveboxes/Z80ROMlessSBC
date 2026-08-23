# Native CP/M boot image

The board runs the 63K CP/M 2.2 system preserved in
`src/disks/source-altair/cpm63k.dsk`, but replaces its Altair controller BIOS.
The recovered memory map is:

| Region | Z80 address | Size |
| --- | ---: | ---: |
| CCP | `0xDF00` | 2,048 bytes |
| BDOS | `0xE700` | 3,584 bytes |
| BIOS | `0xF500` | 698 bytes currently |

The mapping was confirmed from a live 64 KiB emulator RAM capture: page-zero
BDOS entry `0xE706` and the BIOS jump table at `0xF500` establish all three
boundaries. Pristine CCP/BDOS bytes are source records 4-47 (one-based), whose
SHA-256 is checked by `build_images.py`. This avoids packaging runtime-mutated
CCP/BDOS state from the capture.

`bios.asm` is assembled at `0xF500`. It uses terminal data/status ports
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
`0x1000`. The SRAM image starts with `JP 0xF500`; cold boot then installs the
normal CP/M warm-boot and BDOS vectors before entering the CCP.
