# Validation Workflows

Run focused checks first, then broaden according to impact. Use terminal CMake; the VS Code CMake Tools extension is unreliable in this repository.

## Firmware and CP/M Build

Prerequisites include CMake, Ninja, Python 3, `picotool`, `z80asm`, and a complete Arm GNU bare-metal toolchain with Newlib.

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico2_w \
  -DZ80_WIFI_SSID='your-network' \
  -DZ80_WIFI_PASSWORD='your-password'
cmake --build build --target z80_cpm_images -j
```

Use empty Wi-Fi credentials when network access is unnecessary. Never expose real credentials in logs or commits.

Run host image tests from the repository root with discovery:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

If invoking `test_build_images` directly instead of discovery, change into `src/cpm` first because it imports `build_images` as a top-level module.

## Hardware and KiCad

Regenerate the native schematic, exports, strict ERC report, and netlist comparison:

```sh
python3 -m venv .venv-kicad
.venv-kicad/bin/pip install -r scripts/requirements-kicad.txt
PYTHON="$PWD/.venv-kicad/bin/python" npm run kicad
```

For any pin, part, package, resistor, capacitor, or layout change, also verify:

- BOM fitted/purchase counts
- all pin mappings and active levels against exact datasheets
- GPIO uniqueness and Pico physical-header mapping
- package widths, board row budgets, gaps, and pin-1 orientation
- generated breadboard SVG against prose
- KiCad ERC, net count, and endpoint count
- affected bring-up phases and pass gates

## PLD

The current source is `src/pld/sram_control.pld`. After equation changes:

- Exhaustively compare steady-state truth tables.
- Check consensus/hazard behavior during RESET#/BUSACK# ownership changes.
- Confirm product-term capacity for the selected ATF22V10 variant.
- Compile a JEDEC file with the project's supported PLD toolchain.
- Program, read back, and verify using the exact device algorithm before installation.

## Documentation

```sh
npm run layout
docs/docs/.venv/bin/mkdocs build --strict -f docs/docs/mkdocs.yml
git diff --check HEAD -- README.md docs .github/workflows/docs.yml
```

`npm run layout` regenerates
`docs/docs/en/images/breadboard-layout.svg`. The pinout images live beside it.
Inspect representative generated pages for broken tables, Mermaid failures, and
poor heading placement after structural changes.

A strict build validates Markdown and links but does not execute JavaScript.
After changing Mermaid source, `mermaid.mjs`, CSS, navigation, or the theme,
also use a browser at desktop and 390 px mobile widths to verify:

- every `pre.z80-mermaid` becomes one populated `.mermaid svg`
- the expected diagram count renders on every affected page
- SVG and table containers do not cause page-level horizontal overflow
- labels remain readable and client-side navigation reruns the Mermaid adapter

## Documentation C Excerpts

The phase-specific C excerpts are explanatory and cumulative. When editing them:

- Keep maintained-source links adjacent.
- Validate Markdown fence balance.
- Keep each page internally coherent; do not imply that a snippet is the
  canonical maintained implementation.
- Prefer validating the maintained source and stage builds over trusting hand-written SDK stubs; stubs can miss incorrect or absent real SDK includes.

## Qualification Evidence

Do not report a clock rate as qualified without captures and error-free tests at that rate. Preserve:

- DHO814 analog captures using compensated 10x probes and short ground springs.
- DSLogic Plus Group A-D captures using 16-channel, 100 MHz Buffer Mode,
  threshold appropriate to each group, Filter=None, and RLE disabled.
- SRAM setup/write-pulse evidence and ownership-transition evidence.
- Repeated cold boots, DMA verification, I/O tests, and fault-injection results
  required by the implementation phase pages.

Separate theoretical calculations, simulated/host-tested behavior, and physical bench measurements in every report.
