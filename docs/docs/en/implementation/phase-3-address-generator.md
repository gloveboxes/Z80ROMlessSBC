# Phase 3 - Control and Port Monitor

<div data-checklist="phase-3" data-checklist-label="Phase 3" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 3 prerequisite passed"> **Prerequisite:** [Phase 2](phase-2-buffer-clock.md#pass-gate).
**Install:** U7 SN74LVC244AN only; Z80/SRAM remain absent.
The historical source-directory name is retained, but no address expander is fitted.

## Wiring - Control and Port Monitor

<input type="checkbox" data-checklist-id="monitor-wiring" aria-label="All eight monitor channels wired and checked"> Wire all eight channels from the
[monitor table](../hardware/address-interface.md#port-address-monitor).
<input type="checkbox" data-checklist-id="monitor-fixed-pins" aria-label="Monitor supply enables and bypass checked before installation"> U7 pins 1/10/19 go to GND, pin 20 to Pico-derived 3.3 V, with 100 nF nearby.
No unused monitor channels remain. Never feed 5 V directly into the Pico.

## Firmware and Tests

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 3 firmware built and loaded"> Build `z80_stage03_control_inputs`; UF2:
`build/src/stage03_mcp23s17/z80_stage03_control_inputs.uf2`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage03_mcp23s17/main.c).

<input type="checkbox" data-checklist-id="idle-inputs" aria-label="Monitor idle controls and port measured"> `s` samples BUSACK/IORQ/RD/WR and `port`. With CPU absent and pulls fitted,
controls are HIGH and port is `17`.

<input type="checkbox" data-checklist-id="input-low-tests" aria-label="Every monitor input LOW test passed"> Pull each input LOW through a temporary
1 kOhm resistor; confirm only the matching GPIO/decoded bit changes and U7
outputs remain in the 3.3 V domain. Never short output pins to a rail.

<input type="checkbox" data-checklist-id="wait-release" aria-label="Independent WAIT assertion and release measured"> Pull IORQ LOW and verify WAIT LOW. Type `w` to raise IO_RELEASE: WAIT must
rise without either data OE becoming active.

<input type="checkbox" data-checklist-id="wait-rearm-fixture" aria-label="WAIT rearmed and temporary monitor pulls removed"> Type `x` to rearm WAIT, then
remove the temporary pull. This tests WAIT independently of data direction.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-routes" aria-label="Phase 3 all eight input routes passed"> All eight input routes correct.
- <input type="checkbox" data-checklist-id="gate-wait" aria-label="Phase 3 WAIT release with disabled data paths passed"> WAIT release works with data paths disabled.
- <input type="checkbox" data-checklist-id="gate-decode" aria-label="Phase 3 masked address decode passed"> Decoded address bits match `address & 0x17`, not a full 8-bit decode.

</div>

## Maintained Source

The complete application below is included from the repository at documentation
build time, not copied into this page.

??? example "Stage 3 application - src/stage03_mcp23s17/main.c"

    ```c
    {% include "../../../../src/stage03_mcp23s17/main.c" %}
    ```
