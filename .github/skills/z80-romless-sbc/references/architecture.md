# Architecture Invariants

## Current Hardware

The final design has nine active through-hole packages:

- Raspberry Pi Pico 2 W / RP2350 supervisor
- Z84C0020PEC Z80
- AS6C1008-55PCN SRAM, lower 64 KiB selected with A16 LOW
- MCP23S17-E/SP address interface
- ATF22V10B/C arbitration and interlock GAL
- SN74AHCT244N 5 V output buffer
- SN74AHCT245N fixed Pico-to-5 V data path
- SN74LVC245AN fixed 5 V-to-Pico data path
- SN74LVC244AN five-signal input buffer

The older LVC8T245 carrier, SD storage, external PSRAM, three AHCT125 packages, and HCT157/HCT08 arbitration are not the current architecture.

## Voltage and Power

- All 5 V logic shares one regulated 5 V rail. Every installed 5 V device must be powered whenever that rail is energized.
- External 5 V reaches Pico `VSYS` only through a 1N5819, anode to external 5 V and banded cathode to `VSYS`. Never tie external 5 V to `VBUS`.
- Pico 3.3 V powers LVC244/LVC245 and 3.3 V pull resistors. Pico is the only 3.3 V source.
- Pico GP0-GP25 are FT pads only while IOVDD is present. GP26-GP29 are not FT. Buffer all incoming 5 V signals.
- LVC244 mappings are BUSACK# to GP0, IORQ# to GP1, MCP SO to GP20, RD# to GP27, and WR# to GP28. Both OEs are tied LOW.
- RP2350 GP23/24/25/29 are reserved by the Pico 2 W wireless interface; do not repurpose them.
- Pull-ups protect absent socketed drivers, not installed unpowered ICs.

## Arbitration and Buses

- `CPU_OWNS_SRAM = RESET# AND BUSACK#`. LOW RESET# or LOW BUSACK# selects Pico SRAM controls; only both HIGH selects Z80 controls.
- GAL SRAM outputs include consensus terms to prevent static-1 hazards during ownership changes.
- GAL pins 14-16 feed AHCT244 channels 2A2-2A4. The GAL's guaranteed 2.4 V HIGH is insufficient for direct AS6C1008 CMOS inputs.
- AHCT244 also translates CLK, BUSREQ#, MCP CS#, SCK, and SI. Keep AHCT244 on the Core board so its Z80 clock path stays local.
- Raw IORQ# feeds GAL pin 13. GAL pin 20 drives the pulled-up Z80 WAIT# node with `WAIT# = IORQ# OR DATA_ENABLE`; firmware keeps DATA_ENABLE asserted until the I/O controls release.
- DATA_ENABLE LOW disables both data paths. Firmware changes DATA_DIR only while disabled. GAL outputs enforce mutually exclusive AHCT245/LVC245 enables.
- MCP23S17 connects directly to pulled-up A0-A15. GP9 `ADDR_ENABLE` controls MCP RESET# through GAL pin 19 and Q1. Preload OLAT before setting IODIR outputs; assert reset to isolate.
- Address and data buses are short shared trunks with taps, not stars or implied series paths.
- Z80 BUSACK# floats address, data, MREQ#, IORQ#, RD#, and WR#; fitted 5 V pull-ups define monitored controls during the grant.

## Physical Placement

With BB830 row 1 at top and row 63 at bottom:

- Memory: GAL rows 5-16, SRAM 18-33, MCP 35-48.
- Core: supply clearance 1-3, AHCT244 8-17, Z80 19-38.
- Peripheral: Pico 1-20, LVC244 22-31, AHCT245 33-42, LVC245 44-53.

Narrow DIPs cross E/F. Z80 and SRAM require real 0.6-inch sockets. Verify current Section 3.1 for notch and pin-1 orientation. Verify purchased 2N3904 E/B/C order from its manufacturer datasheet.

## Timing

- Qualify at 1 MHz first, then 2-6 MHz in 500 kHz steps. Treat 6.5-8 MHz as experimental even if measured clean.
- The CPU's 20 MHz grade is not a system rating. Worst-case GAL + AHCT244 + SRAM control-to-data delay is about 79.5 ns before breadboard and setup margin.
- I/O trapping uses GAL-generated hardware WAIT# plus static clock stop; WAIT setup/hold and release sequencing remain measured behavior.
- Use the logic analyzer for simultaneous buses and controls; use the four-channel DHO814 for analog levels, edge quality, and correlated timing groups.

## Flash, CP/M, and Multicore Ownership

- Pico 2 W onboard flash is 4 MiB. Firmware must end below offset `0x290000`.
- Layout: journal `0x290000`/64 KiB, boot `0x2A0000`/128 KiB, drives at `0x2C0000`, `0x310000`, `0x360000`, `0x3B0000`, each 320 KiB.
- Never pass the `0x290000` linker limit as a compiler definition for physical `PICO_FLASH_SIZE_BYTES`; the C macro must remain 4 MiB.
- CP/M geometry is exactly 327,680 bytes = 2,560 x 128-byte records, 80 tracks x 32 records. It is not IBM 3740 geometry.
- Cold boot recovers journals, validates CRCs, performs reset-held DMA without waiting for BUSACK#, verifies SRAM, and releases RESET# only on success.
- Core 0 owns Z80 GPIO, clock, buses, MCP SPI0, and trap handling. Core 1 owns networking and runtime flash writes.
- Runtime writes keep trapping armed until BUSACK# LOW and re-arm it before BUSACK# HIGH. Non-`PICO_OK` flash-safe results fail closed through watchdog reboot.
- Wi-Fi polling is bounded and nonblocking. Core 1 services disk writes before network work on every loop.
