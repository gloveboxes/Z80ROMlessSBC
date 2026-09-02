---
name: z80-romless-sbc
description: 'Engineer, review, debug, build, test, or document the Z80ROMlessSBC project. Use for Pico 2 W supervisor firmware, Z80/SRAM/GAL/MCP23S17 buses, voltage translation, breadboard placement, CP/M images, onboard-flash storage, staged bring-up, KiCad checks, MkDocs maintenance, DSLogic Plus/DHO814 captures, timing qualification, or BOM and pin-map changes.'
argument-hint: 'Describe the hardware, firmware, documentation, or validation task'
user-invocable: true
---

# Z80 ROMless SBC Engineering

Use this workflow to preserve the project's electrical safety, firmware ownership rules, build reproducibility, and documentation consistency.

## Start With Current Sources

1. Treat `docs/docs/en/` as the engineering and bring-up specification;
   `README.md` is only the concise project entry point.
2. Treat maintained files under `src/` and `hardware/kicad/` as authoritative
   implementation sources. MkDocs C excerpts explain invariants but are not
   canonical code.
3. Load [architecture invariants](./references/architecture.md) before changing pins, parts, buses, power, flash, multicore ownership, or timing.
4. Load [validation workflows](./references/validation.md) before building, testing, regenerating artifacts, or declaring a change complete.
5. Load [documentation lessons](./references/documentation.md) before
   reorganizing MkDocs pages/navigation, editing diagrams, changing
   terminology, or moving documentation assets.

## Procedure

1. Identify the owning surface: electrical design, firmware module, CP/M tooling, staged application, KiCad source, or documentation generator.
2. Read the nearest implementation and its corresponding page under
   `docs/docs/en/implementation/` before editing.
3. State one local hypothesis and the cheapest check that can falsify it.
4. Make the smallest change that preserves the invariants in the references.
5. Run a focused validation immediately after the first substantive edit.
6. Expand validation according to blast radius:
   - Pin, logic, power, or package changes require schematic, BOM, placement, phase-plan, and documentation consistency checks.
   - Shared firmware changes require affected stage builds and later cumulative stages.
   - Flash or CP/M changes require host image tests, exact geometry checks, and final artifact generation.
   - Documentation changes require a strict MkDocs build from `docs/docs`.
7. Report measured or tested behavior separately from theoretical limits. Never promote the 20 MHz CPU rating to a system claim.

## Project-Specific Guardrails

- Do not revive superseded SD-card, PSRAM, LVC8T245-carrier, or 74HCT157/74HCT08 architectures without an explicit redesign request.
- Do not connect a 5 V output directly to a Pico GPIO.
- Do not bypass the AHCT244 on GAL-to-SRAM controls.
- Do not share SPI0 across cores or introduce a second bus master on SCK/MOSI.
- Do not treat a physically installed but unpowered 5 V IC as equivalent to an absent socketed device.
- Bond the DHO814 chassis to protective earth before connecting any input or
   output lead; ordinary passive probes are not isolated.
- Treat DSLogic Plus grounds as USB-host referenced. Connect them only to
   common circuit GND, and never attach CK or TI to a 5 V signal.
- Use the documented DSLogic Group A-D captures. Qualification requires
   16-channel, 100 MHz Buffer Mode; 16-channel Stream Mode is limited to 20 MHz,
   and no 16-channel capture proves complete address, complete data, and all
   controls simultaneously.
- Do not trust package suffixes, TO-92 lead order, breadboard row geometry, or active-low polarity from memory. Verify the exact datasheet and current placement table.
- Do not use CMake Tools extension failures as evidence that the project cannot build; use the terminal workflow in the validation reference.
