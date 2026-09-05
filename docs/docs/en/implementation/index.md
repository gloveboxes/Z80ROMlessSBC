# 8. Progressive Build and Bring-Up Plan

## Start here

*Bring-up* means proving a small part of the circuit works before adding the
next part. Treat each phase like an integration test with a hardware fixture:
the installed chips, wiring, firmware, and supply settings are all part of
the test. A successful firmware upload alone does not prove the wiring.

Read the [construction guide](../hardware/construction.md) before handling
the boards, then complete Phase 0 onward in order. Use the hardware pages as
references, not as an instruction to wire and populate the whole computer at
once. Prepare the [firmware build tools](../system/firmware-build.md) before
Phase 1; use the maintained stage programs rather than assembling the example
C fragments into your own test firmware.

For each phase:

1. **Power off:** disconnect external power and USB before moving a wire,
   inserting a chip, or using the meter's continuity mode.
2. **Wire and inspect:** follow that phase's wiring steps, verify chip
   orientation, then check each new connection end to end with a multimeter.
3. **Power and test:** use the specified current limit and firmware; measure
   the named signals with the appropriate instrument.
4. **Record and decide:** save the firmware version, rail voltages, idle
   current, test output, and any required captures. Advance only when every
   required test passes. If a result differs, stop at this phase and check
   the most recent wiring before changing firmware or adding more hardware.

!!! warning "A failing power check is a stop condition"
    A collapsed rail, unexpected current, or warm chip is not a software
    problem to debug while powered. Disconnect power first. Do not increase
    the current limit just to make the board start.

## Phase sequence

| Phase | Proves |
| --- | --- |
| [0 - Power distribution](phase-0-power.md) | Sockets, rails, protection, and passive defaults |
| [1 - Pico supervisor](phase-1-supervisor.md) | Safe startup levels and GPIO routing |
| [2 - GAL, buffer, and clock](phase-2-buffer-clock.md) | Arbitration equations, interlocks, translation, and clock quality |
| [3 - Address generator](phase-3-address-generator.md) | MCP23S17 reset, SPI, and port operation |
| [4 - Address bus](phase-4-address-bus.md) | Direct 16-bit drive, input sampling, and isolation |
| [5 - Data bus](phase-5-data-bus.md) | Bidirectional translation and contention prevention |
| [6 - SRAM and DMA](phase-6-sram.md) | Full-memory access and RAM diagnostics |
| [7 - Z80 CPU](phase-7-z80.md) | Reset fetch, stepping, execution, and bus grants |
| [8 - Virtual I/O](phase-8-virtual-io.md) | Synchronous IN/OUT trapping and USB diagnostics |
| [9 - Flash storage](phase-9-flash-storage.md) | Manifest boot, journal recovery, and CP/M disks |
| [10 - WebSocket terminal](phase-10-websocket.md) | Final multicore network terminal and disk integration |
| [Frequency qualification](frequency-qualification.md) | Measured operating limit and complete timing evidence |

Build and test one functional block at a time. Do not install the next
chip until the current phase passes. Use sockets for all DIP devices,
place a 100 nF ceramic capacitor directly across each IC's supply pins,
and fit at least one 22 uF bulk capacitor per breadboard. Use a
current-limited 5 V supply, multimeter, oscilloscope, and a
DSLogic Plus logic analyzer using the
[documented capture groups](../hardware/logic-analyzer.md). Start each first
power-up at a 100 mA current limit and
remove power immediately if a rail falls by more than 5%, current rises
unexpectedly, or a device becomes warm.

Each phase owns the signal wiring first required by that phase. Install and
continuity-check those jumpers with the affected active devices removed, then
insert only the devices named by the phase. The wiring diagrams are included
from the consolidated hardware pages, which remain the authoritative
subsystem references; the implementation plan presents the same source in
construction order rather than maintaining duplicate diagrams.

Fit the [specified pull-ups and pull-downs](phase-0-power.md#passive-component-installation)
so every signal has its defined state before firmware starts. Use temporary
1 kOhm series resistors when first connecting two potentially driven nodes.
Record idle current after every phase. Unless stated otherwise, keep all chips
from later phases out of their sockets.

The following phase pages contain Pico SDK fragments showing the
safety-critical behavior and finish with the required two-core `main()`
integration order. The maintained applications provide the actual command
parsers and networking. Test plans include both existing diagnostic commands
and manual measurements or test-only fault setups; they are not all automated.
A command printing `PASS` does not replace the listed electrical checks.
Record any test not performed as **not verified**, rather than inferring a pass.

The fragments simplify or rename identifiers for exposition; the adjacent
**Maintained source** links identify the authoritative, compilable
implementation.

`PIN_BUSACK_N` is GP0, driven from Z80 BUSACK# pin 23 through the
[SN74LVC244 input buffer](../hardware/bus-isolation.md#53-sn74lvc244an-5-v-to-33-v-input-buffer).
IORQ#, RD#, and MCP SO use the same buffer, so no 5 V output reaches the Pico
directly. `PIN_SRAM_CE_N` is fixed at GP5 (ATF22V10 pin 7 in the
[SRAM arbitration design](../hardware/pin-mapping.md#12-sram-control-source-arbitration-atf22v10bc)).
GP23 on the Pico 2 W is dedicated to the CYW43439 wireless module's control
interface (shared with GP24/GP25/GP29) and must never be repurposed.
`PIN_DATA_0`-`PIN_DATA_7` occupy GP10-GP17. There is no
`PIN_SRAM_SOURCE_SELECT` GPIO: the ATF22V10 combines RESET# and BUSACK# inside
each programmed SRAM-control equation in the
[arbitration design](../hardware/pin-mapping.md#12-sram-control-source-arbitration-atf22v10bc),
so ownership still switches automatically with no extra Pico pin. `PIN_WR_N`
is GP28, buffered through the same
[SN74LVC244](../hardware/bus-isolation.md#53-sn74lvc244an-5-v-to-33-v-input-buffer);
`io_trap_handler()` reads both `PIN_RD_N` and `PIN_WR_N` to resolve cycle
intent.
