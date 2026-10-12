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

### Monitor taps and fixed address

U7 is the SN74LVC244AN input buffer. Verify the four taps already wired in
Phase 3; do not add duplicate wires. A3 is used by SRAM but is not monitored.

| Done | Signal / check | Required connection |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="monitor-a0" aria-label="A0 shared net tap to U7 checked"> | A0 | Z80 pin 30 / SRAM pin 12 net → U7 input pin 11 |
| <input type="checkbox" data-checklist-id="monitor-a1" aria-label="A1 shared net tap to U7 checked"> | A1 | Z80 pin 31 / SRAM pin 11 net → U7 input pin 13 |
| <input type="checkbox" data-checklist-id="monitor-a2" aria-label="A2 shared net tap to U7 checked"> | A2 | Z80 pin 32 / SRAM pin 10 net → U7 input pin 15 |
| <input type="checkbox" data-checklist-id="monitor-a4" aria-label="A4 shared net tap to U7 checked"> | A4 | Z80 pin 34 / SRAM pin 8 net → U7 input pin 17 |
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

## Optional Active Address Diagnostic

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
