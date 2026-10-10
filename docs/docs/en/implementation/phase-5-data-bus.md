# Phase 5 - Data Translation

<div data-checklist="phase-5" data-checklist-label="Phase 5" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 5 prerequisite passed"> **Prerequisite:** [Phase 4](phase-4-address-bus.md#pass-gate).
**Install:** U9 AHCT245 and U10 LVC245. CPU/SRAM still absent.

## Wiring - Bidirectional Data Path

<input type="checkbox" data-checklist-id="data-wiring" aria-label="All data branches and translator fixed pins wired and checked"> Wire every D0-D7 branch from the [data table](../hardware/pin-mapping.md#sram-address-and-data-trunks)
and [translator fixed pins](../hardware/bus-isolation.md#51-data-paths).
<input type="checkbox" data-checklist-id="supplies-directions" aria-label="Translator supplies and directions checked before insertion"> U9 runs at +5 V with DIR HIGH; U10 at 3.3 V with DIR LOW.

<input type="checkbox" data-checklist-id="oe-pulls-isolation" aria-label="Both OE routes data pulls and Pico voltage isolation checked"> Check both OE routes
and the eight individual 10 kOhm Pico data pull-downs. Confirm no raw 5 V bus
reaches Pico GPIOs.

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
