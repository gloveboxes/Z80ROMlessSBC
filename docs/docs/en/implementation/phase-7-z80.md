# Phase 7 - Z80 Execution and Bus Grants

<div data-checklist="phase-7" data-checklist-label="Phase 7" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 7 prerequisite passed"> **Prerequisite:** [Phase 6](phase-6-sram.md#pass-gate).
**Install:** No additional chips; CPU was fitted in Phase 6.

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 7 firmware built and loaded"> Build `z80_stage07_z80_cpu`; UF2 lives in `build/src/stage07_z80_cpu/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage07_z80_cpu/main.c).
The test image is `NOP; NOP; JP 0000`. It is injected, read back, then reset
again before execution from SRAM.

| Done | Command | Test |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="load-step-image" aria-label="Stepping image loaded verified and held in reset"> | `l` | Load/verify for stepping; hold RESET afterward |
| <input type="checkbox" data-checklist-id="manual-step" aria-label="Manual reset release and slow steps measured"> | `s` | Release reset on first step, clock one manual cycle with 50 ms minimum half-pulses |
| <input type="checkbox" data-checklist-id="run-rates" aria-label="All four Stage 7 run rates checked in order"> | `0/1/2/3` | Reload and run at 10 Hz / 1 kHz / 100 kHz / 1 MHz |
| <input type="checkbox" data-checklist-id="run-1mhz" aria-label="Stage 7 reload and 1 MHz run checked"> | `r` | Reload/run 1 MHz |
| <input type="checkbox" data-checklist-id="bus-grant" aria-label="Bus grant round-trip measured without Pico memory drive"> | `q` | BUSREQ/BUSACK round-trip; no Pico memory drive |
| <input type="checkbox" data-checklist-id="restart" aria-label="Current run rate reset and restart checked"> | `z` | Reset/restart current run rate |
| <input type="checkbox" data-checklist-id="isolate-stop" aria-label="Reset data isolation and clock stop checked"> | `x` | Assert reset, disable paths, stop clock |

## Test Plan

<input type="checkbox" data-checklist-id="sram-fetch-capture" aria-label="Reset first SRAM fetch and exclusive data drive captured"> Capture reset, first fetch at 0000, M1, CE/OE/WE and data. Both Pico data
enables stay HIGH during SRAM execution and boot inhibit stays LOW.

<input type="checkbox" data-checklist-id="reset-pwm-timing" aria-label="Reset release and first PWM edge timing measured"> Verify
RESET# rises while CLK is stopped LOW, with at least 1 us before PWM starts.
The first clock edge must meet the Z80 reset setup requirement.
<input type="checkbox" data-checklist-id="instruction-loop" aria-label="Normal SRAM NOP and JP loop observed"> Verify normal fetches differ from the injected boot stream; observe the NOP/JP loop.

<input type="checkbox" data-checklist-id="loaded-clock" aria-label="Loaded Z80 clock measured at each selected rate"> Measure the loaded clock at Z80 pin 6. Progress through slow rates before
1 MHz. `q` must float the CPU bus while BUSACK LOW without activating either
Pico path; rearm I/O before releasing a grant in integrated firmware.

<input type="checkbox" data-checklist-id="busack-faults" aria-label="Bus grant and release timeout fault responses verified"> Test a missing BUSACK using a test-only isolated input fixture, not by
shorting the CPU output. Timeout must not authorize memory access; release
timeout must assert RESET and isolate. Power off before fault wiring changes.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-sram-fetch" aria-label="Phase 7 reset to SRAM fetch passed"> Verified reset-to-SRAM fetch.
- <input type="checkbox" data-checklist-id="gate-cold-loads" aria-label="Phase 7 repeated cold loads passed"> Repeated cold loads.
- <input type="checkbox" data-checklist-id="gate-loop" aria-label="Phase 7 instruction loop passed"> Correct instruction loop.
- <input type="checkbox" data-checklist-id="gate-timeouts" aria-label="Phase 7 bus grant timeouts fail safely"> Safe bus-grant timeout behavior.

Higher rates need the separate
[qualification plan](frequency-qualification.md).

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Stage 7 application - src/stage07_z80_cpu/main.c"

    ```c
    {% include "../../../../src/stage07_z80_cpu/main.c" %}
    ```

??? example "Shared CPU reset and bus grants - src/common/cpu.c"

    ```c
    {% include "../../../../src/common/cpu.c" %}
    ```
