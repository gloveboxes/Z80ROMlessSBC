# D.2 CP/M Memory Map and Entry Points

The reset-ready SRAM image uses the following upper-memory layout:

| Region | Address range | Role |
| --- | --- | --- |
| Page zero | `0x0000`-`0x00FF` | CP/M vectors, default FCBs, DMA buffer, and command tail |
| Transient Program Area (TPA) | `0x0100`-`0xE5FF` | `.COM` program, dcc runtime, static data, heap, and stack |
| CCP | `0xE600`-`0xEDFF` | Console Command Processor, reloaded after a warm boot |
| BDOS code and tables | `0xEE00`-`0xFB3F` | CP/M console, file, and disk service layer; entry at `0xEE06` |
| Unused BDOS padding | `0xFB40`-`0xFB7F` | 64 bytes |
| Reserved BDOS padding | `0xFB80`-`0xFBFF` | 128-byte BIOS directory buffer |
| BIOS core | `0xFC00`-`0xFEFD` | 766-byte Z80-optimized boot, console, disk, translation and startup-report routines |
| Spare padding | `0xFEFE`-`0xFEFF` | 2 bytes |
| BIOS bank extension | `0xFF00`-`0xFFFF` | READ/WRITE/INFO/HEAP entry points, mirrored code and private workspace |

The TPA contains `0xE500` bytes, or 58,624 bytes, before the CCP boundary. A
dcc program and every runtime block selected for it must fit in this space
together with its stack and heap. The CCP occupies `0x0800` bytes and the BDOS
slot occupies `0x0E00` bytes; the BDOS slot's final 128 bytes are reserved as
the BIOS directory buffer. The dcc runtime reads the installed
`JP 0xEE06` vector at `0x0005` to locate BDOS; application memory must still
remain below the CCP boundary at `0xE600`. Required CCP/BDOS workspace, the
allocation vectors, disk state, and internal stacks remain inside their
relocated resident regions.

The BIOS cold boot installs `JP` instructions at `0x0000` and `0x0005` for
warm boot and BDOS respectively. A dcc program normally starts at `0x0100` and
returns to CP/M through the warm-boot vector. The custom warm boot reloads
the 44 CCP/BDOS system records before returning to the command prompt,
restoring the resident image. The supported TPA above excludes the CCP.

The complete 1,024-byte `bios.bin` includes the core, two bytes of padding,
and the extension page. The [public BIOS API](bios.md#implemented-banked-memory-bios-extension-abi-24) is installed in
both SRAM banks. The alternate bank has this layout:

| Bank 1 offset | Role |
| --- | --- |
| `0x0000`-`0x07FF` | Extent tables, allocator code and private stack; eight blocks reserved |
| `0x0800`-`0xFEFF` | 63,232-byte application heap |
| `0xFF00`-`0xFFFF` | Mirrored BIOS extension and private workspace; reserved |

The C banked library calls the BIOS API directly. An application can allocate
separate code and stack blocks in the heap and execute the code through HEAP
operation 5 with a stack handle in BC. CP/M’s DMA area and dcc runtime require
no changes. Deploy matching firmware, boot package and drive A system records
together when changing this memory layout.
