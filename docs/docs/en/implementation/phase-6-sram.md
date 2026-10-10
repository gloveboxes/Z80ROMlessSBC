# Phase 6 - Z80-Assisted SRAM Loading

<div data-checklist="phase-6" data-checklist-label="Phase 6" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 6 prerequisite passed"> **Prerequisite:** [Phase 5](phase-5-data-bus.md#pass-gate).
**Install:** Z80 U1 and SRAM U2 together, after all socket wiring is checked.
The CPU is now required for RAM access; RESET is released during injection.

## Wiring - SRAM Socket

<input type="checkbox" data-checklist-id="sram-wiring" aria-label="Every SRAM address data and control pin wired and checked"> Complete every [address/data/fixed pin](../hardware/pin-mapping.md), including
CE# U2.22 from U4.12, WE# U2.29 from U4.9, OE# U2.24 from U3.3, CE2 to +5 V,
and A16 to GND.

<input type="checkbox" data-checklist-id="cpu-wiring" aria-label="Every Z80 control and monitor connection checked"> Complete Z80 CLK/RESET/BUSREQ/WAIT and all control monitor taps.
INT/NMI have pulls to +5 V; M1 goes to TP1; HALT/RFSH outputs remain open.
<input type="checkbox" data-checklist-id="cpu-sram-install" aria-label="CPU and SRAM orientation power and bypasses checked before installation"> Verify pin-1 orientation, power pins, and local bypasses before insertion.

## Firmware and Loader Qualification

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 6 firmware built and loaded"> Build `z80_stage06_sram_loader`; UF2:
`build/src/stage06_sram_dma/z80_stage06_sram_loader.uf2`.
[Application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/main.c)
and [loader protocol](../hardware/address-interface.md#loader-sequence).

First capture slow reset exit and injected `21/36/7E` sequences with the
analyzer, alongside scope measurements of CLK and SRAM controls. Confirm:

- <input type="checkbox" data-checklist-id="reset-exit-capture" aria-label="Reset and first opcode alignment captured"> Six reset clocks and two reset-exit clocks align the first opcode fetch.
- <input type="checkbox" data-checklist-id="opcode-operand-capture" aria-label="Opcode and operand T-state counts captured"> Opcode fetches have four T-states; operand reads have three.
- <input type="checkbox" data-checklist-id="slow-clock-data-hold" aria-label="Slow clock settling and injected data hold measured"> Each slow clock leaves CLK LOW for at least 1 us before returning. Confirm
  injected data remains valid through RD# release before firmware isolates it.
- <input type="checkbox" data-checklist-id="boot-inhibit-capture" aria-label="SRAM output inhibit captured throughout injection"> SRAM OE stays HIGH throughout injected bytes.
- <input type="checkbox" data-checklist-id="write-isolation-capture" aria-label="Upward drive disabled before CPU writes"> Upward data is disabled before the CPU's memory-write cycle.
- <input type="checkbox" data-checklist-id="readback-isolation-capture" aria-label="Exclusive SRAM readback drive captured"> Readback enables SRAM only after the Pico stops driving.
- <input type="checkbox" data-checklist-id="translator-settling-capture" aria-label="Translator high impedance and settling measured"> After disabling both translators, allow at least 1 us before changing Pico
  data pins to outputs; verify U10 is actually high-impedance at that point.
- <input type="checkbox" data-checklist-id="sram-timing-capture" aria-label="SRAM write and read timings meet exact datasheets"> SRAM write pulse, address/data setup/hold, and read sample position meet
  the exact Z80 and SRAM datasheets. Host tests are not this evidence.

`p` runs the full 64 KiB address-derived pattern and readback; `c` the
complement; `m` a full March test. Each command initializes a fresh injected
session and finishes with RESET asserted and the clock stopped. Save the
first mismatch address/expected/actual; do not advance after an error.
Start with the scope-connected current-limited supply and check loaded rail
voltage and clock thresholds before long tests.

| Done | Command | Required result |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="loaded-power-clock" aria-label="Loaded rails and clock thresholds measured before long RAM tests"> | Before long tests | Loaded rail voltage and clock thresholds checked |
| <input type="checkbox" data-checklist-id="ram-pattern" aria-label="Full 64 KiB address-derived pattern passed repeatedly"> | `p` | Full 64 KiB address-derived pattern and readback pass repeatedly |
| <input type="checkbox" data-checklist-id="ram-complement" aria-label="Full 64 KiB complement pattern passed repeatedly"> | `c` | Complement pattern and readback pass repeatedly |
| <input type="checkbox" data-checklist-id="ram-march" aria-label="Full 64 KiB March test passed repeatedly"> | `m` | Full March test passes repeatedly |

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-loader" aria-label="Phase 6 loader phase verified on real hardware"> Loader phase verified on real hardware.
- <input type="checkbox" data-checklist-id="gate-ram" aria-label="Phase 6 all three RAM tests and boundary addresses passed"> All three RAM tests pass repeatedly, including addresses 0000/FFFF.
- <input type="checkbox" data-checklist-id="gate-exclusive-drive" aria-label="Phase 6 no overlapping data drive measured"> No overlapping data drive occurs.
- <input type="checkbox" data-checklist-id="gate-evidence" aria-label="Phase 6 captures and cold-start results saved"> Save captures and cold-start results.

No normal-run MHz claim follows from slow injected tests alone.

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Stage 6 application - src/stage06_sram_dma/main.c"

    ```c
    {% include "../../../../src/stage06_sram_dma/main.c" %}
    ```

??? example "Shared assisted loader and RAM tests - src/common/sram.c"

    ```c
    {% include "../../../../src/common/sram.c" %}
    ```
