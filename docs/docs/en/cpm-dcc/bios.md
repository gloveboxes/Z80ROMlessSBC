# D.3 Custom BIOS Responsibilities

The BIOS implements CP/M 2.2’s standard 17-entry jump table for BDOS and
dcc’s direct-BIOS functions.

| CP/M BIOS operation | Board implementation |
| --- | --- |
| `BOOT` | Initialize CP/M and page-zero vectors; enter CCP |
| `WBOOT` | Flush writes, reload CCP/BDOS from Drive A, and enter CCP |
| `CONST` | Return `0xFF` when terminal status port `0x01`, bit 0, reports input ready |
| `CONIN` | Wait for input on port `0x00`; return a 7-bit character |
| `CONOUT` | Wait for status bit 1 (transmit room); write C to port `0x00` |
| `READER` | Return text EOF (`0x1A`) |
| `LIST` / `PUNCH` | Discard output |
| `LISTST` | Return not ready |
| `HOME` | Set track to zero |
| `SELDSK` | Return the disk parameter header for A–D, or zero for an invalid drive |
| `SETTRK` / `SETSEC` / `SETDMA` | Store track, sector, and software buffer address; no hardware DMA |
| `READ` / `WRITE` | Transfer 128-byte records through ports `0x10`–`0x14`, preserving CP/M write-type semantics |
| `SECTRAN` | Return the sector unchanged; images use linear sector order |

BDOS handles filenames, FCBs, directories, allocation, and record selection.
The BIOS handles drives and records, so dcc file APIs need no custom backend.
See the [I/O port map](../system/io-ports.md) for port decoding and aliases.

## Implemented banked-memory BIOS extension (ABI 1)

Three fixed entries in the reserved top page supplement the standard jump
table. Use this public ABI rather than private workspace or bank-select ports.

| Address | Entry | Purpose |
| --- | --- | --- |
| `FF00h` | READ | Copy bank 1 to a base-bank buffer |
| `FF03h` | WRITE | Copy a base-bank buffer to bank 1 |
| `FF06h` | INFO | Report availability, version, and heap geometry |

Before calling INFO, check the descriptor: `BMX1` at `FF09h`, ABI major `1`
at `FF0Dh`, minor `0` at `FF0Eh`, and transfer granularity `16` at `FF0Fh`.
Minor revisions preserve existing calls; incompatible revisions change the major.

### INFO

Call in bank 0 with no input arguments. Require success before READ/WRITE;
the result can be cached for the application’s lifetime.

| Result | Registers |
| --- | --- |
| Available | Carry clear, A=0, HL=`0100h` (version 1.0), DE=`0100h` (heap start), BC=`FE00h` (65,024 bytes) |
| Unavailable | Carry set, A=1, HL=0; BC/DE unspecified |

### READ and WRITE

Pass **HL=base-buffer start, DE=bank-1 offset, BC=byte count**. HL is the
READ destination or WRITE source. Calls start and finish in bank 0.

- Positive counts must be multiples of **16 bytes**; addresses need not align.
  The base buffer must start at or above `0100h`. Both ranges must end at or
  below `FF00h` (exclusive end), without 16-bit overflow.
- Zero count succeeds without switching banks or accessing buffers.
- Success: **A=0, carry clear**. Invalid length/bounds: **A=2, carry set**,
  without switching banks or writing user buffers.
- IX, IY, SP, and maskable interrupt enable are preserved. Both register sets’
  AF/BC/DE/HL are scratch; other flags are unspecified.

Bank-1 offsets `0000h`–`00FFh` hold allocator metadata and are accessible to
raw calls. `FF00h`–`FFFFh` is reserved in both banks. Buffers must not overlap
active code or stack.

Calls are non-reentrant and must not run in interrupt handlers. Do not switch
banks during a call. Copying masks/restores maskable interrupts; NMI is
unsupported. This board holds INT#/NMI# inactive.

### C interface

`examples/BANKED/BIOSBANK.H` and `BIOSBANK.C` adapt dcc’s stack convention to
this ABI. `bmstat()` returns the version or zero; `bmread()` and `bmwrit()`
return 1 on success, 0 on failure.

`BANKMEM.C` provides `bread`/`bwrite` for arbitrary byte counts, batching full
bursts and buffering partial transfers. Allocation (`bmalloc`/`bfree`) and
ownership remain in the C library; the dcc runtime is unchanged. For allocator
metadata and porting details, see the
[companion ABI reference](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/BANKED/BIOS-ABI.md#c-interface).
