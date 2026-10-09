# Phase 4 - Address-Trunk Wiring

**Prerequisite:** [Phase 3](phase-3-address-generator.md#pass-gate).
**Install:** No further active devices. Keep CPU and SRAM absent until Phase 6.

Wire all sixteen Z80-to-SRAM address nets from the
[pin table](../hardware/pin-mapping.md#sram-address-and-data-trunks). Each bit has
its own 10 kOhm pull to +5 V. A0/A1/A2/A4 also tap U7; no Pico or expander
output drives any address bit. Tie SRAM A16 to GND.

With power/USB disconnected, check every actual CPU/SRAM socket contact and
isolation from neighboring nets. With power applied and CPU absent, verify
pulled-up address levels; temporary 1 kOhm LOW tests must change only the
intended monitor bit. Continue to Phase 5 without inserting CPU/SRAM.

## Optional Active Address Diagnostic

**Only after Phase 6 hardware is fitted and loader timing is qualified**, build
`z80_stage04_address_bus`; UF2 lives in `build/src/stage04_address_bus/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage04_address_bus/main.c).
`a` injects writes/readback at walking-one addresses; `x` holds reset and
isolates. This is destructive RAM testing, not a held DC address generator.

Use separate analyzer captures of the complete address bus and write controls
to confirm addresses actually appear during CPU writes. A successful byte
readback alone cannot prove absence of all address aliases; the full-memory
tests in Phase 6 are required.

## Pass Gate

Sixteen distinct, correctly mapped address nets, no shorts, and valid monitor
taps. No active-load claim is made until Phase 6.

## Maintained Source

The complete application below is included from the repository at documentation
build time, not copied into this page. Its active diagnostic remains deferred
until Phase 6.

??? example "Stage 4 application - src/stage04_address_bus/main.c"

    ```c
    {% include "../../../../src/stage04_address_bus/main.c" %}
    ```
