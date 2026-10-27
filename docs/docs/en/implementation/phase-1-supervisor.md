# Phase 1 - Pico Supervisor

## VS Code tasks

Choose **Tasks: Run Task** in the Z80ROMlessSBC workspace.

| Diagnostic | Build task | Deployment task |
| --- | --- | --- |
| Phase 1 supervisor | **Build Pico - Phase 1 supervisor** | **Flash Pico - Phase 1 supervisor** |

Deployment tasks force-update without checking the previous firmware phase.
Complete this phase's hardware prerequisites and save work before flashing. See
[Pico tools](../system/firmware-build.md#deployment-task-requirements).


<div data-checklist="phase-1" data-checklist-label="Phase 1" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 1 prerequisite passed"> **Prerequisite:** [Phase 0 pass gate](phase-0-power.md#pass-gate).
**Install:** Pico 2 W only; all other active sockets empty.

## Wiring - Pico 2 W

<input type="checkbox" data-checklist-id="pico-wiring" aria-label="Pico installation and wiring complete"> Program Phase 1 off-board, disconnect USB, fit its soldered headers with USB
toward row 1, then wire every connection in the
[Pico table](../hardware/pin-mapping.md#10-raspberry-pi-pico-2-w-header-pin-map).
Continuity-check actual empty socket contacts and adjacent pins, not only
wire ends. Keep the clock path short with a nearby GND return; do not add
loops to equalize wire lengths.

### Pico Control Outputs

| Done | Signal | Pico connection | Destination socket contact |
| --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="wire-gp2" aria-label="Pico GP2 clock wire continuity checked"> | CLK | Pico GP2 - header pin 4 | U4 AHCT244 input - pin 2 |
| <input type="checkbox" data-checklist-id="wire-gp3" aria-label="Pico GP3 reset wire continuity checked"> | RESET# | Pico GP3 - header pin 5 | U1 Z80 RESET# - pin 26 |
| <input type="checkbox" data-checklist-id="wire-gp4" aria-label="Pico GP4 bus request wire continuity checked"> | BUSREQ# | Pico GP4 - header pin 6 | U4 AHCT244 input - pin 4 |
| <input type="checkbox" data-checklist-id="wire-gp5" aria-label="Pico GP5 boot read disable wire continuity checked"> | BOOT_READ_DISABLE | Pico GP5 - header pin 7 | U4 AHCT244 input - pin 6 |
| <input type="checkbox" data-checklist-id="wire-gp6" aria-label="Pico GP6 upward enable request wire continuity checked"> | PICO_DATA_UP_OE# | Pico GP6 - header pin 9 | U3 HCT32 input - pin 10 |
| <input type="checkbox" data-checklist-id="wire-gp7" aria-label="Pico GP7 downward enable wire continuity checked"> | DATA_DOWN_OE# | Pico GP7 - header pin 10 | U10 LVC245 OE# - pin 19 |
| <input type="checkbox" data-checklist-id="wire-gp9" aria-label="Pico GP9 IO release wire continuity checked"> | IO_RELEASE | Pico GP9 - header pin 12 | U3 HCT32 input - pin 5 |

Wire all Pico connections from the table above with other active devices
absent. Continuity-check every destination socket contact and adjacent pins.
Tick each row after checking its connection; progress is saved in the Phase 1
checklist.

## Phase 1 Firmware

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Phase 1 firmware built and loaded"> Build `z80_stage01_supervisor`; load
`build/src/stage01_supervisor/z80_stage01_supervisor.uf2` using the
[firmware/console procedure](../system/firmware-build.md#vs-code-deployment-options).
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c).

| Done | Command | Behavior | Measurement |
| --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="sample-inputs" aria-label="Phase 1 input sampling recorded"> | `s` | Sample input/data levels | Monitor inputs may float with U7 absent |
| <input type="checkbox" data-checklist-id="walking-controls" aria-label="All seven walking control outputs measured"> | `w` | Walk seven control outputs, 250 ms each | Scope the correct empty socket contact and neighbors |
| <input type="checkbox" data-checklist-id="held-reset" aria-label="Held reset levels measured"> | `r` | Hold RESET# alternately HIGH/LOW | GP3/header 5 and Z80 socket 26: 3.20-3.40 V / near zero |
| <input type="checkbox" data-checklist-id="held-data" aria-label="All eight held data outputs measured"> | `d` | Hold one data GPIO HIGH; advance D0-D7 | Probe U9/U10 A pins 2-9 |
| <input type="checkbox" data-checklist-id="restore-defaults" aria-label="Phase 1 defaults restored and checked"> | `x` | Restore defaults and data inputs | Both OE requests HIGH; RESET LOW |

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

??? example "Phase 1 application - src/stage01_supervisor/main.c"

    ```c
    {% include "../../../../src/stage01_supervisor/main.c" %}
    ```

??? example "Shared startup and isolation - src/common/supervisor.c"

    ```c
    {% include "../../../../src/common/supervisor.c" %}
    ```
