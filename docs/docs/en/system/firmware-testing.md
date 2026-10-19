# Building and Testing Pico C Firmware

This is the complete software-validation workflow, from a fresh checkout to
host tests, Z80 execution tests and deployable Pico firmware. Run commands
from the repository root unless stated otherwise.

!!! important "No Pico emulator is used"
    The Pico C logic runs **natively on the development computer** with test
    replacements for hardware-facing SDK calls. A separate emulator executes
    **Z80 instructions**, not the Pico firmware. Finally, the real ARM
    firmware is built against the real Pico SDK. These checks complement
    each other; none replaces testing the physical board.

## 1. Understand the four validation layers

| Layer | What runs | What it establishes |
| --- | --- | --- |
| Native C/C++ host tests | Maintained firmware logic compiled for the Mac or other host, with test SDK headers and functions | Software state transitions, bounds, error handling and scripted fault/interleaving cases |
| Independent Z80 execution | Assembled Z80 programs, BIOS and CP/M in an instruction emulator, with modeled peripherals | Actual Z80 instruction behavior, console/disk protocol use, boot and warm-boot behavior |
| Pico cross-build | ARM compiler and linker with the real Pico SDK | Firmware compiles/links for Pico 2 W, SDK APIs are valid and artifacts fit the protected flash layout |
| Physical bench qualification | Deployed firmware, real Pico, Z80, SRAM, buffers and flash | Actual USB behavior, multicore/interrupt operation, bus ownership, timing, electrical margins and sustained reliability |

The host tests do not execute an ARM UF2 or emulate an RP2350.
The Z80 tests do not execute the Pico C peripheral handlers: their peripherals
are software models. A successful cross-build does not execute the firmware.

## 2. Prepare a fresh checkout and host tools

For a new checkout:

```sh
git clone https://github.com/gloveboxes/Z80ROMlessSBC.git
cd Z80ROMlessSBC
```

If already working in this repository, keep the existing checkout and changes.
Do not clone over it or reset it to follow this guide.

On macOS, install Apple's Command Line Tools if not already installed:

```sh
xcode-select --install
```

With [Homebrew](https://brew.sh/) available:

```sh
brew install cmake ninja python@3.14 z80asm
export PYTHON=python3.14
"$PYTHON" --version
cc --version
c++ --version
```

Python 3.11 or newer is required for the complete image/BIOS workflow; the
commands use Python 3.14 as the tested macOS example. On another system,
select an installed Python 3.11+ interpreter and install equivalent native
C/C++ compilers, CMake, Ninja and `z80asm`. `PYTHON` is a shell convenience
used by this guide, not a firmware CMake setting.

Node.js is optional for the browser-keyboard test. If it is installed, CMake
adds `terminal_keyboard`; otherwise that test is not registered. Native
firmware tests do not require npm dependency installation.

**The native C tests need neither a Pico SDK nor an ARM toolchain.** Both are
needed later for the real firmware build.

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

This configures a standalone host CMake project. Its test targets enable
warnings as errors and keep assertions enabled with `-UNDEBUG`, including
when the build type would normally disable assertions.

The equivalent existing convenience command is `npm run test:control`.
It configures, builds and runs the native suite; it does not provision an
emulator environment or deploy to hardware.

### What replaces the hardware?

Test headers live in
[src/common/tests/fakes/](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/common/tests/fakes).
Each harness supplies the functions needed by the production code under
test. Depending on the test, these include:

| Hardware/SDK operation | Host test implementation |
| --- | --- |
| Flash access through XIP (execute-in-place mapping) | A 4 MiB byte array |
| Flash erase/program | Modeled sector/page operations, alignment/bounds checks and flash programming bit rules |
| Assisted Z80 SRAM access | A 64 KiB array, or an injected-cycle model in the loader/control tests |
| GPIO, reset and clock control | Recorded pin/control state, expected operations or requested frequencies |
| Time and deadlines | Controlled software time rather than a measured real-time clock |
| Watchdog reboot | An intercepted reboot, followed by simulated restart/recovery using the retained flash array |
| USB CDC input/output | Controlled connection state, input bytes and output capacity |
| Core/queue interaction | Test queues and explicitly scripted ordering; not a real RP2350 dual-core execution |

The replacements differ between harnesses. A stubbed helper passing a test
is not proof of that helper's real SDK implementation.

### Phase 9 coverage and its boundaries

| CTest name | Maintained code exercised | Important boundary |
| --- | --- | --- |
| `flash_backend_recovery` | Actual `flash_backend.c`: manifest validation, byte loading/readback controller, cache, journal programming and recovery | Flash, SRAM byte access, bus/clock calls and reboot are modeled |
| `stage09_usb_cpm` | Actual Stage 9 application functions and shared `terminal_queue.c` | Boot/storage and SDK calls are substituted; command/stop interleavings are driven by the harness |
| `disk_protocol` | Actual `disk_device.c`: Z80-facing commands, queues, BUSY publication and fatal error latch | The flash backend is stubbed; this is not the journal test |
| `assisted_loader`, `control_faults` | Shared injected-loader and control code | Cycle/pin models do not establish physical setup/hold timing |

The flash-backend test injects bad package lengths/CRCs/reset vectors,
cancelled loads, SRAM read/write/mismatch faults, corrupt journal
programming and all eight supported watchdog fault points. The terminal
test checks framing, ordinary CP/M input, queue/backpressure behavior,
startup reports, safe stop and a **20 MHz start request**. That last check
does not generate or measure a 20 MHz signal.

For a focused native Phase 9 run:

```sh
cmake --build build/host-control \
  --target flash_backend_test stage09_test disk_test
ctest --test-dir build/host-control \
  -R '^(flash_backend_recovery|stage09_usb_cpm|disk_protocol)$' \
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
  -R '^(math_soak_z80_execution|scope_z80_execution|cpm_bios_z80_execution)$'
```

The listing must contain **all three named execution tests**. Without
`SOAK_EMULATOR_PYTHON`, those tests are not registered; passing the remaining
native suite does not imply they ran. `--no-tests=error` rejects an empty
selection, but does not detect a partly missing selection: inspect the names.

Run the execution tests:

```sh
ctest --test-dir build/host-control \
  -R '^(math_soak_z80_execution|scope_z80_execution|cpm_bios_z80_execution)$' \
  --output-on-failure --no-tests=error
```

The actual resident Z80 math/scope images run in the independent core.
The CP/M execution test assembles the maintained BIOS and CCP/BDOS, then
checks cold boot, BIOS reports, directories on A-D, transient exit/warm boot,
and all 10,240 records of the read-only disk diagnostic. Software peripheral
models provide terminal and disk responses. This verifies instructions and
protocol use, not Pico interrupt latency or USB/flash hardware timing.

For a focused Phase 9 run with emulation enabled:

```sh
ctest --test-dir build/host-control \
  -R '^(flash_backend_recovery|stage09_usb_cpm|disk_protocol|cpm_bios_z80_execution)$' \
  --output-on-failure --no-tests=error
```

Expect four named CTest checks; the CP/M execution check contains multiple
Python test cases. The host environment never opens the board's USB port.

## 5. Validate CP/M image construction

With Python 3.11+ and `z80asm` available:

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

Install a complete
[Arm GNU bare-metal toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
for your host, including Newlib. Select the `arm-none-eabi` toolchain; a
compiler without its C runtime is insufficient. The example path below
matches the tested local installation; adjust it to your installed version.

Install the Pico SDK with submodules if it is not already present:

```sh
mkdir -p "$HOME/GitHub/pico"
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git \
  "$HOME/GitHub/pico/pico-sdk"
```

Reuse an existing complete SDK instead of cloning over it. On macOS,
`picotool` can be installed with `brew install picotool`.
Then configure the separate firmware build:

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
arm-none-eabi-gcc --version
arm-none-eabi-gcc -print-file-name=libc.a
cmake -S . -B build/pico -G Ninja \
  -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk" \
  -DPython3_EXECUTABLE="$(command -v "$PYTHON")" \
  -DZ80_WIFI_SSID= \
  -DZ80_WIFI_PASSWORD=
cmake --build build/pico \
  --target z80_stage09_cpm_images z80_cpm_images
```

If the Newlib query prints only `libc.a` rather than a library path, check
the toolchain installation. Initial configuration also needs network access
to obtain the pinned WebSocket dependency if it is not cached.

This builds both affected stages against the **real SDK**. Empty Wi-Fi
credentials keep networking disabled; credentials are not needed for the
Stage 9 USB baseline. For network testing, configure credentials locally
without sharing generated headers or CMake caches.

| Output under `build/pico/` | Purpose |
| --- | --- |
| `src/stage09_flash_storage/z80_stage09_flash_storage.uf2` | Deployable Phase 9 Pico firmware |
| `src/stage10_websocket_terminal/z80_stage10_websocket_terminal.uf2` | Deployable Stage 10 Pico firmware |
| `cpm-stage09/` | Matching Stage 9 boot package, disks, manifest and complete flash image |
| `cpm/` | Matching Stage 10 provisioning set |

The build generates UF2/binary images; it **does not flash or execute them**.
The stage definitions retain their distinct clock configuration:
Stage 9 uses nominal 20 MHz after explicit load/verify/start, while Stage 10
retains its separate 1 MHz startup default. Neither is qualified by a build.
The linker protects storage above firmware offset `0x290000`; the physical
flash size remains 4 MiB.

Use the [firmware build/provisioning reference](firmware-build.md) for other
stage targets and
[verified provisioning commands](firmware-build.md#72-flash-provisioning).
Use matching boot-package and Drive A system images after BIOS changes.
Full-flash provisioning replaces all disk contents; keep backups.

## 7. Record software results, then qualify the board

Record the source revision/local changes, tool versions, build configuration,
test names/results and artifact hashes. Keep separate labels for:

- **Host logic tests passed.**
- **Z80 instruction-emulator tests passed.**
- **Real Pico firmware cross-build passed.**
- **Physical bench tests passed at the recorded measured clock.**

Do not collapse these into an unqualified "hardware passed".
Native tests do not establish real dual-core scheduling, SDK flash lockout,
interrupt/trap latency, USB enumeration/backpressure, actual reset/BUSACK
transitions, electrical margins or flash endurance.

!!! warning "Do not disturb an active physical test"
    No software check above requires opening serial, resetting or programming
    the board. Leave an existing soak and its serial owner alone. Wait for it
    to finish before deploying; do not change probes or wiring while running.

After software validation, follow the
[Phase 9 controlled USB workflow](../implementation/phase-9-flash-storage.md#controlled-usb-workflow)
and its bench checklist. Observe the CP/M prompt and filesystem behavior,
verify safe stop/recovery, and collect the required
[frequency/timing evidence](../implementation/frequency-qualification.md).
Nominal 20 MHz remains an operating assumption until the relevant physical
CP/M, disk-I/O and timing checks pass.
