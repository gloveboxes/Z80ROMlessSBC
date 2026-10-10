# Phase 2 - Output Buffer, Fixed Logic, and Clock

<div data-checklist="phase-2" data-checklist-label="Phase 2" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

## Phase Setup Checklist

| Done | Requirement |
| --- | --- |
| <input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 2 prerequisite passed"> | [Phase 1 pass gate](phase-1-supervisor.md#pass-gate) completed |
| <input type="checkbox" data-checklist-id="later-devices-absent" aria-label="CPU SRAM and data translators absent for Phase 2"> | CPU, SRAM and data translators remain absent |
| <input type="checkbox" data-checklist-id="install-u4" aria-label="U4 fitted after wiring checks passed with power disconnected"> | U4 SN74AHCT244N fitted **after the wiring checks below pass**, with external power and USB disconnected |
| <input type="checkbox" data-checklist-id="install-u3" aria-label="U3 fitted after wiring checks passed with power disconnected"> | U3 SN74HCT32N fitted **after the wiring checks below pass**, with external power and USB disconnected |

## Wiring - Output Buffer and Fixed Logic

Disconnect external power and USB. Keep U3/U4 out of their sockets while
wiring and checking the tables below. CPU, SRAM and later-phase devices
also remain absent. Previously fitted Phase 0/1 connections need checking,
not a second wire. The [buffer and gate reference](../hardware/output-buffer.md)
explains the channel functions.

{%
  include-markdown "../hardware/output-buffer.md"
  start='<template id="phase-2-output-buffer-wiring">'
  end="phase-2-output-buffer-wiring-end</template>"
%}

### Final checks before inserting U3 and U4

| Done | Check |
| --- | --- |
| <input type="checkbox" data-checklist-id="buffer-gate-wiring" aria-label="Buffer and gate channels and supplies wired"> | All listed signal and supply connections complete; U4 pin 18 to Z80 socket pin 6 stays short and entirely on Core |
| <input type="checkbox" data-checklist-id="fixed-pins" aria-label="Buffer and gate fixed pins checked"> | Both U4 OEs grounded, all unused inputs grounded, unused outputs left open |
| <input type="checkbox" data-checklist-id="continuity-install" aria-label="Buffer and gate continuity checked before installation"> | Continuity checked at every actual socket contact, no neighboring shorts, correct supply rails and pin-1 orientation verified before inserting either device |

## Firmware and Tests

Keep the CPU, SRAM and data translators absent throughout this phase.

| Done | Test | Procedure and pass condition |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 2 firmware built and loaded"> | Firmware | Build `z80_stage02_buffers_clock` and load its UF2 from `build/src/stage02_buffers_clock/`. See the [maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/main.c) |
| <input type="checkbox" data-checklist-id="held-buffer-levels" aria-label="All three buffer input and output level pairs measured"> | Buffer levels | Press `d` for each LOW/HIGH step and probe U4 pairs 2/18, 4/16 and 6/14. Inputs are near 0/3.3 V; outputs are near 0/+5 V, LOW below 0.3 V and HIGH at least 4.4 V. The seventh step or `x` restores defaults |
| <input type="checkbox" data-checklist-id="clock-waveforms" aria-label="All three unloaded clock rates qualified"> | Clock | Use `1`, `2`, `3` for 1 kHz, 100 kHz and 1 MHz. At U4 pin 18 and Z80 socket pin 6 verify frequency, 45-55% duty, and no runt pulses or threshold-crossing ringing. HIGH must reach measured CPU VCC minus 0.5 V: at 5.0 V, at least 4.5 V |
| <input type="checkbox" data-checklist-id="inhibit-wait" aria-label="SRAM inhibit and WAIT behavior measured"> | Inhibit and WAIT | Drive raw RD# and IORQ# LOW one at a time through 1 kOhm. SRAM OE stays HIGH while boot inhibit is HIGH; WAIT is LOW only while IORQ and IO_RELEASE are both LOW |

### OR-gate truth-table fixture

For every gate, test LOW/LOW, LOW/HIGH, HIGH/LOW and HIGH/HIGH. The expected
outputs are LOW, HIGH, HIGH and HIGH respectively.

Use current-limited fixture outputs referenced to common circuit GND and
limited to 0 V through measured U3 VCC. Disconnect external power and USB
before every fixture change, and define both inputs before powering up.
Never drive a connected Pico GPIO or another active output.

| Done | Step |
| --- | --- |
| <input type="checkbox" data-checklist-id="gate-fixture-ready" aria-label="OR gate fixture configured safely"> | Set the fixture ground, voltage range and current limit; keep Stage 2 loaded and do not use the Stage 1 walking test |
| <input type="checkbox" data-checklist-id="gate-1-input-1-isolated" aria-label="U4 output 7 disconnected from U3 input 1"> | Gate 1: disconnect U4 output 7 from U3 input 1 |
| <input type="checkbox" data-checklist-id="gate-1-input-2-isolated" aria-label="U4 output 14 disconnected from U3 input 2"> | Gate 1: disconnect U4 output 14 from U3 input 2 |
| <input type="checkbox" data-checklist-id="gate-1-input-1-fixture" aria-label="Fixture output A connected to U3 input 1"> | Gate 1: attach fixture output A to U3 input 1 |
| <input type="checkbox" data-checklist-id="gate-1-input-2-fixture" aria-label="Fixture output B connected to U3 input 2"> | Gate 1: attach fixture output B to U3 input 2 |
| <input type="checkbox" data-checklist-id="gate-1-truth-table" aria-label="All four U3 gate 1 combinations measured"> | Gate 1: test all four combinations at output 3, SRAM OE# |
| <input type="checkbox" data-checklist-id="gate-2-truth-table" aria-label="U3 gate 2 isolated and all four combinations measured"> | Gate 2: disconnect Pico GP9/header 12 from input 5, attach fixture outputs to inputs 4/5, then test all four combinations at output 6, WAIT#. CPU-absent IORQ# at input 4 has no active driver |
| <input type="checkbox" data-checklist-id="gate-3-truth-table" aria-label="U3 gate 3 isolated and all four combinations measured"> | Gate 3: disconnect Pico GP6/header 9 from input 10, attach fixture outputs to inputs 9/10, then test all four combinations at output 8, DATA_UP_OE#. CPU-absent RD# at input 9 has no active driver |
| <input type="checkbox" data-checklist-id="gate-4-fixed" aria-label="Unused U3 gate remains correctly fixed"> | Unused gate: leave inputs 12/13 grounded and output 11 unconnected |

#### Restore normal wiring

| Done | Step |
| --- | --- |
| <input type="checkbox" data-checklist-id="gate-fixture-sources-removed" aria-label="OR gate fixture removed with power disconnected"> | Disconnect external power and USB, then remove the fixture outputs and ground |
| <input type="checkbox" data-checklist-id="gate-fixture-u4-restored" aria-label="U4 to U3 driver jumpers restored"> | Reconnect U4 output 7 to U3 input 1 and U4 output 14 to U3 input 2 |
| <input type="checkbox" data-checklist-id="gate-fixture-pico-restored" aria-label="Pico to U3 driver jumpers restored"> | Reconnect Pico GP9/header 12 to U3 input 5 and Pico GP6/header 9 to U3 input 10 |
| <input type="checkbox" data-checklist-id="fixture-removed" aria-label="Phase 2 temporary wiring restored and defaults selected"> | Check continuity and adjacent-pin isolation, power up, then press `x` to restore defaults |

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-buffers" aria-label="Phase 2 buffer and gate measurements passed"> Buffer tests and all three used-gate truth tables passed; unused gate inputs grounded and output open.
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
