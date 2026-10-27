# BANKED — dcc C heap in the extra SRAM bank

This example needs the Z80ROMlessSBC banked firmware and A16 wiring. It is
specific to the SBC; the ESP32 Altair port-7 implementation is not equivalent.
The project copy lives in Z80ROMlessSBC/examples/BANKED; the requested app copy
lives in esp32-altair-8800/Apps/disk-c-apps/BANKED.

Build with the local dcc toolchain:

```sh
export DCC_DIR=/Users/dave/GitHub/dcc
"$DCC_DIR/dccmake"
```

Output: `build/BANKED.COM`. Copy it to a CP/M disk and run `BANKED` after
loading the matching firmware. The app deliberately resets the shared banked
heap, checks allocator fragmentation/reuse and the FEFFh boundary, then
repeatedly writes and verifies all 65,024 usable bytes with 24 patterns. The
patterns include fixed byte values, address-derived values, and walking one
and zero bits. Each pattern writes the entire bank before a separate
read/compare sweep, exposing aliased addresses. Before every verification
read the base buffer is filled with inverse expected values, so a skipped or
partial read cannot pass using stale data. The only base-RAM data buffer is
256 bytes. Startup lists the allocator, boundary and pattern tests.

The normal build starts stopwatch 0 through timer I/O port `0x17`, displays
elapsed and remaining seconds, completed full-bank iterations, and cumulative
successful bank read/write API calls and average data operations per second
every 10 seconds, then stops after 43,200 seconds. The rate counts successful
`bread`/`bwrite` calls since the stopwatch started, rounded down to whole
operations per second; setup calls are excluded. The access counters count
`bread`/`bwrite` data calls, not allocator
metadata traffic. This works with CP/M 2.2 and needs no calendar clock. Press
ESC to stop early. A failed timer read, memory transfer, allocation, or data
comparison prints `FAIL` and exits.

Timer startup first recovers the port parser with IN followed by OUT zero,
so a previous application's incomplete command cannot affect the countdown.

If A16 is not wired, it reports banked SRAM unavailable without switching the
CPU out of its base memory.

```c
#include "BANKMEM.H"

bhandle record;
unsigned char chunk[256];

if (!binit()) { /* firmware or wiring unavailable */ }
record = bmalloc(4096);
if (record) {
    /* Fill chunk in ordinary C, then copy it into the allocation. */
    bwrite(record, 0, chunk, sizeof(chunk));
    bread(record, 0, chunk, sizeof(chunk));
    bfree(record);
}
```

Check each function's result in real applications. `bmalloc(0)` returns zero;
`bfree(0)` succeeds. `bread`, `bwrite`, `bfree`, `binit` and `breset` return 1
for success, 0 for failure. `bsize` reports the rounded allocation capacity,
and `bavail` reports total free bytes (fragmentation can prevent a request
smaller than that total). Transfers may reach any byte within the rounded
capacity and reject overflow. Allocation contents are not cleared. Freed
handles become invalid and may later be reused, as with ordinary malloc.

Allocations use contiguous 256-byte blocks. Block zero of bank 1 stores a
signature and a 64-byte two-bit map; its last page (`FF00h–FFFFh`) contains
the resident transfer service. There are **254 usable blocks: 65,024 bytes**,
from `0100h` through `FEFFh`. `breset()` discards every allocation; normal
`binit()` checks availability and the signature. The heap survives CP/M warm
boots but resets when firmware reloads the base image. One program owns the
heap at a time; it has no process ownership or crash reclamation.

Handles are offsets, not C pointers. The resident Z80 routine copies data in
**16-byte register bursts**, with two I/O traps per full burst. One assembly
call handles an entire run of full bursts, rather than returning to C after
each burst. BC/DE/HL and their alternate set carry 12 bytes; alternate AF and
IY carry four more. IX holds the destination pointer, and the saved stack
pointer lives in the resident parameter area while SP addresses the payload.
The routine restores IX/IY/SP and maskable interrupt enable and returns to
bank 0. NMI during a transfer is unsupported, and the library is not reentrant.

The library leaves CP/M's DMA area untouched. There is no 128-byte DMA save,
routine installation, or restore on each copy. Partial transfers use a 16-byte
stack buffer; partial writes read/modify/write a burst. Allocation operations
also use a temporary 64-byte map. The caller chooses its base buffer size;
there is no base copy of the entire bank or persistent allocation map.
The library caches one capability byte after initialization, avoiding a trapped
status read on every data chunk. The allocation map remains in bank 1.
Allocation-size validation uses a private Z80 assembly scanner that skips
four continuation entries per map byte. Every transfer still reads the current
map; no allocation-size cache or additional persistent buffer is used.
Aligned payloads call the public BIOS directly, which validates the base/bank
ranges and alignment. Partial transfers retain the C tail-buffer path. These
library optimizations need only a rebuilt COM, using the existing firmware.

This uses **BIOS bank extension ABI 1**. Use matching firmware, relocated boot package,
drive A system records and BANKMEM library: CCP is at `E600h`, BDOS entry at
`EE06h`, BIOS at `FC00h`, and the resident page at `FF00h` in both banks.
The SBC's internal port 7 reports `B4h`/`B5h`; earlier private interfaces are rejected.
The dcc runtime and the existing A16 wiring require no changes.

BIOSBANK.H defines public READ (`FF00h`), WRITE (`FF03h`) and INFO (`FF06h`)
calls. BIOSBANK.C adapts dcc's arguments to the BIOS register convention;
BANKMEM.C accesses no hardware ports, machine-code arrays or private parameter
addresses. The BIOS itself includes the transfer assembly; the supervisor's
copy is generated by `scripts/build-banked-transfer.py` from the same source.
BANKCODE.H is now only a compatibility include for BIOSBANK.H.

READ/WRITE use HL=base buffer, DE=bank offset and BC=byte count (multiple of
16), returning carry clear/A=0 on success or carry set/A=2 for invalid bounds
or length. INFO must succeed first. It returns HL=0100h (version 1.0),
DE=0100h (heap start) and BC=FE00h (heap bytes), or carry set/A=1/HL=0 when
unavailable. The descriptor at FF09h is `BMX1`, major 1, minor 0, granularity
16. Calls preserve IX/IY/SP and interrupt enable; other registers are scratch.
The higher-level C allocation API still accepts arbitrary transfer lengths.

The same C library and COM pass the local native-copy BIOS backend model
without touching the SBC's workspace or port 7. The stock ESP firmware still
needs its own compatible BIOS extension and extra memory; copying this C app
does not install that backend. The stock interactive debugger
has flat memory; use the dedicated banked emulator fixture for this app:

```sh
DCC_DIR=/Users/dave/GitHub/dcc /Users/dave/GitHub/dcc/dccmake \
  dcc-define=BANKED_ONE_CYCLE dcc-build-dir=build/emulator
```

Build this finite test version from `examples/BANKED`, then run from the
Z80ROMlessSBC root:

```sh
build/disk-emulator/bin/python src/common/tests/banked_heap_z80_test.py \
  --program examples/BANKED/build/emulator/BANKED.COM
```

Run that command from Z80ROMlessSBC. This executes the compiled COM under the
real CP/M/BIOS image with modeled A16. It checks software behavior; 20 MHz
electrical timing and throughput require the physical board.

See [TESTING.md](TESTING.md) for verification results and the corrected host
test fixtures. The full host-control suite passes all 78 tests.
