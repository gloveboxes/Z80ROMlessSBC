# Phase 4 - Address-Trunk Wiring

<div data-checklist="phase-4" data-checklist-label="Phase 4" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.
Leave the optional active diagnostic unticked until its Phase 6 conditions hold.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 4 prerequisite passed"> **Prerequisite:** [Phase 3](phase-3-address-generator.md#pass-gate).
**Install:** No further active devices. Keep CPU and SRAM absent until Phase 6.

## Wiring - Address Trunk

Disconnect external power and USB; keep CPU and SRAM absent. Each row is
one shared address net, not a series path through a device. Tick each row
after checking its wire and existing 10 kOhm pull to +5 V at the actual
socket contacts. The pulls were fitted in Phase 0; do not add a second set.
See the [pin table](../hardware/pin-mapping.md#sram-address-and-data-trunks).
Rows are ordered by SRAM pin number, highest to lowest.

| Done | Signal | Z80 socket → SRAM socket | Existing pull-up |
| --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="address-a15" aria-label="A15 wire and pull-up checked"> | A15 | Z80 pin 5 → SRAM pin 31 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a13" aria-label="A13 wire and pull-up checked"> | A13 | Z80 pin 3 → SRAM pin 28 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a8" aria-label="A8 wire and pull-up checked"> | A8 | Z80 pin 38 → SRAM pin 27 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a9" aria-label="A9 wire and pull-up checked"> | A9 | Z80 pin 39 → SRAM pin 26 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a11" aria-label="A11 wire and pull-up checked"> | A11 | Z80 pin 1 → SRAM pin 25 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a10" aria-label="A10 wire and pull-up checked"> | A10 | Z80 pin 40 → SRAM pin 23 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a0" aria-label="A0 wire and pull-up checked"> | A0 | Z80 pin 30 → SRAM pin 12 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a1" aria-label="A1 wire and pull-up checked"> | A1 | Z80 pin 31 → SRAM pin 11 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a2" aria-label="A2 wire and pull-up checked"> | A2 | Z80 pin 32 → SRAM pin 10 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a3" aria-label="A3 wire and pull-up checked"> | A3 | Z80 pin 33 → SRAM pin 9 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a4" aria-label="A4 wire and pull-up checked"> | A4 | Z80 pin 34 → SRAM pin 8 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a5" aria-label="A5 wire and pull-up checked"> | A5 | Z80 pin 35 → SRAM pin 7 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a6" aria-label="A6 wire and pull-up checked"> | A6 | Z80 pin 36 → SRAM pin 6 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a7" aria-label="A7 wire and pull-up checked"> | A7 | Z80 pin 37 → SRAM pin 5 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a12" aria-label="A12 wire and pull-up checked"> | A12 | Z80 pin 2 → SRAM pin 4 | 10 kOhm to +5 V |
| <input type="checkbox" data-checklist-id="address-a14" aria-label="A14 wire and pull-up checked"> | A14 | Z80 pin 4 → SRAM pin 3 | 10 kOhm to +5 V |

The U7 monitor taps were wired in
[Phase 3](phase-3-address-generator.md#wiring-control-and-port-monitor);
no additional monitor wires are needed here.

| Done | Signal / check | Required connection |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="address-cpu-only" aria-label="Exclusive CPU address drive checked"> | Address ownership | No Pico or expander output connects to any address bit; only the Z80 drives A0-A15 |
| <input type="checkbox" data-checklist-id="a16-ground" aria-label="SRAM A16 pin 2 ground connection checked"> | A16 | Verify SRAM pin 2 is directly connected to common GND, selecting the lower 64 KiB |

## Tests

<input type="checkbox" data-checklist-id="socket-continuity" aria-label="Address socket continuity and isolation passed"> With power/USB disconnected, check every actual CPU/SRAM socket contact and
isolation from neighboring nets.

<input type="checkbox" data-checklist-id="address-high-levels" aria-label="Pulled-up address levels measured"> With power applied and CPU absent, verify
pulled-up address levels.

<input type="checkbox" data-checklist-id="monitor-low-tests" aria-label="Address monitor LOW tests passed and temporary pulls removed"> Temporary 1 kOhm LOW tests must change only the
intended monitor bit. Remove the temporary pulls afterward with power and USB
disconnected. Continue to Phase 5 without inserting CPU/SRAM.

## Exception: Reset-Held Multimeter Guide for a Partially Populated Board

If the Z80 and SRAM are **already fitted**, together with Pico 2 W, U3
SN74HCT32N and U4 SN74AHCT244N, use the separate `z80_reset_meter`
application for static measurements. U7 SN74LVC244AN, U9 SN74AHCT245N
and U10 SN74LVC245AN may be absent. This is a troubleshooting exception,
**not permission to skip the normal population order or a Phase 4/6 pass**.
Do not use the ordinary Stage 2 output walk or the active address diagnostic
with this partial population.

The guide never releases reset, drives the data bus, reads missing monitor
buffers, or starts a free-running clock. It holds each clock HIGH and LOW
on separate commands so a multimeter can settle. Six complete reset clocks
are generated only as you explicitly advance through twelve held levels.
The SRAM output inhibit remains asserted throughout.

### Build and open the console

From the repository root, using the
[firmware toolchain](../system/firmware-build.md):

```sh
cmake --build build --target z80_reset_meter
```

UF2: `build/src/stage02_buffers_clock/z80_reset_meter.uf2`.
The [maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/reset_meter.c)
prints the full procedure, pin numbers, expected readings and next command
in the USB serial console. Select the Pico USB serial port, 115200 baud
(USB CDC does not use this rate for signal timing). The guide waits for
the console connection before printing. Reconnecting restores safe levels
and starts the guide again; a disconnected console does not leave CLK HIGH.

Before powering or flashing, verify orientations, common GND, bypasses,
startup pulls, and absence of any raw 5 V connection to Pico GPIOs.
All installed 5 V ICs must be powered. Apply external 5 V **before USB**;
disconnect USB **before external 5 V**. Do not program this populated board
with USB alone. If BOOTSEL flashing is needed, hold BOOTSEL while connecting
USB after external 5 V is present. Do not change wiring or insert/remove
ICs with either supply connected.

### Meter and keypress procedure

Use **DC volts**, black lead in COM and red lead in V, not the current
socket. Connect black to common circuit GND. Measure named pins with the
red probe, using insulated tips to avoid bridging adjacent contacts. Power
off before moving clips. IC pins are numbered from the package notch/dot,
not by breadboard row; U4 faces notch-down in the documented layout.

Type **one command letter, then Enter**. Do not paste command sequences or
hold a key. An empty Enter does nothing; CR/LF/CRLF are supported.

| Command | Action |
| --- | --- |
| `n` + Enter | Acknowledge the current measurements and advance one step |
| `r` + Enter | Repeat current instructions without changing the held level |
| `h` + Enter | Show help and the current measurement instructions |
| `x` | Immediately stop without waiting for Enter; restore CLK LOW, RESET# LOW and read inhibit HIGH |
| `b` + Enter | Restart at preparation, with safe levels restored |

On any unexpected voltage, press `x`, record the actual reading, and
disconnect USB then external power before investigating. Do not advance.
After `x`, Enter clears the discarded input line; then `b` + Enter restarts.

| Console step | Measure / expected outcome |
| --- | --- |
| 1: preparation | Confirm the hardware and meter safety instructions; CLK and RESET# held LOW |
| 2: power | U1.11, U2.32/U2.30, U3.14 and U4.20: nominal 5 V, bench target 4.75-5.25 V; Pico header 36: nominal 3.3 V, target 3.2-3.4 V; listed ground/enable/A16 pins near 0 V |
| 3: safe outputs | RESET# and CLK LOW; BUSREQ# inactive HIGH; inhibit HIGH; both data enables off; SRAM OE# HIGH. Follow every Pico/input/output endpoint printed in the console |
| 4-15: six reset clocks | Alternate one held HIGH and one held LOW per `n` + Enter; measure GP2/header 4 and U4.2 at approximately 3.3/0 V, and U4.18/Z80.6 at approximately 5/0 V. RESET# stays LOW and SRAM OE# stays HIGH |
| 16: reset controls | MREQ#/WR#/RD# inactive HIGH; buffered SRAM CE#/WE# and read-control outputs near 5 V |
| 17: static HCT32 | SRAM OE#, WAIT# and upward-path OE# HIGH at the listed endpoints; inhibit HIGH and IO_RELEASE LOW |
| 18: address pulls | All sixteen SRAM address pins near 5 V through the existing pulls; A16 LOW. Exact pins are printed |
| 19: complete | CLK LOW, reset asserted, no additional clocks. Save actual voltage readings; there is no automatic hardware PASS |

Typical LOW target is at most 0.3 V. Pico HIGH is near 3.3 V; buffered
5 V HIGH should be within 0.5 V of its measured rail under this light load.
Raw CPU HIGH is TTL and may be below 5 V (expect at least 2.4 V).
These are conservative troubleshooting indications, **not timing or full
datasheet qualification**.

The address check sees pull-ups on a floated bus, not CPU address drive.
The floating 5 V data bus has no useful expected byte. This procedure cannot
prove instruction execution, SRAM contents, address uniqueness, full OR-gate
truth tables, clock edge quality or pulse timing. It does not replace
unpowered continuity checks, the missing buffers, or Phase 6 instrument
qualification.

## Optional Active Address Diagnostic

For **all ICs installed** and console control of each loader clock,
use the [Phase 6 console-stepped miniature loader](phase-6-sram.md#console-stepped-miniature-loader).
It writes the three specified test bytes, clears the Z80 accumulator before
each read-back, and prints expected versus monitored bus behavior.
It is separate from the reset-held partial-population guide above.

The same firmware now accepts **`f` + Enter** for the
[automatic full 64 KiB test](phase-6-sram.md#automatic-full-64-kib-test):
fill/read the sequential low-byte pattern, then fill/read the high-byte
pattern, with one concise address/value/expected/SUCCESS-or-FAIL line per
verified address. `x` aborts; all ICs and Phase 6 wiring are required.

<input type="checkbox" data-checklist-id="optional-active-firmware" aria-label="Optional active diagnostic loaded only after Phase 6 qualification"> **Only after Phase 6 hardware is fitted and loader timing is qualified**, build
`z80_stage04_address_bus`; UF2 lives in `build/src/stage04_address_bus/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage04_address_bus/main.c).
`a` injects writes/readback at walking-one addresses; `x` holds reset and
isolates. This is destructive RAM testing, not a held DC address generator.

<input type="checkbox" data-checklist-id="optional-active-captures" aria-label="Optional active address and write control captures verified"> Use separate analyzer captures of the complete address bus and write controls
to confirm addresses actually appear during CPU writes. A successful byte
readback alone cannot prove absence of all address aliases; the full-memory
tests in Phase 6 are required.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-address-map" aria-label="Phase 4 all sixteen address mappings passed"> Sixteen distinct, correctly mapped address nets.
- <input type="checkbox" data-checklist-id="gate-isolation" aria-label="Phase 4 address net isolation passed"> No shorts.
- <input type="checkbox" data-checklist-id="gate-monitor-taps" aria-label="Phase 4 address monitor taps passed"> Valid monitor taps.

No active-load claim is made until Phase 6.

</div>

## Maintained Source

The complete application below is included from the repository at documentation
build time, not copied into this page. Its active diagnostic remains deferred
until Phase 6.

??? example "Stage 4 application - src/stage04_address_bus/main.c"

    ```c
    {% include "../../../../src/stage04_address_bus/main.c" %}
    ```
