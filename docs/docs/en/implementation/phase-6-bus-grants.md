# Phase 6 - CPU Reset, Fetch and Bus Grants

## VS Code tasks

Choose **Tasks: Run Task** in the Z80ROMlessSBC workspace.

| Diagnostic | Build task | Deployment task |
| --- | --- | --- |
| Phase 6 bus grants | **Build Pico - Phase 6 bus grants** | **Flash Pico - Phase 6 bus grants** |

Deployment tasks force-update without checking the previous firmware phase.
Complete this phase's hardware prerequisites and save work before flashing. See
[Pico tools](../tools/pico-tools.md#deployment-task-requirements).


<div data-checklist="phase-6-bus-grants" data-checklist-label="Phase 6 bus grants" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 6 loader and RAM prerequisites passed"> **Prerequisite:** Complete the loader and RAM checks in [Phase 6](phase-6-sram.md#full-memory-loader-qualification).
**Install:** No additional chips; CPU was fitted in Phase 6.

This diagnostic retains the unique bus-grant and receiving-pin measurement
checks formerly in the separate Phase 7. Phase 6's arithmetic and SRAM tests
already provide much stronger execution coverage than this small loop, but
they do not request/release the CPU bus or prove analogue reset/clock margins.
There is no longer a separate Phase 7 page or firmware target; Phase 8-10
keep their existing numbers.

Do not flash this diagnostic during another running test. It is a separate,
manual low-rate fixture, not a new mode in the arithmetic soak.

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Phase 6 bus-grant firmware built and loaded"> Build `z80_stage06_bus_grant`; UF2 lives in `build/src/stage06_sram_dma/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/bus_grant_test.c).
The test image is `NOP; NOP; JP 0000`. It is injected, read back, then reset
again before execution from SRAM.

| Done | Command | Test |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="load-step-image" aria-label="Stepping image loaded verified and held in reset"> | `l` | Load/verify for stepping; hold RESET afterward |
| <input type="checkbox" data-checklist-id="manual-step" aria-label="Manual reset release and slow steps measured"> | `s` | Release reset on first step, clock one manual cycle with 50 ms minimum half-pulses |
| <input type="checkbox" data-checklist-id="run-rates" aria-label="All four bus-grant fixture run rates checked in order"> | `0/1/2/3` | Reload and run at 10 Hz / 1 kHz / 100 kHz / 1 MHz |
| <input type="checkbox" data-checklist-id="run-1mhz" aria-label="Bus-grant fixture reload and 1 MHz run checked"> | `r` | Reload/run 1 MHz |
| <input type="checkbox" data-checklist-id="bus-grant" aria-label="Bus grant round-trip measured without Pico memory drive"> | `q` | BUSREQ/BUSACK round-trip; no Pico memory drive |
| <input type="checkbox" data-checklist-id="restart" aria-label="Current run rate reset and restart checked"> | `z` | Reset/restart current run rate |
| <input type="checkbox" data-checklist-id="isolate-stop" aria-label="Reset data isolation and clock stop checked"> | `x` | Assert reset, disable paths, stop clock |

Commands are single characters; Enter is not required. `q` requires a
free-running mode, not manual stepping. A request or release timeout fails
closed and clears the run state: start a fresh run before retrying.
The diagnostic does not enable an I/O trap; its Z80 loop contains no port I/O.
On a successful grant, the Pico never takes over the memory address/data bus.

Build from the repository root with the
[firmware toolchain](../system/firmware-build.md):

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build --target z80_stage06_bus_grant
```

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

- <input type="checkbox" data-checklist-id="gate-sram-fetch" aria-label="Phase 6 reset to SRAM fetch passed"> Verified reset-to-SRAM fetch.
- <input type="checkbox" data-checklist-id="gate-cold-loads" aria-label="Phase 6 repeated cold loads passed"> Repeated cold loads.
- <input type="checkbox" data-checklist-id="gate-loop" aria-label="Phase 6 instruction loop passed"> Correct instruction loop.
- <input type="checkbox" data-checklist-id="gate-timeouts" aria-label="Phase 6 bus grant timeouts fail safely"> Safe bus-grant timeout behavior.

Higher rates need the separate
[qualification plan](frequency-qualification.md).

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Phase 6 bus-grant diagnostic - src/stage06_sram_dma/bus_grant_test.c"

    ```c
    {% include "../../../../src/stage06_sram_dma/bus_grant_test.c" %}
    ```

??? example "Shared CPU reset and bus grants - src/common/cpu.c"

    ```c
    {% include "../../../../src/common/cpu.c" %}
    ```
