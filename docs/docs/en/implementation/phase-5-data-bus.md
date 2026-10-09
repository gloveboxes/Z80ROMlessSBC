# Phase 5 - Data Translation

**Prerequisite:** [Phase 4](phase-4-address-bus.md#pass-gate).
**Install:** U9 AHCT245 and U10 LVC245. CPU/SRAM still absent.

## Wiring - Bidirectional Data Path

Wire every D0-D7 branch from the [data table](../hardware/pin-mapping.md#sram-address-and-data-trunks)
and [translator fixed pins](../hardware/bus-isolation.md#51-data-paths).
U9 runs at +5 V with DIR HIGH; U10 at 3.3 V with DIR LOW. Check both OE routes
and RN3 pulls. Confirm no raw 5 V bus reaches Pico GPIOs.

## Firmware and Tests

Build `z80_stage05_data_bus`; UF2 lives in `build/src/stage05_data_bus/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage05_data_bus/main.c).

For upward `d` patterns, temporarily pull **raw RD# LOW through 1 kOhm** with
CPU absent; otherwise the HCT32 correctly keeps U9 disabled. Keep SRAM absent
and boot inhibit HIGH. Scope 00/FF/55/AA and walking-bit patterns on all eight
bus lines. Verify U10 remains disabled throughout upward drive.

Remove the temporary RD pull. For `i`, apply weak external test pulls to
D0-D7 and sample the downward path; U9 must remain disabled. `e` repeats 1000
direction changes with external AA test pulls. Use 10 kOhm test pulls so the
upward drive can override them; do not attach another push-pull bus driver.
`x` isolates both paths. The software result does not measure voltage.

## Pass Gate

Correct 5 V upward and 3.3 V downward levels, stable disabled paths, no
simultaneous OE assertion, and no excessive current. Remove every temporary
test pull before fitting CPU/SRAM in Phase 6.
