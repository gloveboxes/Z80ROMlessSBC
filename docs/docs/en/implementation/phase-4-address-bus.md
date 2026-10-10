# Phase 4 - Address-Trunk Wiring

<div data-checklist="phase-4" data-checklist-label="Phase 4" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.
Leave the optional active diagnostic unticked until its Phase 6 conditions hold.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 4 prerequisite passed"> **Prerequisite:** [Phase 3](phase-3-address-generator.md#pass-gate).
**Install:** No further active devices. Keep CPU and SRAM absent until Phase 6.

<input type="checkbox" data-checklist-id="address-trunk" aria-label="All sixteen address nets wired"> Wire all sixteen Z80-to-SRAM address nets from the
[pin table](../hardware/pin-mapping.md#sram-address-and-data-trunks). Each bit has
its own 10 kOhm pull to +5 V.

<input type="checkbox" data-checklist-id="monitor-taps" aria-label="Address monitor taps and exclusive CPU drive checked"> A0/A1/A2/A4 also tap U7; no Pico or expander
output drives any address bit.

<input type="checkbox" data-checklist-id="a16-ground" aria-label="SRAM A16 ground connection checked"> Tie SRAM A16 to GND.

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
