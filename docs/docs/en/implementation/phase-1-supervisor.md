# Phase 1 - Pico Supervisor

**Prerequisite:** [Phase 0 pass gate](phase-0-power.md#pass-gate).
**Install:** Pico 2 W only; all other active sockets empty.

## Wiring - Pico 2 W

Program Stage 1 off-board, disconnect USB, fit its soldered headers with USB
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

Build `z80_stage01_supervisor`; load
`build/src/stage01_supervisor/z80_stage01_supervisor.uf2` using the
[firmware/console procedure](../system/firmware-build.md#load-a-stage-and-open-its-console).
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c).

| Command | Behavior | Measurement |
| --- | --- | --- |
| `s` | Sample input/data levels | Monitor inputs may float with U7 absent |
| `w` | Walk seven control outputs, 250 ms each | Scope the correct empty socket contact and neighbors |
| `r` | Hold RESET# alternately HIGH/LOW | GP3/header 5 and Z80 socket 26: 3.20-3.40 V / near zero |
| `d` | Hold one data GPIO HIGH; advance D0-D7 | Probe U9/U10 A pins 2-9 |
| `x` | Restore defaults and data inputs | Both OE requests HIGH; RESET LOW |

## Single-Step Data-Pin Test

Type `d` without Enter. One GP10-GP17 output stays HIGH until the next `d`;
all others remain LOW. The ninth step restores input mode. If the initial
inputs are not all LOW, firmware refuses the test: check the eight individual
10 kOhm GP10-GP17 pull-down resistors.
Use `x` to stop. USB disconnect restores any held data/reset test.

## Test Plan

Apply external +5 V before USB. Check Pico 3.3 V and that no GPIO sees +5 V.
At startup GP6/GP7/GP4/GP5 must be HIGH; GP2/GP3/GP9 LOW. Scope each walking
output at its empty destination; software PASS only means the sequence ended.
Use held `r`/`d` levels for meter checks. Do not run these commands with CPU,
SRAM, or any other active device fitted.

## Pass Gate

Correct routing, isolated defaults, no neighboring pin activity, and valid
rail voltages. Floating monitor readings are not a pass or a fault by themselves.

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
