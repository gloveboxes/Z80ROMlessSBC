# Phase 5 - Data Translation

**Prerequisite:** [Phase 4](phase-4-address-bus.md#pass-gate).
**Install:** U9 AHCT245 and U10 LVC245. CPU/SRAM still absent.

## Wiring - Bidirectional Data Path

Wire every D0-D7 branch from the [data table](../hardware/pin-mapping.md#sram-address-and-data-trunks)
and [translator fixed pins](../hardware/bus-isolation.md#51-data-paths).
U9 runs at +5 V with DIR HIGH; U10 at 3.3 V with DIR LOW. Check both OE routes
and the eight individual 10 kOhm Pico data pull-downs. Confirm no raw 5 V bus
reaches Pico GPIOs.

## Firmware and Tests

Build `z80_stage05_data_bus`; UF2 lives in `build/src/stage05_data_bus/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage05_data_bus/main.c).

For upward `d` patterns, temporarily pull **raw RD# LOW through 1 kOhm** with
CPU absent; otherwise the HCT32 correctly keeps U9 disabled. Keep SRAM absent
and boot inhibit HIGH. Scope 00/FF/55/AA and walking-bit patterns on all eight
bus lines. Verify U10 remains disabled throughout upward drive.

Remove the temporary RD pull. For `i`, apply weak external test pulls to
D0-D7 and sample the downward path; U9 must remain disabled.

For `e`, **reinstall the 1 kOhm raw RD# pull-down** at the empty Z80 socket
pin 21, keeping CPU/SRAM absent and boot inhibit HIGH. Fit external `0xAA`
test pulls: D1/D3/D5/D7 each through 10 kOhm to +5 V, and D0/D2/D4/D6 each
through 10 kOhm to GND. Do not attach another push-pull bus driver. Disconnect
USB and external power before changing any test pull, then restore external
+5 V before USB.

`e` repeats 1000 drive/isolate/receive cycles. Capture U9 OE# pin 19 and U10
OE# pin 19: require each to go LOW in its respective phase, an interval with
both HIGH between phases, and no overlapping LOW enables. Without the RD#
pull-down, U9 stays disabled and the test can report PASS just by reading the
external `0xAA` pulls. Even with the correct fixture, byte readback alone does
not prove upward drive or exclusion; retain the upward `d` captures and the
actual-OE captures as separate pass evidence.

`x` isolates both paths. The software result does not measure voltage.

## Pass Gate

Correct 5 V upward and 3.3 V downward levels, stable disabled paths, no
simultaneous OE assertion, and no excessive current. Remove every temporary
test pull before fitting CPU/SRAM in Phase 6.

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
