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
[BIOS transfer API](../cpm-dcc/bios.md#implemented-banked-memory-bios-extension-abi-1).

### Physical bank-select path

The [Phase 4 checklist](../implementation/phase-4-address-bus.md#sram-bank-select)
owns the GP8–U4–A16 wiring and R24 pull-down. GP8/J2 pin 1 is reserved for
bank selection.

### PIO acceleration

Phase 10 enables PIO-controlled SRAM bank switching after the second-bank probe
succeeds. PIO1 uses
two state machines and 31 instruction words with a 120 MHz supervisor clock:
one generates the 20 MHz Z80 clock; the other handles bank-select OUTs without
an ARM interrupt. PIO0 remains available for the matrix display.

During a bank write, PIO holds CLK LOW, samples D0 onto A16 while SRAM is
deselected, releases WAIT, and rearms after IORQ#/WR# release. The fixed HCT32
WAIT circuit remains required. CLK stays LOW for at least eight system ticks
(66.7 ns) after WAIT release. Reads and other ports use firmware handlers.

Runtime banking requires active PIO. If bank hardware or PIO resources are
unavailable, other I/O continues through PWM/software handling, but banking
reports unavailable. Once allocated, the backend requires 20 MHz Z80 /
120 MHz supervisor clocks. Stuck transactions trigger fail-closed recovery
after 500 ms.

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
