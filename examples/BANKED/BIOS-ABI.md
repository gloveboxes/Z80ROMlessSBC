# Banked-memory BIOS extension ABI 1

The BIOS exposes three permanent entry points in its reserved top page.
They are separate from CP/M 2.2's unchanged 17-entry BIOS jump table. The
addresses and calling convention form the portable interface; callers must
not access the implementation's workspace or hardware bank-select port.

| Address | Entry | Purpose |
| --- | --- | --- |
| `FF00h` | READ | Copy from bank 1 into a base-bank buffer |
| `FF03h` | WRITE | Copy from a base-bank buffer into bank 1 |
| `FF06h` | INFO | Discover version and usable heap geometry; check availability |

At `FF09h` the descriptor is four ASCII bytes `BMX1`, followed by ABI major
at `FF0Dh` (`1`), minor at `FF0Eh` (`0`), and transfer granularity at `FF0Fh`
(`16`). Check the signature and supported major before calling INFO, so an
application never executes unknown bytes on a BIOS without this extension.
Minor revisions preserve the existing calls; incompatible changes require a
different major version. Never replace the existing slots with other calls.

## INFO

Call `FF06h` while bank 0 is selected. No input registers are required.

| Result | Registers |
| --- | --- |
| Available | Carry clear, A=0, HL=`0100h` (version 1.0), DE=`0100h` (heap start), BC=`FE00h` (65,024 heap bytes) |
| Unavailable | Carry set, A=1, HL=0; BC/DE unspecified |

INFO must succeed before using READ or WRITE. Its result may be cached for
the lifetime of the running application. Data calls trust that capability
check and avoid another hardware status I/O on every transfer.

## READ and WRITE

Both calls take **HL=base buffer, DE=bank-1 byte offset, BC=byte count**.
For READ, HL is the destination; for WRITE, HL is the source. Pointers are
ordinary starting addresses, not end addresses.

- A positive count must be a multiple of **16 bytes**. Addresses need not be
  aligned. Both address ranges must end at or below `FF00h`, without 16-bit
  overflow, and the base buffer must start at or above `0100h`.
- A zero count succeeds without switching banks or accessing either buffer.
- Success returns **A=0, carry clear**. Invalid length or bounds return
  **A=2, carry set**, without switching banks or writing user buffers.
- IX, IY, SP and maskable interrupt enable are preserved. Main and alternate
  AF/BC/DE/HL are scratch; other flags are unspecified. Calls start and finish
  in bank 0.

Raw bank offsets `0000h–00FFh` are accessible for the allocator's metadata;
they are not part of the advertised application heap. The top page
`FF00h–FFFFh` is reserved in both banks. Applications must own their buffers
and avoid overlapping their active stack or code.

The extension is not reentrant. Do not call it from interrupt handlers or
use raw bank selects while a call is active. NMI during the SBC backend's
register transfers is unsupported. The documented SBC keeps INT#/NMI# pulled
inactive; the routine masks/restores maskable interrupts during copying.

## C interface

`examples/BANKED/BIOSBANK.H` defines the public ABI and `BIOSBANK.C` adapts
dcc's stack arguments to the register convention. `bmstat()` returns the
version word or zero; `bmread()` and `bmwrit()` return 1 for success, 0 for
failure. `BANKMEM.C` calls these functions and knows no private workspace
addresses or bank-select ports.

The higher-level `bread` and `bwrite` accept arbitrary byte counts. They batch
full bursts into one BIOS call and use a small read/modify/write buffer for
partial transfers. `bmalloc`, `bfree`, allocation metadata and ownership remain
in the C library. The dcc runtime requires no changes.

To reuse this allocator on another platform, initialize bank 1 block zero with
`BKM2` at offsets 0–3 and a 64-byte two-bit map at offset `20h`. Each map entry
describes one 256-byte block: 0=free, 1=allocation start, 2=continuation,
3=reserved. Initial map bytes are `03h`, then 62 zero bytes, then `C0h`,
reserving metadata block 0 and resident page 255. Other metadata bytes start
at zero. The BIOS data API does not interpret the map; this is the companion
C allocator's storage format.
