# Z80 ROMless SBC - WIP Engineering and Build Specification

**Repository:** [github.com/gloveboxes/Z80ROMlessSBC](https://github.com/gloveboxes/Z80ROMlessSBC)

## Overview

This is a theoretical Z80 single-board computer design that has not yet been
built or validated in hardware. Treat it as an engineering proposal and
bring-up plan, not a proven reference design. Its electrical assumptions,
timing margins, and firmware interactions require bench validation through
the staged [implementation plan](implementation/index.md).

The design pairs a ROMless Z80 with a Raspberry Pi Pico 2 W supervisor. The
Pico supplies the clock, controls reset, takes ownership of the bus, and loads
a boot image into SRAM before allowing the Z80 to run. A 64 KiB SRAM chip
provides the Z80 memory space. Buffers and bus transceivers isolate the Pico's 3.3 V
GPIO from the 5 V bus, while an MCP23S17 drives the 16-bit address bus during
DMA and trapped I/O cycles.

The Pico also acts as a virtual peripheral controller. During a trapped I/O
cycle, it can stop the fully static Z80 clock while the GAL asserts WAIT#,
inspect the requested port, exchange one byte, and then resume execution.
Z80 `IN` and `OUT` instructions feed nonblocking terminal queues; a WebSocket
server runs on the Pico's other core so network traffic does not affect Z80
timing.

Z80 boot software and CP/M disks occupy reserved regions of the Pico's onboard
flash rather than removable media. The Pico reads this memory-mapped storage
directly and uses DMA to populate SRAM before releasing the Z80. Once the Z80
is running, the same I/O trap serves sector-oriented disk ports backed by the
[onboard flash partition](system/operation.md#63-onboard-flash-cpm-disk-storage).

### CP/M Boot and Disk Flow

The complete software and storage path is:

1. The `z80_cpm_images` target assembles the optimized CCP/BDOS and board BIOS
   into `z80boot.img`, then adds a header and CRCs to create `z80boot.pkg`.
2. It also writes the CCP/BDOS and BIOS to Drive A's reserved system tracks and
   emits Drives A-D as separate 320 KiB CP/M images.
3. It combines the Stage 10 Pico firmware, `z80boot.pkg`, and all four disks
   into `z80romless-flash.bin`, the complete 4 MiB initial-provisioning image.
   The separate artifacts remain available for selective updates.
4. On cold boot, the Pico recovers the journal, validates `z80boot.pkg`, copies
   its 64 KiB payload into SRAM, verifies the copy, installs the page-zero
   vectors, and releases the Z80 from reset. The Z80 enters the BIOS and reaches
   the CP/M `A>` prompt.
5. During operation, the BIOS converts disk requests into 128-byte transfers
   that the Pico services from flash. The Pico caches writes and journals each
   flash commit. A warm boot does not use `z80boot.pkg`; the BIOS reloads
   CCP/BDOS from Drive A before returning to the prompt.

See the [CP/M boot-image README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md),
[disk-media README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md),
[flash-storage design](system/operation.md#63-onboard-flash-cpm-disk-storage),
and [CP/M appendix](cpm-dcc/index.md) for construction details, addresses,
protocols, and validation evidence.

The build plan proves each subsystem before relying on it in the next phase.
It progresses from power, bus isolation, and SRAM DMA through Z80 execution,
virtual I/O, flash storage, the WebSocket terminal, and clock qualification.

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
