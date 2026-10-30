# D.2 CP/M Memory Map and Entry Points

The reset-ready SRAM image uses the following upper-memory layout:

| Region | Address range | Role |
| --- | --- | --- |
| Page zero | `0x0000`-`0x00FF` | CP/M vectors, default FCBs, DMA buffer, and command tail |
| Transient Program Area (TPA) | `0x0100`-`0xE5FF` | `.COM` program, dcc runtime, static data, heap, and stack |
| CCP | `0xE600`-`0xEDFF` | Console Command Processor, reloaded after a warm boot |
| BDOS shim and tables | `0xEE00`-`0xFB3F` | Bank-0 console and disk/file proxy; entry at `0xEE06` |
| BDOS shim private stack | `0xF600`-`0xF7FF` | 512-byte stack inside the bank-0 BDOS slot; protects the CCP code below its small stack |
| Unused BDOS padding | `0xFB40`-`0xFB7F` | Mirrored bank-1 warm-boot return trampoline |
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
and the extension page. The [public BIOS API](bios.md#banked-memory-bios-extension-abi-26)
is installed in both SRAM banks. Production bank 0 contains a BDOS disk/file
shim in the `EE00h–FB3Fh` slot; the authoritative BDOS resides in bank 1.
The Drive A system tracks contain that shim for warm boot.

The bank-0 shim saves the caller's stack pointer and switches to its private
stack before forwarding disk calls. It restores the caller's stack before
returning. This keeps nested bank transfers away from the CCP instructions
immediately below the CCP stack.

| Bank-1 address | Role |
| --- | --- |
| `0000h–00FFh` | CP/M page zero |
| `0100h–(heap_base-1)` | Fixed DCC library image, statics and BSS |
| `heap_base–(C800h-stack-1)` | DCC library `malloc` heap, starting after linked BSS |
| `(C800h-stack)–C7FFh` | Pinned library stack; size comes from the `.LIB` header |
| `C800h–DDFFh` | General `bmalloc` pool, 5,632 bytes |
| `DE00h–E5FFh` | Resident loader, proxy and service workspace |
| `E600h–EDFFh` | Extent allocator metadata/code |
| `EE00h–FB3Fh` | Authoritative CP/M BDOS |
| `FB40h–FB7Fh` | Mirrored warm-boot trampoline |
| `FB80h–FEFFh` | BIOS buffer and core |
| `FF00h–FFFFh` | Mirrored bank-switch gateway |

Boot loads and verifies bank-1 page zero and every byte from `DE00h` through
`FFFFh`. The `0100h–DDFFh` application, library stack, and shared heap range
is not cleared at boot; those regions are written by their owners before use.

Bank 0 cannot dereference bank-1 addresses. It uses the BIOS transfer calls
for parameter blocks and the direct-offset call for exported functions. See
the [fixed-address library](bank1-library-spec.md). Firmware and the CP/M
system image must match when the bank-1 resident layout changes.
