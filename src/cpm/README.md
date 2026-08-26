# Native CP/M boot image

The board runs a 64K CP/M 2.2 system generated from the preserved
`src/disks/source-altair/cpm63k.dsk`, but replaces its Altair controller BIOS.
The memory map is:

| Region | Z80 address | Size |
| --- | ---: | ---: |
| CCP | `0xE300` | 2,048 bytes |
| BDOS | `0xEB00` | 3,584 bytes |
| BIOS | `0xF900` | 703 bytes currently |

The checked-in `cpm64_system.bin` was captured from RAM after the matching
Burcon `MOVCPM 64` and `SYSGEN` utilities produced and booted a 64K system. It
contains the 44 relocated CCP/BDOS records and is protected by a SHA-256 check
in `build_images.py`. Page zero uses BDOS entry `0xEB06`, and the custom BIOS
jump table begins at `0xF900`. The TPA is `0x0100`-`0xE2FF` (57,856 bytes),
1,024 bytes larger than the previous 63K configuration.

`disassemble_cpm64.py` recursively disassembles the exact fingerprinted image
from the CCP vectors and all indirect CCP/BDOS dispatch tables. It generates
`cpm64_i8080.asm` with Intel 8080 mnemonics and leaves unvisited strings,
tables, and workspace as byte data. See `cpm64_i8080_audit.md` for the
correctness and optimization review. The boot-proven `cpm64_system.bin` remains
immutable. The builder instead assembles the generated `cpm64_z80.asm` port.
Its unoptimized generation mode reproduces the immutable image byte-for-byte;
the active port uses 242 safe relative branches and three independently checked
substitutions to reclaim 245 bytes while preserving the fixed section ABI.

Use the matching
[Burcon CP/M 2.2 utilities](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/)
when reproducing this artifact: run that archive's `MOVCPM.COM` and
`SYSGEN.COM` as described in its
[Using MOVCPM guide](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/Using%20MOVCPM.pdf).
Burcon reserves `0x0700` bytes for its BIOS and reports `SAVE 38` after
`MOVCPM 64`. The SBC replaces the machine BIOS with `z80_bios.asm` while preserving
the Burcon `0xE300`/`0xEB00`/`0xF900` memory ABI.

`z80_bios.asm` is assembled at `0xF900`. It uses terminal data/status ports
`0x00`/`0x01` and disk command, drive, LBA-low, LBA-high, and data ports
`0x10`-`0x14`. Warm boot reloads 44 sequential records from native Drive A.
The source deliberately uses the Z80 instruction set, including `DJNZ`,
relative branches, rotate/carry status polling, carry-to-mask `SBC A,A`, and
`INIR`/`OTIR` block I/O; it is not restricted to the 8080-compatible subset.
The BIOS uses an identity `SECTRAN` because disk conversion has already removed
the Altair skew. It forwards CP/M's standard write type to the Pico so normal
writes can be coalesced in a 4 KiB cache, while directory writes and warm boot
flush through the journal. A 250 ms idle deadline also persists an isolated
normal overwrite. `LIST`, `PUNCH`, and `READER` remain in their mandatory
jump-table positions, but no devices are implemented: printer and punch output
is discarded, reader input returns CP/M text EOF (`0x1A`), and `LISTST` reports
not ready. The existing aligned
console (`0x00`) and disk (`0x10`) port groups are retained for direct-I/O
compatibility; changing their values would not reduce the number of I/O traps.

Disk geometry lives once in `src/disks/geometry.py`. The host builder converts
it to assembler equates, assembles the BIOS, builds the reset-ready SRAM image,
adds the firmware manifest and CRCs, replaces Drive A's reserved tracks, and
optionally composes the complete 4 MiB flash image.

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

`z80boot.pkg` has a 20-byte little-endian manifest at offset zero, `0xFF`
padding through offset `0x0FFF`, and a 65,536-byte SRAM image at offset
`0x1000`. The SRAM image starts with `JP 0xF900`; cold boot then installs the
normal CP/M warm-boot and BDOS vectors before entering the CCP.
