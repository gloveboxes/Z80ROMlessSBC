# 2. SRAM Loading and Bank Switching

The Z80 alone drives SRAM addresses and write strobes. The Pico loads and
verifies memory by supplying instructions under a slow clock, then releases
the Z80 to execute from SRAM. BUSREQ/BUSACK pauses the CPU for flash work;
it does not permit Pico memory mastering.

For wiring, use the implementation checklists:
[Phase 3 control/port monitor](../implementation/phase-3-address-generator.md#wiring-control-and-port-monitor),
[Phase 4 address trunk and bank select](../implementation/phase-4-address-bus.md),
and [Phase 6 SRAM loading](../implementation/phase-6-sram.md).

## Banked 128 KiB SRAM

The AS6C1008 provides two 64 KiB banks. GP8 controls A16 through U4 AHCT244;
port `0x07` selects the bank. Switching affects all addresses, including code,
stack, and vectors, so applications must use the
[BIOS transfer API](../cpm-dcc/bios.md#banked-memory-bios-extension-abi-10).

### Physical bank-select path

The [Phase 4 checklist](../implementation/phase-4-address-bus.md#sram-bank-select)
owns the GP8–U4–A16 wiring and R24 pull-down. GP8/J2 pin 1 is reserved for
bank selection.

### PIO acceleration

Phases 9 and 10 complete every Z80 I/O cycle in PIO with a 120 MHz
supervisor clock. A PIO0 state machine (4 words, beside the matrix display)
generates the 20 MHz Z80 clock. PIO1 is claimed exclusively for the I/O engine
(29 words) and holds that clock through PIO0 IRQ 1 with the RP2350's
`IRQ PREV`. PIO2 is left to the CYW43 SPI driver, which takes the highest free
block. PIO1 owns GP6-GP17 while CP/M runs: `SET` drives UP_OE#/DOWN_OE#, `OUT`
drives A16, IO_RELEASE and D0-D7 (with their pin directions), side-set drives
IO_RELEASE and `JMP PIN` tests RD#. The board's 10 kOhm IORQ#/RD#/WR#
pull-ups keep a granted bus inactive, so the engine needs no BUSACK# recheck
after IORQ#.

Every cycle holds CLK LOW at its next LOW phase, then:

- **OUT:** PIO enables U10, samples after at least eight system ticks,
  releases WAIT and lets CLK free-run; U10 only drives the Pico side. `OUT 07h`
  drives D0 onto A16 while SRAM is deselected. Other ports autopush a 32-bit
  snapshot (data bits 0-7, A0/A1/A2/A4 bits 8-11, RD# bit 17, WR# bit 18) into
  the 4-entry RX FIFO; when it is full, PIO stalls before WAIT release.
- **IN:** PIO pushes a request snapshot and waits on its TX FIFO. Core 0
  replies with the OUT pin word (A16 unchanged, IO_RELEASE LOW, D0-D7) and the
  driving and idle pin directions. PIO drives D0-D7, enables U9 and releases
  WAIT with CLK still LOW for at least eight ticks, then runs exactly two
  clocks: TW samples WAIT inactive and T3's falling edge samples the data. It
  clears the hold, sets it again eight ticks later, and the clock SM sees it
  at T3's LOW-phase check, never TW's, whatever the IRQ PREV latency. After
  IORQ# rises, PIO isolates U9, rearms WAIT, returns D0-D7 to inputs and only
  then resumes CLK. The next opcode fetch therefore never sees the Pico
  driving the bus; there is no race against the 50 ns T3-to-T1 window.

Core 0 services both FIFOs from the PIO1 RX-not-empty IRQ in bus order, so an
IN always observes every earlier OUT; it also drains them before granting the
bus or deciding a stop. Snapshots with neither or both strobes fail closed.
The fixed HCT32 WAIT circuit remains required.

If either PIO resource cannot be allocated, CP/M start fails closed. Without
verified bank hardware, PIO1 keeps GP8/A16 on SIO, so other I/O continues but
banking reports unavailable. The backend requires 20 MHz Z80 / 120 MHz
supervisor clocks. Stuck transactions trigger fail-closed recovery after
500 ms.

### Firmware and port contract

Loading probes bank isolation, initializes/verifies bank-1 metadata and the
mirrored BIOS page, and restores bank 0. Failed banking checks allow the base
64 KiB loader to continue. A new image load resets the banked heap; warm boot
preserves it.

| Operation | Meaning |
| --- | --- |
| `IN A,(07h)` | `B4h`: bank 0 ready; `B5h`: bank 1 ready; `00h`: unavailable |
| `OUT (07h),A` | PIO selects `A & 1`; ignored without active PIO |

Status reads use the firmware trap. Port aliases follow `address & 0x17`;
see the [port reference](../system/io-ports.md). INT#/NMI# remain inactive on
this board; the transfer API preserves maskable interrupt state but does not
support NMI or reentrant calls.

### dcc banked heap example

[examples/BANKED](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/BANKED)
provides `bmalloc`/`bfree` handles and `bread`/`bwrite` through base-bank buffers.
Handles are not C pointers. Capacity is 65,024 bytes in 256-byte allocations;
the metadata block and top BIOS page are reserved.

The BIOS copies 16-byte bursts; the C library buffers partial transfers.
See the [memory map](../cpm-dcc/memory-map.md) and [BIOS ABI](../cpm-dcc/bios.md)
for bounds and register conventions. Deploy matching firmware, boot package,
and Drive A system records. Changes to `src/common/banked_transfer.asm`
require regenerating its supervisor header with `scripts/build-banked-transfer.py`.

## Loader Sequence

1. Isolate both data paths, stop the execution clock, and reset the Z80.
2. Inhibit SRAM reads while supplying `LD HL,nn` and `LD (HL),n` bytes.
   The Z80 performs each memory write.
3. Verify with injected `LD A,(HL)`, enabling SRAM reads and the downward
   buffer only for the read cycle. Compare every byte.
4. On success, reset to PC zero, isolate the Pico data paths, enable normal
   SRAM reads, and start execution. On failure, hold RESET# LOW and stop CLK.

Slow steps include settling delays; boot duration depends on software and
buffer turnaround as well as clock pulses. See maintained
[sram.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/sram.c)
and [cpu.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/cpu.c).
These are loader operations, not runtime DMA.

### Board verification and recovery

The board has passed repeated 20 MHz PIO boots and `BANKED.COM`, including
32 KiB copies, bounds, reuse, exhaustion, and the final heap byte. This
establishes operation on that board; copy throughput and electrical margins
have not been measured.

Use **Build and flash Pico - Phase 10 firmware**, confirm the CP/M prompt,
run `BANKED`, then inspect **Ctrl-] s** for `[pio] active=1 running=1`.
Firmware-only updates preserve the matching CP/M package, disks, and ENV.

Host regressions cover PIO instruction timing, bus grants, console handoff,
and an interrupt during clock startup. Clock frequency is published before
PIO starts so an early BIOS interrupt can resume it correctly. Host models
complement physical CLK/WAIT#/A16/SRAM-control captures.
