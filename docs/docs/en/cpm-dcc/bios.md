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

## Implemented banked-memory BIOS extension (ABI 2.4)

The reserved top page supplements CP/M's standard BIOS jump table. Check
`BMX2` at `FF09h`, major 2 at `FF0Dh`, minor 4 at `FF0Eh`, and granularity
16 at `FF0Fh`, then require INFO success before using the other entries.

| Address | Entry | Purpose |
| --- | --- | --- |
| `FF00h` | READ | Copy bank 1 to a base-bank buffer |
| `FF03h` | WRITE | Copy a base-bank buffer to bank 1 |
| `FF06h` | INFO | HL=0204h version, DE=0800h heap start, BC=F700h capacity |
| `FF10h` | HEAP | E=0 allocate, 1 free, 2 size, 3 available, 4 reset, 5 call bank-1 code, 6 call at an offset with a DCC-style argument |

### Transfers and heap calls

READ/WRITE take HL=base buffer, DE=bank offset, BC=length. Positive lengths
are multiples of 16; addresses need not align. The base buffer starts at or
above 0100h, both ranges end at or below FF00h without wrapping, and a zero
length succeeds without touching memory. Success returns A=0/carry clear;
invalid arguments return A=2/carry set. INFO failure returns A=1/carry set,
HL=0.

HEAP takes HL=bytes or handle and returns HL=handle, size, free bytes, or
1/0 for free/reset. Its A=0/carry clear, including allocation failure.

### Calling bank-1 code

#### Entry at the start: E=5

HL must be a live allocation containing executable Z80 code. BC=0 uses the
private bank-1 stack, leaving 58 bytes for the payload. A distinct live stack
handle in BC uses that allocation instead. `bmexec(code, 0)` and
`bmexec(code, stack)` expose these two choices.

With an allocated stack, the BIOS validates both handles, sets SP to the end
of the stack allocation, places a return address there, and jumps to the
code. The caller keeps both allocations live until the payload returns.
The payload receives the caller's IY, but the BIOS does not validate it as a
context handle.

#### Entry at an offset: E=6

HL is a live code handle, BC is a distinct live stack handle, IX is a 16-bit
byte offset, and IY is a bank-1 context pointer. The BIOS enters at `HL+IX`.
Offset zero enters the start; every offset must be smaller than the rounded
code capacity. E=6 always requires a separate stack allocation.

The BIOS places IY on that stack as one 16-bit C argument. After its usual
frame prologue, a DCC function reads the argument at `IX+4` and returns its
16-bit result in HL. The caller must provide a valid context pointer and an
offset at an instruction boundary.

[PRIMEBK1](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/PRIMEBK1)
demonstrates two entry offsets with independent contexts and stacks.

#### Stack switch and return

Both CALL operations follow the same handoff:

1. The mirrored gateway saves interrupt state on the bank-0 stack, disables
   interrupts, and selects bank 1.
2. The bank-1 allocator records the bank-0 SP value, switches to its private
   stack, then selects the payload stack. E=5 with BC=0 keeps the private stack.
3. The payload finishes with `RET`. The allocator restores the saved SP, and
   the mirrored gateway switches back to bank 0 before returning to the caller.

The gateway returns the payload's HL value and restores IX/IY. Bank-1 code
never accesses the bank-0 stack, calls bank-0 code, re-enters the allocator,
or enables interrupts.

### SPREADS: two-entry-point example

The small [SPREADS source](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/SPREADS/DEMO.C)
shows operation 6 without relocation or a dispatch table. Its first bank-1
function is at offset 0; the second is at `bmarm - bcode`. Both receive the
same bank-1 `SpreadParams` pointer through the DCC-style stack argument. The
bank-0 wrappers expose natural calls, `vegemite(&params)` and
`marmite(&params)`: each copies the C parameter block to bank 1, invokes its
entry point, and copies the result back. Requests for 8, 7 and 3 characters produce `VEGEMITE`,
`MARMITE` and `VEG`. The
[example build instructions](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/SPREADS/README.md)
use the shared ABI 2.4 wrappers from PRIMEBK1. The docs link to the maintained
source and include that same file below, so the displayed code is read from
the current checkout on every documentation build.

??? example "SPREADS/DEMO.C"

    ```c
    {% include "../../../../examples/SPREADS/DEMO.C" %}
    ```

### Compatibility and limits

CALL clients should also read bank-1 offset `0008h`: revision 1 supports the
private-stack call, revision 3 accepts an optional stack handle, and revision
4 adds E=6 direct-offset C calls. The BIOS
page comes from the CP/M boot package, while the bank-1 allocator comes from
Pico firmware. Updating only CP/M can leave the old allocator installed.

The heap uses 256-byte allocation units and address-ordered free extents.
Lengths and physical/free-list links are stored outside payloads. Allocation
walks only free extents and splits; free merges neighboring extents. SIZE and
AVAILABLE read a table/counter directly. Heap operations run in bank 1 on
its reserved private stack; E=5 with BC nonzero switches to the supplied stack
for the payload. IX/IY/SP and maskable interrupt enable are preserved; calls return
to bank 0. Other registers are scratch.

Bank 1 reserves 0000h–07FFh for metadata, code and stack, and FF00h–FFFFh for
the mirrored gateway. Application data occupies 0800h–FEFFh: **63,232 bytes**.
BANKMEM checks `BKM3BEX1` at offsets 0–7 before heap calls. No CP/M, BDOS or
base C stack access runs with bank 1 selected. Calls are non-reentrant;
interrupt-handler use and NMI during banking are unsupported.

### How `bmalloc` works

`bmalloc` is a small bank-0 C wrapper around the **Z80 extent allocator running
in bank 1**. The Pico loads and verifies the allocator at boot; it does not
perform allocation on behalf of the Z80. The ordinary dcc runtime `malloc`
continues to manage bank-0 memory separately.

An extent is a contiguous run of 256-byte blocks. The allocator keeps an
address-ordered list containing only free extents, together with a length for
each physical extent. It no longer copies a 64-byte allocation map into bank
0 or examines every block occupied by a large allocation.

For example, `bmalloc(1000)` requests four blocks, giving a rounded capacity
of 1024 bytes. On an empty heap its handle is `0800h`. The free extent starts
at `0C00h` afterward and contains the remaining 243 blocks.

The call proceeds as follows:

1. The C wrapper rejects a zero or oversized request and initializes the
   library if necessary. `binit` checks the BIOS descriptor, INFO result and
   bank-1 `BKM3BEX1` signature before permitting heap calls.
2. `BIOSBANK.C` passes HL=requested bytes and E=0 to HEAP at `FF10h`.
3. The mirrored gateway saves interrupt state on the bank-0 stack, disables
   maskable interrupts, selects bank 1 and jumps to its allocator. Bank-1
   code saves the caller's stack pointer and selects its private stack.
4. The allocator rounds the request up to 256 bytes and walks free extents
   in address order. It takes the first sufficiently large extent. An exact
   fit is removed from the free list; a larger extent is split, leaving the
   remainder free. Extent links and the cached free-byte count are updated.
5. The allocator restores the caller's stack pointer and jumps back to the
   mirrored gateway. The gateway selects bank 0, restores interrupt state
   and returns the handle in HL. IX, IY and SP are preserved.

The handle is an **offset in bank 1**, not a directly dereferenceable C
pointer. Use `bread` and `bwrite` with a bank-0 buffer to access the payload.
Requests fail with handle zero when no contiguous extent fits, even if the
total number of free bytes is sufficient. Newly allocated contents are not
cleared.

```c
#include "BANKMEM.H"

/* Run inside the application's main/function body. */
if (binit()) {
    bhandle data;
    unsigned char buffer[16];

    data = bmalloc(1000);
    if (data) {
        /* bsize(data) is 1024; the handle is never a C pointer. */
        buffer[0] = 'Z';
        if (bwrite(data, 0, buffer, 1) && bread(data, 0, buffer, 1)) {
            /* Process the bytes in ordinary bank-0 C code. */
        }
        bfree(data);
    }
}
```

### Freeing, size queries and reset

`bfree` also executes in bank 1. It validates that the aligned handle names a
live allocation, reads its stored length, and checks its physical neighbors.
Free neighbors are unlinked and merged with the released extent. The final
extent is inserted into the address-ordered free list, preventing adjacent
free extents from accumulating. `bfree(0)` succeeds; unaligned, interior,
reserved and already-freed handles are rejected. A freed address may later
be reused by another allocation.

Finding neighboring extents and unlinking them uses direct table lookups.
Inserting the merged extent can walk the free list, so free is not
worst-case constant time. Allocation cost depends on how many free extents
must be searched, rather than how many 256-byte blocks an allocated prefix
occupies.

| C call | Bank-1 operation | Result |
| --- | --- | --- |
| `bmalloc(bytes)` | E=0, ALLOC | Aligned handle or 0 |
| `bfree(handle)` | E=1, FREE | 1 on success, 0 for an invalid handle |
| `bsize(handle)` | E=2, SIZE | Rounded capacity from the extent table, or 0 |
| `bavail()` | E=3, AVAILABLE | Cached total free bytes |
| `breset()` | E=4, RESET | Discards all allocations and restores one free extent |
| `bmexec(code, stack)` | E=5, CALL | Calls code on the private stack when stack=0, otherwise on a live stack allocation; returns HL |
| `bmcall(code, stack, offset, context)` | E=6, CALL AT | Enters code at a validated byte offset with context as one stack argument; returns HL |

SIZE and AVAILABLE do not scan the heap. Every `bread`/`bwrite` validates its
handle and range through SIZE before transferring data. This removes the
previous map-transfer overhead from each payload API call as well as speeding
up allocation. RESET preserves the allocator code and its active stack; use
it only when every existing handle may be invalidated.

### Bank-1 storage and startup

Metadata is separate from application payloads: there are no allocation
headers or footers inside the returned capacity. Four 256-byte tables are
indexed by block number:

| Bank-1 address range | Contents |
| --- | --- |
| `0000h–00FFh` | Layout/install signatures, free-list head, saved SP, free-byte counter and scratch |
| `0100h–01FFh` | Extent lengths in blocks; zero identifies an interior slot |
| `0200h–02FFh` | Previous physical extent start |
| `0300h–03FFh` | Next free extent; FF marks an allocated extent |
| `0400h–04FFh` | Previous free extent |
| `0500h–07BFh` | Reserved allocator code area |
| `07C0h–07FFh` | Private assembly stack |
| `0800h–FEFFh` | Application heap: 247 blocks, **63,232 bytes** |
| `FF00h–FFFFh` | Mirrored BIOS transfer/gateway page |

Only extent starts have a nonzero length; unused link fields at interior
slots are not meaningful. The next physical extent starts at the current
start block plus its length. The predecessor table permits direct checks of
the preceding extent when coalescing.

The reserved lower region grew from one page to eight pages. This costs
1,792 bytes of payload capacity compared with the earlier 65,024-byte map
heap, in exchange for extent metadata, more code space and a private stack.
See the [complete memory map](memory-map.md) for the bank-0 CP/M layout.

At boot the supervisor first loads and verifies bank 0, then loads and
independently verifies 2304 bank-1 bytes: the 2048-byte reserved runtime region
and the 256-byte mirrored resident page. The A16 isolation probe preserves
and checks bank 0's reset-vector byte. A successful initialization creates
one free extent at block 8 with length 247; bank-1 load/verify progress is
printed before CP/M starts. Probe or verification failure leaves bank 1
disabled. CP/M warm boot preserves the heap; reloading the SRAM image
initializes it again.

### Measured behavior

In the reported board run, ALLOCPRF measured 512 allocation/free pairs per
case. On an empty heap, 256-byte, 4 KiB and 32 KiB requests each took **95 ms**
for the entire batch (about 186 microseconds per pair). Holding the first
16 KiB occupied changed this to 96 ms. With 16 small holes separated by live
allocations, the 4 KiB and 32 KiB cases each took 171 ms because their search
had to skip undersized free extents.

These are pair timings including loop and result-check overhead, not isolated
`bmalloc` latency. These measurements were collected with the earlier comparison benchmark.
The current ALLOCPRF contains only the extent allocator benchmark; the C/map
reference implementation has been removed. MEMPERF excludes allocation/free from its transfer timers.
In the same reported run its 128-call, 256-byte transfer cases improved from
98/99 ms to 67/68 ms for write/read, while bulk transfer time stayed at about
44/45 ms. The direct SIZE lookup reduces overhead on each small transfer.

Install matching firmware, boot package, drive A system records and rebuilt
COM files together. The dcc runtime and CP/M DMA buffer require no changes.
For complete register contracts and metadata layout, see the
[companion ABI reference](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/BANKED/BIOS-ABI.md).
