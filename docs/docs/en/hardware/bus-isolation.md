# 5. Bus Translation and Isolation

The Z80 is the sole address-bus driver. Two fixed-direction transceivers
separate its 5 V data bus from the Pico. Both must be disabled before changing
Pico data GPIO direction or value. Never enable both at once.

## 5.1 Data Paths

| Device | Supply | DIR pin 1 | A pins 2-9 | B pins 18-11 | OE# pin 19 |
| --- | --- | --- | --- | --- | --- |
| U9 SN74AHCT245N | +5 V at pin 20 | +5 V, A to B | GP10-GP17 | D0-D7 | HCT32 pin 8 |
| U10 SN74LVC245AN | +3.3 V at pin 20 | GND, B to A | GP10-GP17 | D0-D7 | GP7 |

Both devices use common GND at pin 10 and local 100 nF bypass capacitors.
Use the [Phase 5 wiring tables](../implementation/phase-5-data-bus.md#wiring-bidirectional-data-path)
for individual data-bit connections.

| Mode | GP6 upward request | GP7 downward OE# | Upward path | Downward path |
| --- | ---: | ---: | --- | --- |
| Isolated / run | 1 | 1 | Off | Off |
| Injected read / IN reply | 0 | 1 | On only while RD# LOW | Off |
| SRAM verification / OUT | 1 | 0 | Off | On |

U3 gates U9 OE# with raw RD# and GP6; GP7 controls U10 OE# directly.
Both GPIOs have 10 kOhm pull-ups to 3.3 V. Firmware isolates both paths
before changing direction, waits at least 1 us before driving, and preloads
output data or configures inputs before enabling a path. This is software
exclusion: driving GP6 and GP7 LOW together is prohibited.

Software-serviced I/O steps the clock until the strobes release, isolates
both paths, and rearms WAIT before resuming execution. PIO handles SRAM
bank-select writes directly; see [bank switching](address-interface.md#pio-acceleration).
RD# gating alone does not prevent contention if GP6 remains asserted into
a later memory read.

Do not substitute TXS/TXB auto-direction parts, BSS138, or resistor-divider
modules for this push-pull multi-load bus. The AHCT245 lacks data-port
power-off protection: apply external +5 V before USB and remove USB before
+5 V. Disabling its OE does not eliminate its input clamp paths.

## 5.2 CPU-Only Address Ownership

Only the Z80 drives A0-A15; SRAM, pulls, and four input-monitor taps share
the trunk. Loading requires the Z80 running under injected instructions,
not held in reset. BUSACK is retained for flash quiescence, never Pico DMA.

## 5.4 Implementation Wiring Index

- [Pico connections](../implementation/phase-1-supervisor.md#wiring-pico-2-w)
- [Fixed logic and output buffer](../implementation/phase-2-buffer-clock.md#wiring-output-buffer-and-fixed-logic)
- [Control and port monitor](../implementation/phase-3-address-generator.md#wiring-control-and-port-monitor)
- [Data paths](../implementation/phase-5-data-bus.md#wiring-bidirectional-data-path)
- [SRAM and CPU insertion](../implementation/phase-6-sram.md#wiring-sram-socket)
