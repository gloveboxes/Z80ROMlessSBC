# Z80 ROMless SBC - WIP Engineering & Build Specification

**Repository:** [github.com/gloveboxes/Z80ROMlessSBC](https://github.com/gloveboxes/Z80ROMlessSBC)

## Overview

This document describes a theoretical Z80 single-board computer design
that has not yet been built, wired, or validated in hardware. Treat it
as an engineering proposal and bring-up plan, not as a proven reference
design. Every electrical assumption, timing margin, and firmware
interaction still needs bench validation with the staged tests in
the [implementation plan](implementation/index.md) before the design should be
considered reliable.

The proposed system is a ROMless Z80 computer supervised by a Raspberry
Pi Pico 2 W. The Pico supplies the Z80 clock, holds and releases reset,
loads a boot image directly into SRAM by taking the Z80 bus, and then
lets the Z80 execute from RAM. A 64 KB SRAM provides the Z80 memory
space; level shifters and bus transceivers isolate the Pico's 3.3 V GPIO
from the 5 V Z80/SRAM bus; an MCP23S17 expands the Pico's address-drive
capability during DMA and trapped I/O cycles.

The key design idea is that the Pico acts as both supervisor and virtual
peripheral controller. It can stop the fully static Z80 clock on an I/O
cycle while the GAL asserts WAIT#, inspect the requested port, exchange
one data byte, release WAIT# only after the selected data path is ready, and then
resume execution. Terminal I/O is intended to be one of those virtual
peripherals: Z80 `IN` and `OUT` instructions feed nonblocking queues on
the Pico, while a WebSocket terminal server runs on the Pico's other
core so Wi-Fi and browser traffic do not disturb Z80 timing.

Z80 software lives in a reserved region of the Pico's own onboard
flash rather than on removable media or compiled into the running
firmware image. The Pico reads boot binaries, monitor images, and CP/M
disk images directly from that memory-mapped flash region, then uses
the already-validated DMA path to populate SRAM before the Z80 is
released. Once the Z80 is running, the same I/O trap mechanism can
expose sector-oriented virtual disk ports backed by that same flash
partition described in
[Section 6.3](system/operation.md#63-onboard-flash-cpm-disk-storage).

### CP/M Boot and Disk Flow

The complete software and storage path is:

1. The build assembles the Z80-optimized CCP/BDOS and board BIOS, then places
   them in a reset-ready 64 KiB Z80 memory image.
2. The builder wraps that image as `z80boot.pkg` with a manifest and CRC, and
   inserts the same optimized resident system into Drive A's reserved system
   tracks. Drives B-D are built as separate native 320 KiB CP/M images.
3. The boot package and four disk images are provisioned into fixed regions of
   the Pico 2 W's 4 MiB onboard flash. They may be combined with the Pico
   firmware as `z80romless-flash.bin`.
4. On cold boot, the Pico validates the package and journal, holds the Z80 in
   reset, takes ownership of the bus, copies the 64 KiB image from flash into
   SRAM, verifies the copy, installs the CP/M page-zero vectors, and releases
   reset.
5. The Z80 starts from SRAM, enters the BIOS, and reaches the CP/M `A>` prompt.
   The BIOS converts CP/M's disk requests into 128-byte virtual-I/O transfers;
   the Pico services those transfers from the flash disk regions.
6. The Pico caches ordinary writes and journals directory or other important
   writes. On a CP/M warm boot, the BIOS reloads the resident CCP/BDOS records
   from flash-backed Drive A into SRAM before returning to the prompt.

This gives the project one simple separation: **flash provides persistent
storage, SRAM provides the Z80's executing memory, and the BIOS plus Pico
connect the two**. The [CP/M boot-image README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md), [disk-media
README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md), [Section 6.3](system/operation.md#63-onboard-flash-cpm-disk-storage),
and [Appendix D](cpm-dcc/index.md) provide the
construction details, addresses, protocols, and validation evidence.

The build plan is deliberately phased. Early phases prove power,
translation, SPI expansion, bus isolation, SRAM DMA, and clock control
before the Z80 is installed. Later phases prove virtual-ROM boot,
synchronous I/O trapping, WebSocket terminal service, and finally the
maximum qualified clock rate. Passing an earlier phase is a prerequisite
for trusting the assumptions used by the next one.

## Project Documentation

The repository also includes focused documentation for specific implementation
areas:

- [Stage 0: Power and passive wiring](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage00_power/README.md) - safe
  initial power, continuity, resistance, rail-voltage, and diode-OR checks.
- [Native CP/M boot image](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md) - CP/M memory layout, Z80
  optimization, boot-package construction, BIOS behavior, and host testing.
- [DCC debug I/O adapter](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/dcc_io_adapter/README.md) - complete
  project-hosted Altair-compatible port drivers, SBC native disks, interrupt
  services, private environment configuration, and ANSI terminal input.
- [CP/M disk media](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md) - source disk formats, conversion,
  native geometry, generated images, and flash provisioning.
