# Phase 1 - Pico Supervisor

<div data-checklist="phase-1" data-checklist-label="Phase 1" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 1 prerequisite passed"> **Prerequisite:** [Phase 0 pass gate](phase-0-power.md#pass-gate).
**Install:** Pico 2 W only; all other active sockets empty.

## Wiring - Pico 2 W

<input type="checkbox" data-checklist-id="pico-wiring" aria-label="Pico installation and wiring complete"> Program Stage 1 off-board, disconnect USB, fit its soldered headers with USB
toward row 1, then wire every connection in the
[Pico table](../hardware/pin-mapping.md#10-raspberry-pi-pico-2-w-header-pin-map).
Continuity-check actual empty socket contacts and adjacent pins, not only
wire ends. Keep the clock path short with a nearby GND return; do not add
loops to equalize wire lengths.

{%
  include-markdown "../hardware/pin-mapping.md"
  start='<template id="phase-1-pico-wiring">'
  end="phase-1-pico-wiring-end</template>"
%}

## Stage 1 Firmware

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 1 firmware built and loaded"> Build `z80_stage01_supervisor`; load
`build/src/stage01_supervisor/z80_stage01_supervisor.uf2` using the
[firmware/console procedure](../system/firmware-build.md#load-a-stage-and-open-its-console).
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c).

| Done | Command | Behavior | Measurement |
| --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="sample-inputs" aria-label="Stage 1 input sampling recorded"> | `s` | Sample input/data levels | Monitor inputs may float with U7 absent |
| <input type="checkbox" data-checklist-id="walking-controls" aria-label="All seven walking control outputs measured"> | `w` | Walk seven control outputs, 250 ms each | Scope the correct empty socket contact and neighbors |
| <input type="checkbox" data-checklist-id="held-reset" aria-label="Held reset levels measured"> | `r` | Hold RESET# alternately HIGH/LOW | GP3/header 5 and Z80 socket 26: 3.20-3.40 V / near zero |
| <input type="checkbox" data-checklist-id="held-data" aria-label="All eight held data outputs measured"> | `d` | Hold one data GPIO HIGH; advance D0-D7 | Probe U9/U10 A pins 2-9 |
| <input type="checkbox" data-checklist-id="restore-defaults" aria-label="Stage 1 defaults restored and checked"> | `x` | Restore defaults and data inputs | Both OE requests HIGH; RESET LOW |

## Single-Step Data-Pin Test

Type `d` without Enter. One GP10-GP17 output stays HIGH until the next `d`;
all others remain LOW. The ninth step restores input mode. If the initial
inputs are not all LOW, firmware refuses the test: check the eight individual
10 kOhm GP10-GP17 pull-down resistors.
Use `x` to stop. USB disconnect restores any held data/reset test.

## Test Plan

<input type="checkbox" data-checklist-id="power-levels" aria-label="Pico supply and GPIO voltage checks passed"> Apply external +5 V before USB. Check Pico 3.3 V and that no GPIO sees +5 V.

<input type="checkbox" data-checklist-id="startup-levels" aria-label="Pico startup levels measured"> At startup GP6/GP7/GP4/GP5 must be HIGH; GP2/GP3/GP9 LOW.

Scope each walking
output at its empty destination; software PASS only means the sequence ended.
Use held `r`/`d` levels for meter checks. Do not run these commands with CPU,
SRAM, or any other active device fitted.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-routing" aria-label="Phase 1 routing and neighboring pin checks passed"> Correct routing and no neighboring pin activity.
- <input type="checkbox" data-checklist-id="gate-isolation" aria-label="Phase 1 isolated defaults verified"> Isolated defaults.
- <input type="checkbox" data-checklist-id="gate-rails" aria-label="Phase 1 rail voltages valid"> Valid rail voltages.

Floating monitor readings are not a pass or a fault by themselves.

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Stage 1 application - src/stage01_supervisor/main.c"

    ```c
    {% include "../../../../src/stage01_supervisor/main.c" %}
    ```

??? example "Shared startup and isolation - src/common/supervisor.c"

    ```c
    {% include "../../../../src/common/supervisor.c" %}
    ```
