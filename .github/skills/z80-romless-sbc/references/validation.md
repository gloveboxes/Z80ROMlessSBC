# Validation Workflows

Run focused checks first, then broaden according to impact. Use terminal CMake; the VS Code CMake Tools extension is unreliable in this repository.

## Firmware and CP/M Build

On Apple silicon macOS, use Apple Container and initialize the shared submodule.
The main image supplies `z80asm`, `z80dasm` and `cpmtools` (`cpmcp`, `cpmrm`,
`fsck.cpm`); the cached test image adds Ninja. CMake uses the container for
Phase 6/9/10 assembly and CP/M image builds. Pico C/C++ builds, the Arm GNU
toolchain, DCC compilation and flashing remain on the host. Linux and Windows
CMake builds use host `z80asm` and `cpmtools`.

Use the maintained launcher from the repository root; it checks the cached
image and builds it when needed:

```sh
python3 scripts/z80_container.py image
python3 scripts/z80_container.py images --output-dir build/cpm
python3 scripts/z80_container.py disk stage
python3 scripts/z80_container.py disk prepare
```

Disk staging and preparation validate the Altair source and use the container;
`disk install` modifies a selected native image and requires its explicit
confirmation. Drive A gets current system records and AUTORUN; B–D are copied
unchanged. Stop the interactive emulator and save wanted session changes before
`python3 scripts/z80_container.py prepare` replaces session disks.

For firmware and DCC-generated code debugging, use `z80dasm` to inspect
linked machine code and `z80asm` for compatible minimal Z80 repros. Set the
correct load origin and distinguish embedded data from instructions; DCC's
host assembler accepts its own syntax.

For manual read-only Z80 disassembly, set `INPUT_DIR` to the input file's
absolute parent directory and `INPUT_FILE` to its basename, then:

```sh
python3 scripts/z80_container.py image
container run --rm --mount "type=bind,source=$INPUT_DIR,target=/input,readonly" \
  --entrypoint z80dasm z80-cpm:local "/input/$INPUT_FILE"
```

Disassembly is printed on the host; redirect it to a chosen host file if
needed. The host `disassemble_cpm64.py` is a separate standard-library audit
tool.

Firmware prerequisites include CMake, Ninja, Python 3, `picotool`, and a
complete Arm GNU bare-metal toolchain with Newlib:

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
git submodule update --init --recursive
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico2_w
cmake --build build --target z80_cpm_images -j
```

Phase 10 provisions `WIFI_SSID`, `WIFI_PASSWORD` and `FS_SERVER` over USB
through the unified Ctrl-] c configuration shell and saves them in ENV; do not
pass credentials to CMake. Remote FS is optional. Quitting after saved changes
announces and performs a disk-flushed, safe-stop reboot; unchanged settings do
not reboot.
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

Z80 execution tests run with `python3 scripts/z80_container.py test`.
Host DCC builds required fixtures; the container runs the emulator's instruction,
flag, T-state and bank regressions first. Add an independently checked
regression for each emulator bug. Emulator tests do not qualify hardware timing.

Run CP/M image tests against this checkout inside the main image, with the
checkout mounted read-only:

```sh
python3 scripts/z80_container.py image
container run --rm --mount "type=bind,source=$PWD,target=/workspace,readonly" \
  --entrypoint python3 z80-cpm:local \
  -m unittest discover -s /workspace/src/cpm -p 'test_*.py' -v
```

These are image-builder unit tests, not the Z80 execution suite above. Host
discovery requires host `z80asm` and `cpmtools` and may skip checks if absent.

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
- Protocol changes: test cumulative/wrapping ACKs, ACKs for never-sent data,
  pending-input admission on the next service pass, rejection of repeated and
  out-of-order frames, reconnect, stale callbacks, heartbeat timeout, and USB
  fallback.
- Transport changes: count accepted `tcp_write` calls separately from output
  attempts. Verify a four-frame terminal burst uses four 521-byte frame writes
  and one flush, while single interactive/control messages flush promptly.
  Rejected writes must preserve ordering; failed flushes must not duplicate
  accepted writes. These sizes describe the current protocol, not universal
  WebSocket limits; update assertions if framing is intentionally changed.
- Receive/lifetime changes: test split/chained pbufs, empty frames, masking,
  fragmented/control interleaving, malformed input, the message size limit,
  several frames in one span, PONG backpressure, send/close/abort from inside
  a callback, close deadline wrap, slot reuse, disconnect/error races,
  destructor cleanup, HTTP final ACKs and zero steady-state allocations.
  The terminal tests must also fail if a terminal callback transmits.
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
selected-slot disk writes, native 512 KiB image loading, installed Drive A warm-boot
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
- Run `PYTHON=.venv-kicad/bin/python npm run kicad` for full ERC, netlist and
  documented-connection parity, exact PCB/DSN/SES reproduction, post-refill
  DRC, route-length budgets and transactional fabrication exports.
- Use `npm run kicad -- --schematic-only` for schematic-only work; it does not
  validate or update the PCB. Follow the PCB skill for routing changes.
- Run `npm run test:control` for injection, data-path and trap regression tests.
- Physically measure reset-exit phase, injected-read data setup, SRAM inhibit,
  stepped I/O completion, both actual OEs, and resumed PWM before qualification.
- Scope evidence's single-pause evaluator is inconclusive for stepped release;
  raw captures and manual timing review are required.

## Documentation

Use the [mkdocs-docs](../../mkdocs-docs/SKILL.md) skill for site, diagram and
C-excerpt validation.

## Qualification Evidence

Do not report a clock rate as qualified without captures and error-free tests at that rate. Preserve:

- DHO814 analog captures using compensated 10x probes and short ground springs.
- DSLogic Plus Group A-D captures using 16-channel, 100 MHz Buffer Mode,
  threshold appropriate to each group, Filter=None, and RLE disabled.
- SRAM setup/write-pulse evidence and ownership-transition evidence.
- Repeated cold boots, assisted-load verification, I/O tests, and fault-injection results
  required by the implementation phase pages.

Separate theoretical calculations, simulated/host-tested behavior, and physical bench measurements in every report.
