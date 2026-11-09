# Agent Instructions

Z80ROMlessSBC is a ROMless Z80 computer supervised by a Raspberry Pi Pico 2 W.
The repository includes firmware, CP/M applications/images, hardware and documentation.

## Standing Context

- [README.md](./README.md) is the concise entry point;
  [docs/docs/en/](./docs/docs/en/) is the engineering and bring-up specification.
- Maintained implementation lives in [src/](./src/), [examples/](./examples/),
  [scripts/](./scripts/) and [hardware/](./hardware/).
  Documentation code excerpts explain invariants; they are not canonical source.
- Change generators rather than hand-editing generated artifacts. Use maintained
  build, validation and deployment tools instead of ad hoc replacements.

## Build and Test

Run from the repository root. Use terminal CMake: the VS Code CMake Tools
extension is unreliable here, so its failures are not build evidence.

- Firmware and CP/M images: `cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w`
  then `cmake --build build --target z80_cpm_images -j`
- CP/M host tests: `python3 -m unittest discover -s src/cpm -p 'test_*.py' -v`
- Z80 control/native tests: `npm run test:control`
- Z80 execution tests use the single C emulator (x80 plus two-bank SRAM):
  `cmake -S src/cpm/z80_debug_host -B build/cpm/z80_debug_host -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF`
  then build target `z80sbc-emulator`; run `python3.14 src/common/tests/<name>_z80_test.py`
  (Python 3.10+, `z80asm` on `PATH`)
- Every Z80 result depends on the emulator: run `emulator_z80_test.py` first and
  after any change to `x80.cxx`, `sbc_*.c*` or the bank model. Add a regression
  there for each emulator bug, checked against an independent reference.
- KiCad DIP/SMT: `PYTHON=.venv-kicad/bin/python npm run kicad` / `npm run kicad:smt`
- Docs: `docs/docs/.venv/bin/mkdocs build --strict -f docs/docs/mkdocs.yml`

## Hardware Safety

- Never connect a 5 V output to a Pico GPIO; buffer or translate every 5 V input.
- Apply external +5 V before USB and remove USB first. Never tie external +5 V
  to `VBUS`; an installed unpowered 5 V IC is not an absent socketed device.
- Pico never masters memory buses; changing the current architecture requires
  an explicit redesign request.
- Verify pinouts, package suffixes and active-low polarity against the exact
  datasheet, not memory.

## Working Rules

- Preserve unrelated worktree changes. Keep changes scoped and update directly
  related documentation and generated outputs together.
- Run the smallest relevant checks, expanding with the change's impact.
  Report commands, results and any unverified behavior explicitly.
- Distinguish calculations, host/emulator tests and physical measurements.
  A successful build or ERC/DRC check does not qualify hardware or timing.
- Builds do not authorize flashing, resets, instrument changes, uploads or
  manufacturing orders. Obtain authorization before affecting external systems.
- Keep credentials out of source, logs and commits.
- Docs are concise and describe only the current design; use the `mkdocs-docs`
  skill when editing them.
- This project is greenfield with one user. Describe only the current design:
  no legacy designs, compatibility paths, migration notes or historical wording
  ("legacy", "superseded", "no longer", "formerly") in code, docs or skills.

## On-Demand Workflows

Use the matching skill package in [.github/skills/](./.github/skills/) and load
only the references needed for the task. Keep task-specific commands, safety
gates and procedures in that package's `SKILL.md` or linked references, not in
this file. `.claude/skills` and `.agents/skills` (Claude, Codex) are symlinks
to `.github/skills`; edit skills only there. On Windows, enable symlinks
(`git config core.symlinks true` with Developer Mode) before cloning.
