# Phase 7 - Z80 Execution and Bus Grants

**Prerequisite:** [Phase 6](phase-6-sram.md#pass-gate).
**Install:** No additional chips; CPU was fitted in Phase 6.

Build `z80_stage07_z80_cpu`; UF2 lives in `build/src/stage07_z80_cpu/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage07_z80_cpu/main.c).
The test image is `NOP; NOP; JP 0000`. It is injected, read back, then reset
again before execution from SRAM.

| Command | Test |
| --- | --- |
| `l` | Load/verify for stepping; hold RESET afterward |
| `s` | Release reset on first step, clock one 10 Hz-equivalent cycle |
| `0/1/2/3` | Reload and run at 10 Hz / 1 kHz / 100 kHz / 1 MHz |
| `r` | Reload/run 1 MHz |
| `q` | BUSREQ/BUSACK round-trip; no Pico memory drive |
| `z` | Reset/restart current run rate |
| `x` | Assert reset, disable paths, stop clock |

## Test Plan

Capture reset, first fetch at 0000, M1, CE/OE/WE and data. Both Pico data
enables stay HIGH during SRAM execution and boot inhibit stays LOW. Verify
normal fetches differ from the injected boot stream; observe the NOP/JP loop.
Measure the loaded clock at Z80 pin 6. Progress through slow rates before
1 MHz. `q` must float the CPU bus while BUSACK LOW without activating either
Pico path; rearm I/O before releasing a grant in integrated firmware.

Test a missing BUSACK using a test-only isolated input fixture, not by
shorting the CPU output. Timeout must not authorize memory access; release
timeout must assert RESET and isolate. Power off before fault wiring changes.

## Pass Gate

Verified reset-to-SRAM fetch, repeated cold loads, correct instruction loop,
and safe bus-grant timeout behavior. Higher rates need the separate
[qualification plan](frequency-qualification.md).
