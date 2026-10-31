# Building and Testing Pico C Firmware

Run these software checks from the repository root. Native tests compile Pico
logic for the host with hardware replacements; the instruction emulator runs
Z80 code, not ARM firmware. Cross-builds use the real Pico SDK. Physical board
testing remains a separate qualification step.

## 1. Understand the four validation layers

| Layer | What runs | What it establishes |
| --- | --- | --- |
| Native C/C++ host tests | Maintained firmware logic compiled for the Mac or other host, with test SDK headers and functions | Software state transitions, bounds, error handling and scripted fault/interleaving cases |
| Independent Z80 execution | Assembled Z80 programs, BIOS and CP/M in an instruction emulator, with modeled peripherals | Actual Z80 instruction behavior, console/disk protocol use, boot and warm-boot behavior |
| Pico cross-build | ARM compiler and linker with the real Pico SDK | Firmware compiles/links for Pico 2 W, SDK APIs are valid and artifacts fit the protected flash layout |
| Physical bench qualification | Deployed firmware, real Pico, Z80, SRAM, buffers and flash | Actual USB behavior, multicore/interrupt operation, bus ownership, timing, electrical margins and sustained reliability |

## 2. Prerequisites

Follow [Toolchain](../toolchain.md) for checkout and installation. Native tests
need host C/C++ compilers, CMake, Ninja, Python 3.11+, and `z80asm`;
CP/M images also need `cpmtools`. Set `PYTHON` to your installed interpreter:

```sh
export PYTHON=python3
"$PYTHON" --version
```

Node.js optionally enables `terminal_keyboard`; no npm installation is needed
for native tests. The Pico SDK and Arm toolchain are needed only for SDK/TLS
checks and firmware cross-builds.

## 3. Configure, build and run the native C tests

Use a separate build directory from the Pico cross-build. Explicitly select
the host compilers, not `arm-none-eabi-gcc`:

```sh
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DCMAKE_C_COMPILER=cc \
  -DCMAKE_CXX_COMPILER=c++
cmake --build build/host-control
ctest --test-dir build/host-control --output-on-failure --no-tests=error
```

The offline suite covers control/loader state, flash recovery, terminal
queues, services, PIO banking, and HTTP/weather/chat parsing and transport.
Warnings are errors and assertions remain enabled. The optional
`npm run test:control` runs the same configure/build/test sequence.

On macOS/Linux, build the optional live runner with the firmware’s real Mbed TLS
configuration and trust bundle, configure with `PICO_SDK_PATH`:

```sh
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DCMAKE_C_COMPILER=cc -DCMAKE_CXX_COMPILER=c++ \
  -DPICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake --build build/host-control --target http_live_test
python3 src/common/tests/http_transfer_integration.py \
  --runner build/host-control/http_live_test --https
```

The live runner needs `openssl`; `--https` also needs internet access. It tests
local HTTP binary/text transfers, empty files, redirects, chunking, 404 errors,
untrusted TLS rejection, and a verified GitHub HTTPS download. The production
FT/parser/crypto code runs through a native socket adapter; Pico lwIP behavior
is covered separately by offline transport tests.

### What replaces the hardware?

[Test SDK replacements](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/common/tests/fakes)
model flash as a 4 MiB array, SRAM as arrays or injected cycles, GPIO/clock
state, controlled time, USB queues, reboot, and scripted core interleavings.
They enforce software contracts without reproducing physical IRQs, dual-core
scheduling, or electrical timing.

### Phase 9 coverage and its boundaries

| CTest name | Maintained code exercised | Important boundary |
| --- | --- | --- |
| `flash_backend_recovery` | Actual `flash_backend.c`: manifest validation, byte loading/readback controller, cache, journal programming and recovery | Flash, SRAM byte access, bus/clock calls and reboot are modeled |
| `stage09_usb_cpm` | Actual Phase 9 application functions and shared `terminal_queue.c` | Boot/storage and SDK calls are substituted; command/stop interleavings are driven by the harness |
| `usb_core1_console` | Actual `usb_console.c` routing and bounded diagnostic ring | Modeled SDK endpoints assert core-1 affinity; checks concurrent producer/drain, CRLF vs raw console bytes, backpressure, wrap, overflow, disconnect and init failure, not physical IRQ delivery |
| `stage09_autoboot_cpm` | The same application compiled with automatic startup, including its actual `main` initialization path | SDK/storage calls are substituted; tests cover boot before USB connection, retained CP/M output, late failure reporting and no automatic retries |
| `disk_protocol` | Actual `disk_device.c`: Z80-facing commands, single-slot mailbox, BUSY ownership, reads, read-ahead hits/misses, seqlock fallback, write invalidation and fatal error latch | The flash backend is stubbed; this is not the journal test |
| `assisted_loader`, `control_faults` | Shared injected-loader and control code | Cycle/pin models do not establish physical setup/hold timing |

Fault cases include invalid packages, cancelled loads, SRAM mismatches,
interrupted journal writes, queue overflow, disconnects, and initialization
failures. Harnesses check core ownership, disk-first scheduling, safe stops,
and automatic boot before USB connection. A requested 20 MHz clock is checked
as a software value, not measured as a signal.

For a focused native Phase 9 run:

```sh
cmake --build build/host-control \
  --target flash_backend_test stage09_test stage09_autoboot_test disk_test
ctest --test-dir build/host-control \
  -R '^(flash_backend_recovery|stage09_usb_cpm|stage09_autoboot_cpm|disk_protocol)$' \
  --output-on-failure --no-tests=error
```

The authoritative target/test list is
[src/common/tests/CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/tests/CMakeLists.txt).

## 4. Add the independent Z80 instruction emulator

This is an optional host-only dependency, separate from the Pico SDK.
The environment name is shared with the existing arithmetic/SRAM soak tests.
Create it once; reuse it if already configured:

```sh
"$PYTHON" -m venv build/math-soak-emulator
build/math-soak-emulator/bin/python -m pip install \
  -r src/common/tests/requirements-soak.txt
```

The requirements pin `z80==1.0.0`. Register that interpreter with the native
test project and rebuild:

```sh
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DCMAKE_C_COMPILER=cc \
  -DCMAKE_CXX_COMPILER=c++ \
  -DSOAK_EMULATOR_PYTHON="$PWD/build/math-soak-emulator/bin/python"
cmake --build build/host-control
ctest --test-dir build/host-control -N \
  -R '^(math_soak_z80_execution|scope_z80_execution|banked_memory_z80_execution|cpm_bios_z80_execution)$'
```

The listing must contain **all four named execution tests**. Without
`SOAK_EMULATOR_PYTHON`, those tests are not registered; passing the remaining
native suite does not imply they ran. `--no-tests=error` rejects an empty
selection, but does not detect a partly missing selection: inspect the names.

Run the execution tests:

```sh
ctest --test-dir build/host-control \
  -R '^(math_soak_z80_execution|scope_z80_execution|banked_memory_z80_execution|cpm_bios_z80_execution)$' \
  --output-on-failure --no-tests=error
```

The math/scope and bank-transfer programs execute in the independent core.
CP/M tests cover cold/warm boot, directories A–D, transients, AUTORUN, expanded
disk writes, and the read-only disk diagnostic. Modeled peripherals establish
instruction/protocol behavior, not Pico latency or hardware timing.

## 5. Validate CP/M image construction

With Python 3.11+, `z80asm`, and `cpmtools` available:

```sh
"$PYTHON" -m unittest discover -s src/cpm -p 'test_*.py' -v
```

These image tests check packaging, CRCs, exact memory/disk geometry, BIOS
jump-table compatibility and CP/M translation. They are not the independent
instruction-execution tests above.

The separate project-owned CP/M host also tests multi-extent programs and
cross-drive writes; see the
[end-to-end CP/M procedure](../cpm-dcc/end-to-end.md) for its build and run
commands. Neither host model replaces the Pico/physical-board gate.

## 6. Cross-compile the real Pico firmware

Install the SDK and complete Arm toolchain using [Toolchain](../toolchain.md).
Keep the firmware build separate from host tests.

Then configure the separate firmware build:

```sh
cmake -S . -B build/pico -G Ninja \
  -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk" \
  -DPython3_EXECUTABLE="$(command -v "$PYTHON")"
cmake --build build/pico \
  --target z80_stage09_cpm_images z80_stage09_cpm_loader_images z80_cpm_images
```

This builds both Phase 9 variants and Phase 10 against the real SDK, without
flashing. Phase 10 Wi-Fi credentials are configured through USB/ENV at runtime;
they are not build arguments.

| Output under `build/pico/` | Purpose |
| --- | --- |
| `src/stage09_flash_storage/z80_stage09_flash_storage.uf2` | Deployable Phase 9 Pico firmware |
| `src/stage09_flash_storage/z80_stage09_cpm_loader.uf2` | Automatic Phase 9 loader, independent of USB connection |
| `src/stage10_websocket_terminal/z80_stage10_websocket_terminal.uf2` | Deployable Phase 10 Pico firmware |
| `cpm-stage09/` | Matching Phase 9 boot package, disks, manifest and complete flash image |
| `cpm-stage09-loader/` | Matching automatic-loader provisioning set |
| `cpm/` | Matching Phase 10 provisioning set |

Both phases run the Z80 at 20 MHz after SRAM loading/verification. Phase 9
starts on command or automatically with its loader variant; Phase 10 starts
automatically. The linker reserves flash above `0x1D0000` for storage.

Use [VS Code deployment tasks](firmware-build.md#vs-code-deployment-options)
when ready to flash. BIOS changes require matching boot-package and Drive A
images; full provisioning replaces disks and ENV.

## 7. Record software results, then qualify the board

Record the revision/local changes, tool versions, configuration, test names,
results, and artifact hashes. Distinguish host tests, Z80 execution, SDK builds,
and physical bench results. Software checks do not establish electrical
margins, real interrupt/multicore timing, USB behavior, or power-cut recovery.
They need no attached Pico; leave active hardware soaks and their serial
owners undisturbed.

After software validation, follow the
[Phase 9 controlled USB workflow](../implementation/phase-9-flash-storage.md#controlled-usb-workflow)
and its bench checklist. Observe the CP/M prompt and filesystem behavior,
verify safe stop/recovery, and collect the required
[frequency/timing evidence](../implementation/frequency-qualification.md).
The Z80 clock has been scope-measured at 20 MHz on the project breadboard.
Integrated CP/M, disk-I/O and receiving-pin timing checks remain separate
qualification evidence.
