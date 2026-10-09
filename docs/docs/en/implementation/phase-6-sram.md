# Phase 6 - Z80-Assisted SRAM Loading

**Prerequisite:** [Phase 5](phase-5-data-bus.md#pass-gate).
**Install:** Z80 U1 and SRAM U2 together, after all socket wiring is checked.
The CPU is now required for RAM access; RESET is released during injection.

## Wiring - SRAM Socket

Complete every [address/data/fixed pin](../hardware/pin-mapping.md), including
CE# U2.22 from U4.12, WE# U2.29 from U4.9, OE# U2.24 from U3.3, CE2 to +5 V,
and A16 to GND. Complete Z80 CLK/RESET/BUSREQ/WAIT and all control monitor taps.
INT/NMI have pulls to +5 V; M1 goes to TP1; HALT/RFSH outputs remain open.
Verify pin-1 orientation, power pins, and local bypasses before insertion.

## Firmware and Loader Qualification

Build `z80_stage06_sram_loader`; UF2:
`build/src/stage06_sram_dma/z80_stage06_sram_loader.uf2`.
[Application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/main.c)
and [loader protocol](../hardware/address-interface.md#loader-sequence).

First capture slow reset exit and injected `21/36/7E` sequences with the
analyzer, alongside scope measurements of CLK and SRAM controls. Confirm:

- Six reset clocks and two reset-exit clocks align the first opcode fetch.
- Opcode fetches have four T-states; operand reads have three.
- SRAM OE stays HIGH throughout injected bytes.
- Upward data is disabled before the CPU's memory-write cycle.
- Readback enables SRAM only after the Pico stops driving.
- SRAM write pulse, address/data setup/hold, and read sample position meet
  the exact Z80 and SRAM datasheets. Host tests are not this evidence.

`p` runs the full 64 KiB address-derived pattern and readback; `c` the
complement; `m` a full March test. Each command initializes a fresh injected
session and finishes with RESET asserted and the clock stopped. Save the
first mismatch address/expected/actual; do not advance after an error.
Start with the scope-connected current-limited supply and check loaded rail
voltage and clock thresholds before long tests.

## Pass Gate

Loader phase verified on real hardware; all three RAM tests pass repeatedly,
including addresses 0000/FFFF, and no overlapping data drive occurs. Save
captures and cold-start results. No normal-run MHz claim follows from slow
injected tests alone.
