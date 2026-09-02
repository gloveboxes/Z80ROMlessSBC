# D.2 CP/M Memory Map and Entry Points

The reset-ready SRAM image uses the following upper-memory layout:

| Region | Address range | Role |
| --- | --- | --- |
| Page zero | `0x0000`-`0x00FF` | CP/M vectors, default FCBs, DMA buffer, and command tail |
| Transient Program Area (TPA) | `0x0100`-`0xE6FF` | `.COM` program, dcc runtime, static data, heap, and stack |
| CCP | `0xE700`-`0xEEFF` | Console Command Processor, reloaded after a warm boot |
| BDOS | `0xEF00`-`0xFCFF` | CP/M console, file, and disk service layer; entry at `0xEF06` |
| BIOS | `0xFD00`-`0xFFC5` | 710-byte Z80-optimized boot, console, disk, and translation routines |

The TPA contains `0xE600` bytes, or 58,880 bytes, before the CCP boundary. A
dcc program and every runtime block selected for it must fit in this space
together with its stack and heap. The dcc runtime reads the installed
`JP 0xEF06` vector at `0x0005` to locate BDOS; application memory must still
remain below the CCP boundary at `0xE700`. Required CCP/BDOS workspace, the
BIOS directory buffer and allocation vectors, disk state, and internal stacks
remain inside their relocated resident regions.

The BIOS cold boot installs `JP` instructions at `0x0000` and `0x0005` for
warm boot and BDOS respectively. A dcc program normally starts at `0x0100` and
returns to CP/M through the warm-boot vector. Because a transient program may
overwrite the resident CCP while using the TPA, the custom warm boot reloads
the 44 CCP/BDOS system records before returning to the command prompt.
