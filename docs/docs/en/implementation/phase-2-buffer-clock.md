# Phase 2 - Output Buffer, Fixed Logic, and Clock

<div data-checklist="phase-2" data-checklist-label="Phase 2" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 2 prerequisite passed"> **Prerequisite:** [Phase 1](phase-1-supervisor.md#pass-gate).
**Install:** U4 SN74AHCT244N and U3 SN74HCT32N. CPU/SRAM/data translators
remain absent. No device requires programming.

## Wiring - Output Buffer and Fixed Logic

<input type="checkbox" data-checklist-id="buffer-gate-wiring" aria-label="Buffer and gate channels and supplies wired"> Wire every channel and supply from [the buffer and gate tables](../hardware/output-buffer.md).
Keep U4 pin 18 to Z80 socket pin 6 entirely on Core.

<input type="checkbox" data-checklist-id="fixed-pins" aria-label="Buffer and gate fixed pins checked"> Tie both U4 OEs to GND,
unused U4 inputs 15/17 and U3 inputs 12/13 to GND; leave their outputs open.
<input type="checkbox" data-checklist-id="continuity-install" aria-label="Buffer and gate continuity checked before installation"> Check continuity before inserting either device.

{%
  include-markdown "../hardware/output-buffer.md"
  start='<template id="phase-2-output-buffer-wiring">'
  end="phase-2-output-buffer-wiring-end</template>"
%}

## Firmware and Tests

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 2 firmware built and loaded"> Build `z80_stage02_buffers_clock`; load its UF2 from
`build/src/stage02_buffers_clock/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/main.c).

<input type="checkbox" data-checklist-id="held-buffer-levels" aria-label="All three buffer input and output level pairs measured"> `d` holds LOW then HIGH on GP2, GP4, GP5 in order. Probe U4 input/output pairs
2/18, 4/16, 6/14 and their empty destinations. Inputs should be near 0/3.3 V;
outputs near 0/+5 V, LOW below 0.3 V and HIGH at least 4.4 V. The seventh
step restores defaults; `x` restores at any time.

<input type="checkbox" data-checklist-id="clock-waveforms" aria-label="All three unloaded clock rates qualified"> `1`, `2`, `3` select 1 kHz, 100 kHz, 1 MHz. Probe U4 pin 18 and Z80 socket
pin 6: correct frequency, 45-55% duty, no runt pulses or ringing across CPU
thresholds. The clock HIGH must satisfy the Z80 clock's VCC-minus-0.5 V
requirement; a meter does not qualify pulse shape.

<input type="checkbox" data-checklist-id="inhibit-wait" aria-label="SRAM inhibit and WAIT behavior measured"> With CPU absent, manually drive raw RD#/IORQ# through 1 kOhm to GND, one
signal at a time. Confirm SRAM OE remains HIGH while boot inhibit is HIGH;
confirm WAIT LOW only while IORQ LOW and IO_RELEASE LOW.

<input type="checkbox" data-checklist-id="gate-truth-tables" aria-label="Every OR gate input combination measured"> Test all four input
combinations of each OR gate using isolated, current-limited logic sources;
do not use the walking Stage 1 image after these devices are installed.
Never short an active output to a rail.

<input type="checkbox" data-checklist-id="fixture-removed" aria-label="Phase 2 temporary wiring restored"> Restore temporary wiring afterward.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-buffers" aria-label="Phase 2 buffer and gate measurements passed"> All buffer/gate truth tables measured correctly.
- <input type="checkbox" data-checklist-id="gate-clock" aria-label="Phase 2 unloaded clock levels passed"> Clock levels measured correctly.

Start later
execution at 1 MHz, not the CPU's 20 MHz rating. This phase qualifies the
unloaded socket clock only; repeat with the CPU fitted.

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Stage 2 application - src/stage02_buffers_clock/main.c"

    ```c
    {% include "../../../../src/stage02_buffers_clock/main.c" %}
    ```

??? example "Shared clock generation - src/common/clock.c"

    ```c
    {% include "../../../../src/common/clock.c" %}
    ```
