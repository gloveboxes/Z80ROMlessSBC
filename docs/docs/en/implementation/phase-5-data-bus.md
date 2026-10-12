# Phase 5 - Data Translation

<div data-checklist="phase-5" data-checklist-label="Phase 5" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 5 prerequisite passed"> **Prerequisite:** [Phase 4](phase-4-address-bus.md#pass-gate).
**Install:** U9 AHCT245 and U10 LVC245. CPU/SRAM still absent.

## Wiring - Bidirectional Data Path

**U9 is the SN74AHCT245N Pico-to-5 V translator; U10 is the SN74LVC245AN
5 V-to-Pico translator.** Disconnect external power and USB and keep both
translators out of their sockets until wiring checks pass. CPU and SRAM
remain absent.

Each row below is one shared net with branches, not a series path through
a chip. Tick each row after checking every listed contact and isolation
from adjacent pins. See the [data table](../hardware/pin-mapping.md#sram-address-and-data-trunks)
and [translator reference](../hardware/bus-isolation.md#51-data-paths).

### 5 V data-bus branches

#### Z80 to SRAM

Rows are ordered by SRAM pin number, highest to lowest.

| Done | Signal | Z80 socket → SRAM socket |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="data-sram-d7" aria-label="D7 Z80 to SRAM hop checked"> | D7 | Z80 pin 13 → SRAM pin 21 |
| <input type="checkbox" data-checklist-id="data-sram-d6" aria-label="D6 Z80 to SRAM hop checked"> | D6 | Z80 pin 10 → SRAM pin 20 |
| <input type="checkbox" data-checklist-id="data-sram-d5" aria-label="D5 Z80 to SRAM hop checked"> | D5 | Z80 pin 9 → SRAM pin 19 |
| <input type="checkbox" data-checklist-id="data-sram-d4" aria-label="D4 Z80 to SRAM hop checked"> | D4 | Z80 pin 7 → SRAM pin 18 |
| <input type="checkbox" data-checklist-id="data-sram-d3" aria-label="D3 Z80 to SRAM hop checked"> | D3 | Z80 pin 8 → SRAM pin 17 |
| <input type="checkbox" data-checklist-id="data-sram-d2" aria-label="D2 Z80 to SRAM hop checked"> | D2 | Z80 pin 12 → SRAM pin 15 |
| <input type="checkbox" data-checklist-id="data-sram-d1" aria-label="D1 Z80 to SRAM hop checked"> | D1 | Z80 pin 15 → SRAM pin 14 |
| <input type="checkbox" data-checklist-id="data-sram-d0" aria-label="D0 Z80 to SRAM hop checked"> | D0 | Z80 pin 14 → SRAM pin 13 |

#### Z80 to U9 AHCT245, then U10 LVC245

Keep the same D7-to-D0 order. Use the second free hole connected to each
Z80 data pin for the hop to U9. At U9, use another hole in the same connected
breadboard group for the hop to U10. The signal does not pass through U9's
circuitry: all four IC contacts for a bit remain on one shared net.
Tick each row after checking both hops.

| Done | Signal | Z80 socket → U9 AHCT245 B pin | U9 AHCT245 B pin → U10 LVC245 B pin |
| --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="data-translators-d7" aria-label="D7 Z80 to U9 and U9 to U10 hops checked"> | D7 | Z80 pin 13 → U9 pin 11 | U9 pin 11 → U10 pin 11 |
| <input type="checkbox" data-checklist-id="data-translators-d6" aria-label="D6 Z80 to U9 and U9 to U10 hops checked"> | D6 | Z80 pin 10 → U9 pin 12 | U9 pin 12 → U10 pin 12 |
| <input type="checkbox" data-checklist-id="data-translators-d5" aria-label="D5 Z80 to U9 and U9 to U10 hops checked"> | D5 | Z80 pin 9 → U9 pin 13 | U9 pin 13 → U10 pin 13 |
| <input type="checkbox" data-checklist-id="data-translators-d4" aria-label="D4 Z80 to U9 and U9 to U10 hops checked"> | D4 | Z80 pin 7 → U9 pin 14 | U9 pin 14 → U10 pin 14 |
| <input type="checkbox" data-checklist-id="data-translators-d3" aria-label="D3 Z80 to U9 and U9 to U10 hops checked"> | D3 | Z80 pin 8 → U9 pin 15 | U9 pin 15 → U10 pin 15 |
| <input type="checkbox" data-checklist-id="data-translators-d2" aria-label="D2 Z80 to U9 and U9 to U10 hops checked"> | D2 | Z80 pin 12 → U9 pin 16 | U9 pin 16 → U10 pin 16 |
| <input type="checkbox" data-checklist-id="data-translators-d1" aria-label="D1 Z80 to U9 and U9 to U10 hops checked"> | D1 | Z80 pin 15 → U9 pin 17 | U9 pin 17 → U10 pin 17 |
| <input type="checkbox" data-checklist-id="data-translators-d0" aria-label="D0 Z80 to U9 and U9 to U10 hops checked"> | D0 | Z80 pin 14 → U9 pin 18 | U9 pin 18 → U10 pin 18 |

### Pico-side data branches and pull-downs

Pico pin numbers are physical pin positions; GPIO names are in parentheses.
Verify the eight individual 10 kOhm pull-downs fitted in Phase 0, not a
second set. These pulls belong on the Pico side, not the 5 V data bus.

| Done | Signal | Pico pin | U9 AHCT245 A pin | U10 LVC245 A pin | Existing pull-down |
| --- | --- | --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="data-pico-d0" aria-label="D0 Pico branches and pull-down checked"> | D0 | Pico pin 14 (GP10) | U9 pin 2 | U10 pin 2 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d1" aria-label="D1 Pico branches and pull-down checked"> | D1 | Pico pin 15 (GP11) | U9 pin 3 | U10 pin 3 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d2" aria-label="D2 Pico branches and pull-down checked"> | D2 | Pico pin 16 (GP12) | U9 pin 4 | U10 pin 4 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d3" aria-label="D3 Pico branches and pull-down checked"> | D3 | Pico pin 17 (GP13) | U9 pin 5 | U10 pin 5 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d4" aria-label="D4 Pico branches and pull-down checked"> | D4 | Pico pin 19 (GP14) | U9 pin 6 | U10 pin 6 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d5" aria-label="D5 Pico branches and pull-down checked"> | D5 | Pico pin 20 (GP15) | U9 pin 7 | U10 pin 7 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d6" aria-label="D6 Pico branches and pull-down checked"> | D6 | Pico pin 21 (GP16) | U9 pin 8 | U10 pin 8 | 10 kOhm to GND |
| <input type="checkbox" data-checklist-id="data-pico-d7" aria-label="D7 Pico branches and pull-down checked"> | D7 | Pico pin 22 (GP17) | U9 pin 9 | U10 pin 9 | 10 kOhm to GND |

### Supplies, directions and enables

Verify existing Phase 0/1/2 connections rather than adding duplicate wires
or capacitors. Supply, ground and DIR links are direct wires, not resistors.

| Done | Pin / component | Required connection |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="data-u9-supply" aria-label="U9 5 V supply checked"> | U9 pin 20, VCC | +5 V |
| <input type="checkbox" data-checklist-id="data-u9-ground" aria-label="U9 ground checked"> | U9 pin 10, GND | Common GND |
| <input type="checkbox" data-checklist-id="data-u9-direction" aria-label="U9 A to B direction checked"> | U9 pin 1, DIR | +5 V: A to B |
| <input type="checkbox" data-checklist-id="data-u10-supply" aria-label="U10 Pico derived 3.3 V supply checked"> | U10 pin 20, VCC | Pico-derived +3.3 V, not +5 V |
| <input type="checkbox" data-checklist-id="data-u10-ground" aria-label="U10 ground checked"> | U10 pin 10, GND | Common GND |
| <input type="checkbox" data-checklist-id="data-u10-direction" aria-label="U10 B to A direction checked"> | U10 pin 1, DIR | Common GND: B to A |
| <input type="checkbox" data-checklist-id="data-u9-oe" aria-label="U9 gated output enable route checked"> | U9 pin 19, OE# | U3 HCT32 output pin 8; U3 input pin 9 receives Z80 RD# pin 21 and input pin 10 receives Pico pin 9 (GP6) |
| <input type="checkbox" data-checklist-id="data-u10-oe" aria-label="U10 output enable route checked"> | U10 pin 19, OE# | Pico pin 10 (GP7) |
| <input type="checkbox" data-checklist-id="data-bypass" aria-label="Both translator bypass capacitors checked"> | C6/C7, 100 nF each | C6 near U9 pin 20 across +5 V/GND; C7 near U10 pin 20 across +3.3 V/GND |
| <input type="checkbox" data-checklist-id="data-install" aria-label="Translator voltage isolation and orientation checked before insertion"> | Installation | Confirm no raw 5 V bus reaches Pico pins; verify rails, continuity, adjacent-pin isolation and pin-1 orientation before fitting U9/U10 with power and USB disconnected |

Once fitted, apply external +5 V before USB and disconnect USB before
removing +5 V. U9 lacks data-port power-off isolation; disabling OE does
not make USB-only operation safe. Never enable both data paths at once.

## Firmware and Tests

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 5 firmware built and loaded"> Build `z80_stage05_data_bus`; UF2 lives in `build/src/stage05_data_bus/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage05_data_bus/main.c).

<input type="checkbox" data-checklist-id="upward-fixture" aria-label="Upward data test fixture fitted with CPU and SRAM absent"> For upward `d` patterns, temporarily pull **raw RD# LOW through 1 kOhm** with
CPU absent; otherwise the HCT32 correctly keeps U9 disabled. Keep SRAM absent
and boot inhibit HIGH.

<input type="checkbox" data-checklist-id="upward-patterns" aria-label="Every upward data pattern and downward isolation measured"> Scope 00/FF/55/AA and walking-bit patterns on all eight
bus lines. Verify U10 remains disabled throughout upward drive.

<input type="checkbox" data-checklist-id="downward-patterns" aria-label="Downward test samples verified with upward path disabled"> Remove the temporary RD pull. For `i`, apply weak external test pulls to
D0-D7 and sample the downward path; U9 must remain disabled.

<input type="checkbox" data-checklist-id="endurance-fixture" aria-label="Endurance RD and 0xAA test pulls fitted with power disconnected"> For `e`, **reinstall the 1 kOhm raw RD# pull-down** at the empty Z80 socket
pin 21, keeping CPU/SRAM absent and boot inhibit HIGH. Fit external `0xAA`
test pulls: D1/D3/D5/D7 each through 10 kOhm to +5 V, and D0/D2/D4/D6 each
through 10 kOhm to GND. Do not attach another push-pull bus driver. Disconnect
USB and external power before changing any test pull, then restore external
+5 V before USB.

<input type="checkbox" data-checklist-id="endurance-oe-capture" aria-label="Endurance actual OE captures verified with no overlap"> `e` repeats 1000 drive/isolate/receive cycles. Capture U9 OE# pin 19 and U10
OE# pin 19: require each to go LOW in its respective phase, an interval with
both HIGH between phases, and no overlapping LOW enables. Without the RD#
pull-down, U9 stays disabled and the test can report PASS just by reading the
external `0xAA` pulls. Even with the correct fixture, byte readback alone does
not prove upward drive or exclusion; retain the upward `d` captures and the
actual-OE captures as separate pass evidence.

<input type="checkbox" data-checklist-id="restore-isolation" aria-label="Both data paths restored to isolation"> `x` isolates both paths. The software result does not measure voltage.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-levels" aria-label="Phase 5 upward and downward levels passed"> Correct 5 V upward and 3.3 V downward levels.
- <input type="checkbox" data-checklist-id="gate-isolation" aria-label="Phase 5 disabled paths and exclusive enables passed"> Stable disabled paths and no simultaneous OE assertion.
- <input type="checkbox" data-checklist-id="gate-current" aria-label="Phase 5 current check passed"> No excessive current.
- <input type="checkbox" data-checklist-id="gate-fixture-removed" aria-label="Every temporary data test pull removed before Phase 6"> Remove every temporary test pull before fitting CPU/SRAM in Phase 6.

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page. The shared bus module also implements the
four-bit port sampling used in Phase 3.

??? example "Stage 5 application - src/stage05_data_bus/main.c"

    ```c
    {% include "../../../../src/stage05_data_bus/main.c" %}
    ```

??? example "Shared data paths and port sampling - src/common/bus.c"

    ```c
    {% include "../../../../src/common/bus.c" %}
    ```
