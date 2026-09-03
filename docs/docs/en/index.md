# Z80 ROMless SBC - WIP Engineering and Build Specification

**Repository:** [github.com/gloveboxes/Z80ROMlessSBC](https://github.com/gloveboxes/Z80ROMlessSBC)

## Overview

This is a theoretical Z80 single-board computer design that has not yet been
built or validated in hardware. Treat it as an engineering proposal and
bring-up plan, not a proven reference design. Its electrical assumptions,
timing margins, and firmware interactions require bench validation through
the staged [implementation plan](implementation/index.md).

The design pairs a Z80 without a ROM chip with a Raspberry Pi Pico 2 W. The
Pico supplies the clock, controls reset, takes ownership of the bus, and loads
a boot image into static RAM (SRAM) before allowing the Z80 to run. A 64 KiB
SRAM chip provides the Z80 memory space. Buffers and bus transceivers isolate
the Pico's 3.3 V pins from the 5 V bus, while an MCP23S17 I/O expander drives
the 16-bit address bus during direct memory access (DMA) and paused
input/output cycles.

The Pico also provides virtual peripherals. During a Z80 input/output request,
hardware pauses the processor while the Pico identifies the requested port and
exchanges one byte. Z80 `IN` and `OUT` instructions feed terminal queues; a
WebSocket server runs on the Pico's other core so network traffic does not
affect Z80 timing.

Z80 boot software and CP/M disks occupy reserved regions of the Pico's onboard
flash rather than removable media. The Pico reads the flash directly and uses
DMA to populate SRAM before releasing the Z80. Once the Z80 is running, the
same mechanism provides virtual disk access backed by the
[onboard flash partition](system/operation.md#63-onboard-flash-cpm-disk-storage).

### CP/M Boot and Disk Flow

The complete software and storage path is:

1. The image build combines CP/M's command processor (CCP), core operating
   system (BDOS), and board input/output layer (BIOS) in `z80boot.img`. It adds
   an integrity header and checksums to create `z80boot.pkg`.
2. The build also writes the CCP, BDOS, and BIOS to Drive A's reserved boot
   area and emits Drives A-D as separate 320 KiB CP/M disk images.
3. It combines the final software for the Pico 2 W, `z80boot.pkg`, and all four
   disks in `z80romless-flash.bin`, the complete 4 MiB image used for initial
   flash setup. The separate files allow later updates without replacing the
   rest of flash.
4. On cold boot, the Pico completes any interrupted disk write, validates
   `z80boot.pkg`, copies its 64 KiB payload into SRAM, verifies the copy, and
   releases the Z80 from reset. The BIOS installs CP/M's restart and system-call
   entry points, then displays the `A>` prompt.
5. Drives A-D are persistent read/write disks. During operation, the BIOS
   converts disk requests into 128-byte transfers that the Pico reads from or
   writes to flash. The Pico caches writes, commits them after 250 ms of
   inactivity or when CP/M requests an immediate save, and uses a recovery log
   to protect each flash update. A warm boot does not use `z80boot.pkg`; the
   BIOS reloads the CCP and BDOS from Drive A before returning to the prompt.

See the [CP/M boot-image README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md),
[disk-media README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md),
[flash-storage design](system/operation.md#63-onboard-flash-cpm-disk-storage),
and [CP/M appendix](cpm-dcc/index.md) for construction details, addresses,
protocols, and validation evidence.

The build plan proves each subsystem before relying on it in the next phase.
It progresses from power, bus isolation, and SRAM DMA through Z80 execution,
virtual peripherals, flash storage, the browser terminal, and measurement of
the maximum reliable clock speed.

## Project Documentation

The repository includes these focused implementation guides:

- [Stage 0: Power and passive wiring](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage00_power/README.md) -
   power, continuity, resistance, rail-voltage, and diode-OR checks.
- [Native CP/M boot image](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md) -
   memory layout, Z80 optimization, image construction, BIOS behavior, and host
   tests.
- [DCC debug I/O adapter](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/dcc_io_adapter/README.md) -
   Altair-compatible I/O drivers, native disks, interrupts, configuration, and
   ANSI terminal input.
- [CP/M disk media](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md) -
   source formats, conversion, native geometry, generated images, and flash
   provisioning.
