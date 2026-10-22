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

## Firmware Boundaries, Concurrency, and Performance

For changes to transport, queues, scheduling or callbacks, validate the owning
implementation rather than only its mocks. Preserve the existing core ownership
and disk-first service order. Host tests establish logic and work counts; they
do not establish real IRQ latency, GPIO timing or wireless throughput.

### Focused Native and SDK Checks

Use an actual SDK path when configuring native tests so the real Mbed TLS and
complete WebSocket transport regressions are enabled:

```sh
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/host-control \
  --target network_test network_no_wifi_test terminal_queue_test websocket_transport_test -j
ctest --test-dir build/host-control --output-on-failure \
  -R '^(terminal_connections|terminal_led_without_wifi|terminal_concurrent_rings|websocket_.*)$'
```

The path is a placeholder: use the configured local SDK, not an assumed directory.
Check test discovery/configuration output; a green run with optional transport
tests omitted is not equivalent to exercising the complete server.

- Queue changes: test full/empty bounds, order, concurrent producer/consumer
  progress, non-nested locking, and O(1) ownership handover without stale bytes.
- Protocol changes: test cumulative/wrapping ACKs, ACKs for never-enqueued data,
  pending-input admission on the next service pass, duplicate handling, reconnect,
  stale callbacks, heartbeat timeout, and USB fallback.
- Transport changes: count accepted `tcp_write` calls separately from output
  attempts. Verify a four-frame terminal burst uses four 265-byte frame writes
  and one flush, while single interactive/control messages flush promptly.
  Rejected writes must preserve ordering; failed flushes must not duplicate
  accepted writes. These sizes describe the current protocol, not universal
  WebSocket limits; update assertions if framing is intentionally changed.
- Receive/lifetime changes: test split/chained pbufs, empty frames, masking,
  fragmented/control interleaving, malformed input, message/queue bounds, fair
  dispatch budgets, PONG backpressure, callback reentrancy, close deadline wrap,
  slot reuse, disconnect/error races, destructor cleanup and HTTP final ACKs.
- Run the changed WebSocket lifetime/parser tests under AddressSanitizer and
  UndefinedBehaviorSanitizer in a separate native build when supported by the
  host compiler. Sanitizer success is not a substitute for actual SDK builds.
- Expand to the complete native suite for shared ownership or protocol changes,
  then build affected firmware stages and later cumulative stages. Verify the
  actual SDK/lwIP/CYW43 configuration, linker memory limits and stack allocation;
  hand-written fakes cannot prove hardware API or memory-region correctness.

### Measurement and Acceptance

Before optimisation, record a reproducible workload, clock/build/network setup,
the baseline, and an explicit acceptance target. Do not invent a latency target
or claim a measured speedup from fewer function calls alone.

Measure the relevant subset before and after under the same conditions:

| Surface | Evidence |
| --- | --- |
| Web terminal | Sustained bytes/second, key-to-echo latency distribution/tail, browser consumption-ACK delay, queue high-water marks and retries/timeouts |
| Core 1 scheduling | Maximum network-poll gap and per-service duration during idle, bulk terminal traffic, disk reads, cached writes, flash commits and weather/TLS work |
| Core 0/trap | Worst-case trap/IRQ duration, USB/supervisor responsiveness, no new control errors, drops or trap timeouts, and preserved hardware timing guards |
| Memory/lifetime | Firmware size, static RAM, stack headroom, peak heap/transport use, and stable resource/connection-slot usage across repeated reconnects |

Exercise bidirectional terminal traffic, full queues, a slow/non-consuming
browser, transient backpressure, Wi-Fi loss/rejoin, USB/browser handover and
repeated connect/disconnect. Verify disk/service progress during network load
and network progress during storage activity; neither may be silently starved.
Do not infer that flash commits are a bottleneck solely because they are
synchronous, or dismiss their effect solely because ordinary disk I/O is fast.

Use a real browser/xterm check when changing browser consumption or input
semantics; mocks alone cannot prove rendered output order or consumption ACK
timing. For bus/timing changes use the documented scope/logic-analyzer workflows.
Hardware measurements require an explicitly safe setup; firmware deployment
still requires the normal authorization and saved-work/power confirmation.
Do not flash or reset merely to obtain a benchmark without that authorization.

Report baseline/result/acceptance outcome, correctness regressions, and any
missing hardware evidence separately. Preserve electrical/flash invariants even
when a change appears faster; an unqualified clock or timing change is not an
acceptable performance result.

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
