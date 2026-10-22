# Validation Workflows

Run focused checks first, then broaden according to impact. Use terminal CMake; the VS Code CMake Tools extension is unreliable in this repository.

## Firmware and CP/M Build

Prerequisites include CMake, Ninja, Python 3, `picotool`, `z80asm`, and a complete Arm GNU bare-metal toolchain with Newlib.

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
git submodule update --init --recursive
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico2_w
cmake --build build --target z80_cpm_images -j
```

Phase 10 provisions `WIFI_SSID`, `WIFI_PASSWORD` and `FS_SERVER` over USB
with Ctrl-] n and saves them in ENV; do not pass credentials to CMake.
Without settings, CP/M continues over USB. Never expose real credentials
in logs or commits. `FS_SERVER` is stored only, with no remote-FS controller.

Phase 10 weather reads `OWM_KEY`, `OWM_LOCATION` and optional `OWM_UNITS`
from ENV. Do not compile keys into firmware. Native `weather_*` and
`extended_io_services` tests cover its cache, bounded HTTP decoding, async
TLS lifecycle and real service transactions without credentials or hardware.
Live weather needs Wi-Fi, SNTP and valid OWM settings; distinguish that
acceptance check from successful firmware builds and fake-transport tests.
Configure native tests with `-DPICO_SDK_PATH=/path/to/pico-sdk` to include
`weather_real_tls_config`. It uses the actual firmware crypto configuration
and CA, not fake TLS APIs. Keep SHA-384 enabled for the USERTrust RSA root;
Mbed TLS 3's SHA-512 option does not imply SHA-384 support.

Run host image tests from the repository root with discovery:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

If invoking `test_build_images` directly instead of discovery, change into `src/cpm` first because it imports `build_images` as a top-level module.

## Flashing Tools

Use the existing `.venv-flash` environment, or create it and install
`scripts/requirements-flash.txt` if missing. Run the focused host tests:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python -m unittest discover -s scripts -p 'test_pico*.py' -v &&
python -m unittest discover -s src/disks -p 'test_*.py' -v
```

Repeat activation/preflight in each fresh shell; an earlier tool call's
activation does not persist. For a launch-only check use
`python scripts/pico_flash.py --help` after that same preflight. Do not flash
or run unit tests merely to work around a failed dependency import.

These tests cover serial-number discovery and changed port names, targeted
picocom disconnection, exclusive serial settings, status/boot ordering,
stop/flush timeouts, exact UF2 firmware boundaries, journal headers, no-backup
selected-slot disk writes, real Altair conversion, installed Drive A warm-boot
compatibility and picotool failure handling. macOS `lsof` can include file-descriptor records
and return exit 1 with valid owners when only one device alias matches;
picotool force options must follow serial selection. Both cases have
regression coverage. The tests do not stop, reset or flash real hardware.
Do not run a physical flash merely to validate a tooling change. For an
authorized live check, `pico_supervisor.py status` and `help` are read-only
supervisor queries and refuse an occupied port.

## Hardware and KiCad

Regenerate the native schematic, exports, strict ERC report, and netlist comparison:

```sh
python3 -m venv .venv-kicad
.venv-kicad/bin/pip install -r scripts/requirements-kicad.txt
PYTHON="$PWD/.venv-kicad/bin/python" npm run kicad
```

The pipeline also runs `scripts/check-doc-interconnects.py`. It resolves every
pairwise Mermaid endpoint to its schematic reference and pin, requires both
ends of each documented wire to share a KiCad net, and checks fixed power,
enable, direction, address-select, and no-connect pins.

For any pin, part, package, resistor, capacitor, or layout change, also verify:

- BOM fitted/purchase counts
- all pin mappings and active levels against exact datasheets
- GPIO uniqueness and Pico physical-header mapping
- package widths, board row budgets, gaps, and pin-1 orientation
- generated breadboard SVG against prose
- KiCad ERC, net count, and endpoint count
- affected bring-up phases and pass gates

## Fixed Logic and Assisted Loader

- No PLD/JEDEC/programmer is used. Check the exact HCT32 OR-gate pinout.
- Run `PYTHON=.venv-kicad/bin/python npm run kicad`: schematic-only ERC,
  netlist parity, documented connections and schematic exports.
- Do not run the historical `--full` pipeline or regenerate PCB/project,
  DSN/SES, routing, fabrication, or PCB renders while migration is deferred.
- Run `npm run test:control` for injection, data-path and trap regression tests.
- Physically measure reset-exit phase, injected-read data setup, SRAM inhibit,
  stepped I/O completion, both actual OEs, and resumed PWM before qualification.
- Scope evidence's single-pause evaluator is inconclusive for stepped release;
  raw captures and manual timing review are required.

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
- Repeated cold boots, assisted-load verification, I/O tests, and fault-injection results
  required by the implementation phase pages.

Separate theoretical calculations, simulated/host-tested behavior, and physical bench measurements in every report.
