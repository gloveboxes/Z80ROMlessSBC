# D.4 Custom BIOS Responsibilities

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

## Banked-memory BIOS extension (ABI 1.0)

The mirrored `FF00h` page is present in both banks. Check `BMX1` at `FF09h`,
major 1 at `FF0Dh`, minor 0 at `FF0Eh`, and INFO success before calling it.
Firmware and the CP/M system image must be updated together.

| Address | Entry | Contract |
| --- | --- | --- |
| `FF00h` | READ | HL=bank-0 buffer, DE=bank-1 address, BC=length |
| `FF03h` | WRITE | HL=bank-0 buffer, DE=bank-1 address, BC=length |
| `FF06h` | INFO | HL=`0100h`, DE=`C800h`, BC=`1600h` on success |
| `FF10h` | Service | E selects the operation below |

READ and WRITE use multiples of 16 bytes; both ranges must end at or below
the exclusive limit `FF00h` without wrapping.
Raw transfers into resident memory are for system code; application handles
are allocated only in `C800h–DDFFh`. The bank-0 C wrappers `bread` and `bwrite`
provide byte-granular transfers using discovered BIOS byte-copy helpers
(`F800h` READ, `F803h` WRITE). These helpers retain the register convention above, accept arbitrary
byte counts, and return A=0/carry clear or A=2/carry set for an invalid range.
Zero count succeeds without dereferencing either address. For positive counts,
the bank-0 buffer must start at or above `0100h`; the raw bank-1 address has
no equivalent lower-bound check and can include page zero. Both ranges must
end at or below `FF00h`, without wrapping. This does not make reserved bank-1
memory available to applications. Full-range validation precedes copying;
partial writes preserve neighbouring bytes.
IX/IY, stack and interrupt state are preserved.

`bread` and `bwrite` call the checked entries `F806h` READ and `F809h` WRITE.
They take A=handle page (a handle's high byte; handles are 256-byte aligned),
DE=bank-1 address, HL=bank-0 buffer and BC=byte count. The bank-0 shim maps
the page to its allocator extent: pool pages `C8h–DDh`, the pinned library at
page `01h` and its stack at `C0h`. With interrupts masked, the shim calls a
mirrored helper at `FB48h` that selects bank 1, reads the live extent length
and allocated marker, and reselects bank 0 without touching the stack. A
free, unused or unknown page returns A=3/carry set. A range starting below
the handle or ending beyond the extent returns A=2/carry set. Otherwise the
call continues as `F800h`/`F803h`. Every call checks the live tables, so a
handle freed or invalidated by RESET is rejected; no bounds are cached.

Counts divisible by 16 dispatch directly to the raw gateway, without setting up
tail storage. The gateway's 237-byte mirrored code retains the low count and
direction in A and saves the advancing source SP in that source bank's private
`FFF0h` word. Only D0 of bank-select OUT data is significant; the other bits may
contain the counter. Group bookkeeping happens once per 256 bytes, while each
16-byte burst still performs exactly two bank-select OUTs. The main carry flag
survives the register payload and avoids a second direction test. No allocation
metadata is cached. `FF06h` delegates to a bank-0-only INFO helper at `FA00h` inside the
BDOS shim. Deploy matching firmware, boot package and drive A system records together.
See [MEMPERF](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/MEMPERF) for the repeated-workload
methodology and emulator cycle measurements, which exclude hardware wait time.

Resident allocator operations have a short dispatch path and save SP directly.
Metadata-table selectors preserve AF with bit operations; SIZE still validates
the current extent length and allocated marker on every call. Private host and
library context snapshots store nine bytes: the DMA word plus the actual search
state at `FB14h`, `FB18h–FB1Ah` and `FB29h–FB2Bh`. The host search proxy uses the
same packed seven-byte search layout. Adjacent shared disk-geometry scratch is
never copied as per-domain state.

### Allocation and direct entry calls

| E | Operation | Arguments and result |
| --- | --- | --- |
| 0 | ALLOC | HL=requested bytes; returns a 256-byte-aligned handle or zero |
| 1 | FREE | HL=handle; returns 1 on success |
| 2 | SIZE | HL=handle; returns rounded size or zero |
| 3 | AVAILABLE | Returns free bytes, initially 5,632 |
| 4 | RESET | Invalidates general-pool handles and restores the free extent |
| 5 | CALL | HL=live code handle, BC=distinct live stack handle, IX=entry offset, IY=context; returns code's HL |
| 6 | BDOS proxy | Resident bank-0 BDOS shim service |
| 7 | CAPABILITIES | Returns bank-1 address of a 48-byte versioned descriptor, or zero |
| 8 | LIB CONTROL | HL=discovered request address; returns status independently of results |
| 9 | LAST STATUS | Returns last resident-service status without clearing it |

### Capability and library control records

After INFO succeeds, use `bank_bios_query()` rather than inspecting allocator
signatures or private tables. The descriptor contains 24 little-endian words:

| Word | Meaning |
| --- | --- |
| 0-3 | Size (48), descriptor revision (1), ABI (`0100h`), feature mask (`003Fh`) |
| 4-7 | Pool start, capacity, allocation unit, raw burst size |
| 8-11 | Byte READ/WRITE entries, library request address and size |
| 12-15 | Export table address, image origin, pinned code and stack handles |
| 16-19 | Call-token address, maximum body bytes, exports and signature characters |
| 20-23 | Call revision (1), checked READ/WRITE entries, one reserved zero word |

Feature bits 0-5 advertise byte transfers, library control, detailed status,
DMA/search contexts, generation/epoch validation and checked handle transfers,
respectively.
`bank_bios_library_control()` copies the request to its discovered address,
dispatches operation 8 and retrieves the response. All fields are 16-bit words:

| Byte offset | Meaning |
| --- | --- |
| 0, 2 | Request size (32), revision (1) |
| 4, 6 | Action (0 LOAD, 1 INVALIDATE, 2 QUERY, 3 CALL), output status |
| 8, 10 | Expected/current module generation, zero-based export index |
| 12, 14 | Live FCB staging/frame handle, per-call token |
| 16, 18, 20 | Ready flag, export count, entry size |
| 22, 24, 26, 28 | Image bytes, export table address, image origin, stack handle |
| 30 | Expected/current pool epoch |

LOAD copies a padded 48-byte FCB from a live pool allocation. QUERY reports
authoritative descriptors without changing ownership. CALL requires a live
512-byte extent, a body length prefix in 1..256, a typed export and matching
module generation/pool epoch. INVALIDATE makes the module uncallable even at
generation exhaustion. Generations and epochs never wrap: further LOAD/RESET
fails with EXHAUSTED rather than reviving stale state.

BIOS status codes are:

| Code | Status | Meaning |
| --- | --- | --- |
| 0 | OK | The BIOS operation succeeded; an invoked function's result is separate. |
| 1 | UNAVAILABLE | The banked-memory capability or matching resident service is unavailable. |
| 2 | ARGUMENT | An argument, range, length, request field or operation is invalid. |
| 3 | HANDLE | A required allocation handle is not live or does not meet the operation's requirements. |
| 4 | MEMORY | No free extent can satisfy the allocation request. |
| 5 | PINNED | An attempt was made to free a pinned library or stack extent. |
| 6 | DISPATCH | A typed library adapter did not return its required success value of 1. |
| 7 | FORMAT | The library header is invalid. |
| 8 | IO | A library load encountered a file open or read failure. |
| 9 | STALE | The module is not ready, or the expected module generation or pool epoch does not match. |
| 10 | EXHAUSTED | A non-wrapping module-generation or pool-epoch counter cannot advance. |

Raw CALL returns arbitrary function HL, including valid zero;
status is separate. `bank_memory_last_error()` and `bank_bios_last_error()`
use the BIOS codes above. `bank_library_last_error()` uses the separate `BL_*`
codes defined in
[BANKLIB.H](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/BANKSDK/BANKLIB.H);
for example, `BL_IO` is 2 and `BL_STALE` is 12.

`bmalloc` and `bfree` run in the bank-1 extent allocator. Free-list and
physical-extent tables occupy the pinned `E600h–EDFFh` region; returned
handles never point at metadata. Allocation searches free extents and splits
one; free coalesces adjacent extents. SIZE and AVAILABLE read tables directly.
`breset` preserves the pinned library, stack, loader and allocator.
Four compact 32-byte metadata tables preserve the entire 5,632-byte pool;
service growth does not reduce the library's DCC heap or stack.

The `0100h` library image and `C000h` stack are pinned handles for CALL.
For an ordinary allocated code block, the caller supplies its entry offset.
The BIOS validates the code and stack handles and offset; it cannot verify
that the offset is an instruction boundary. CALL places IY on the bank-1
stack as one 16-bit DCC C argument. A function using the normal DCC frame
prologue reads it at `IX+4` and returns a 16-bit value in HL.

### Stack switch and return

1. The mirrored gateway saves the bank-0 interrupt state and selects bank 1.
2. Resident code saves the bank-0 SP in bank-1 workspace and uses its private
   service stack. CALL then switches to the exclusive end of the supplied,
   distinct live stack allocation and rejects `BC=0`.
3. The function returns with `RET`. Resident code restores SP, and the
   gateway selects bank 0, restores interrupt state and returns to C.

The bank-1 function never dereferences the bank-0 stack. IX and IY are
restored; HL carries the function result. Calls are sequential and cannot
re-enter the gateway from bank 1. Interrupt handlers and NMI during a bank
switch are unsupported.

DMA and search continuation belong separately to the host and library domains.
Only search fields are restored, not shared disk-geometry scratch variables.
Search Next refreshes the shared directory buffer before resuming; this adds
a directory read. Current drive/user and disk allocation state remain shared.

### Bank-1 CP/M and C library

Bank 1 holds the authoritative BDOS and drive allocation state. The bank-0
BDOS disk/file shim forwards calls and marshals FCBs and DMA records. A bank-1
library may call its own BDOS for console and file I/O. Its DCC `malloc` heap
starts at the linked end of its image and BSS (`__hstart`); its upper limit
is `C800h` minus the configured stack size. The loader clears BSS and obtains
the heap base from the library header.
The fixed linked image begins at `0100h`; the separate general `bmalloc` pool
begins at `C800h`.

The bank-0 shim saves its caller's stack pointer, uses a private stack at
`F600h–F7FFh` while forwarding a call, then restores the caller's stack.
The CCP's own stack is too close to its code for nested bank transfers.

The resident LIB loader validates a `.LIB` header, reads the image into `0100h`,
initializes DCC heap bounds, and exposes export offsets. The bank-0 BANKLIB
runtime copies the length-prefixed call frame with `bank_bios_write()`, then
invokes LIB CONTROL (operation 8, action 3 CALL). The resident service
validates the module generation, pool epoch, frame allocation and typed export
before invoking CALL internally. After successful dispatch, BANKLIB reads
the result frame with `bank_bios_read()` and publishes it to the caller.
Direct CALL is available for lower-level code but does not replace the
typed library-control checks. The [bank-1 library design](bank1-library-spec.md) describes
ownership and limits. [LIBDEMO](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/LIBDEMO)
is the maintained C example; [SPREADS](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/SPREADS)
shows the smaller hand-written-code path.

The example has two parts: the bank-0 host builds `DATATEST.COM`, and the
bank-1 library builds `DATA.LIB`. DCC generates the shared API header and
call wrappers from the library source. The host exercises scalar values,
copied buffers, flat records, file I/O, bank-owned blocks and library lifecycle.
Its [HOSTCHK.C](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/LIBDEMO/host/HOSTCHK.C)
helper handles test setup, memory reports and cleanup checks.

??? example "Bank-0 host: LIBDEMO/host/DATATEST.C"

    ```c
    {% include "../../../../examples/LIBDEMO/host/DATATEST.C" %}
    ```

??? example "Bank-1 library: LIBDEMO/lib/DATA.C"

    ```c
    {% include "../../../../examples/LIBDEMO/lib/DATA.C" %}
    ```

### Boot and deployment

The Pico loads and verifies bank 0, then loads and verifies 8,960 bank-1
bytes, including the loader, allocator, BDOS, BIOS and mirrored gateway.
Bank-1 warm boot switches back to bank 0. Drive A's system tracks contain
the bank-0 BDOS shim, so warm boot restores the same gateway. Deploy matching
firmware, CP/M system image, and rebuilt `.COM` and `.LIB` files together.
Libraries carry format 1, ABI 1.0 and call revision 1 in their header.
