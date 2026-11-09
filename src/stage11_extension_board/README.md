# Stage 11: ATtiny85 extension-board firmware

This is a **standalone bench project** using two separate socketed boards:
one for flashing with a **Raspberry Pi Pico 2 W**, and one for testing with
a **Pimoroni Pico Plus 2 W**. **Move only the ATtiny85 between the boards**,
with both boards powered off. Leave each board's wiring in place.

Neither Pico is the Z80 supervisor. Do not use these pin assignments on a
Pico still connected to the Z80 board. No existing CP/M images or supervisor
firmware are changed by building this project.

The sound-only target runs autonomous sweeps, segmented envelopes, pulses,
vibrato, sirens, note/rest sequences, arpeggios and gated tones. Flash presets,
pitch/tempo scaling, priorities, a two-sound playback FIFO, live updates and
readable status/events let games send events instead of timing sound steps.
See the [command protocol](test_ATTiny85/README.md#command-protocol-and-timers).

## Separate board guides

| Board | Guide | Fitted hardware |
| --- | --- | --- |
| Flashing | [Flash the ATtiny85](flash_ATTiny85/README.md) | Pico 2 W, ATtiny85 socket, ISP wires, RESET pull-up and bypass capacitor. No piezo or I2C wiring. |
| Testing | [Test the flashed ATtiny85](test_ATTiny85/README.md) | Pimoroni Pico Plus 2 W, ATtiny85 socket, I2C wires and pull-ups, RESET pull-up, bypass capacitor and passive piezo. PB3/PB4 unconnected. No ISP wiring. |

Both boards use **3.3 V** from their own Pico and common ground within that
board. Each socket needs its own 100 nF capacitor between ATtiny85 pins
8 and 4, and its own 10 kohm RESET pull-up. Do not join the boards' 3.3 V
rails or run wires between their ATtiny85 sockets.

## Flash, then transfer, then test

1. Build the two boards using their separate wiring guides. Check DIP-8
   pin-1 orientation; a ZIF socket on the flashing board makes repeated
   transfers easier.
2. With both boards powered off, insert the ATtiny85 into the flashing
   board. Connect only the **flashing Pico** by USB and run the ISP task.
3. Type uppercase **P** and wait for **SUCCESS: Flash verified.**
4. Exit picocom with **Ctrl-A, then Ctrl-X**. Disconnect USB and any other
   power from **both boards** before removing the chip.
5. Move the ATtiny85 into the testing board's socket, checking the notch
   and pin 1 again. Never insert or remove it while either board is powered.
6. Connect only the **testing Pico** by USB and run the test task. Observe
   the sound and status/events as described in the testing guide.

To reprogram, power off both boards and move the chip back to the flashing
socket. The piezo stays on the testing board, so it cannot load
the programming signals.

## Build

Install the official Pico SDK and an Arm GNU bare-metal toolchain, plus
`avr-gcc`, avr-libc and `avr-objcopy`. On macOS an AVR toolchain is
available from `osx-cross/avr/avr-gcc@9` (Homebrew may request trust approval).

From the repository root, with the Arm toolchain on `PATH`:

```sh
cmake -S src/stage11_extension_board -B build/stage11/pico2_w -G Ninja \
  -DPICO_BOARD=pico2_w
cmake --build build/stage11/pico2_w --target stage11_flash_attiny85 -j
avr-size build/stage11/pico2_w/flash_ATTiny85/attiny85.elf

cmake -S src/stage11_extension_board \
  -B build/stage11/pimoroni_pico_plus2_w_rp2350 -G Ninja \
  -DPICO_BOARD=pimoroni_pico_plus2_w_rp2350
cmake --build build/stage11/pimoroni_pico_plus2_w_rp2350 \
  --target stage11_test_attiny85 -j
```

The SDK defaults to `/Users/dave/GitHub/pico/pico-sdk`, matching the main
Pico firmware project. No SDK-location prompt is needed. To use a different
installation, configure with `-DPICO_SDK_PATH=/path/to/pico-sdk`.

Keep separate build directories for the two SDK board definitions; do not
reuse one CMake cache for both boards. The tasks select these automatically.
For an original Pico, use `-DPICO_BOARD=pico` in a separate build directory.
The [Stage 11 CMake project](CMakeLists.txt) is standalone; the main Z80
build does not acquire an AVR toolchain dependency.

Outputs:

- `build/stage11/pico2_w/flash_ATTiny85/stage11_flash_attiny85.uf2`
- `build/stage11/pimoroni_pico_plus2_w_rp2350/test_ATTiny85/stage11_test_attiny85.uf2`
- `build/stage11/pico2_w/flash_ATTiny85/attiny85.elf` and `attiny85.bin`
- `build/stage11/pico2_w/flash_ATTiny85/attiny85_firmware.h` (generated C array)

CMake compiles the target for `-mmcu=attiny85`, checks its binary fits
8 KiB, and regenerates the embedded array whenever target sources change.
There is no placeholder or manually maintained machine-code array.
Edit the target source, then rebuild the programmer to change the flashed
firmware.

## VS Code build-and-run tasks

Run **Tasks: Run Task** from the Command Palette:

| Task | Action |
| --- | --- |
| **Build and run Pico - Stage 11 ATtiny85 ISP** | Compile the AVR firmware and Pico programmer, flash the flashing board's Pico, and open its USB console. Type **P** to program the ATtiny85. |
| **Build and run Pico - Stage 11 ATtiny85 test** | Build and flash the testing board's Pico, then open its USB console to monitor sound/status. The test starts automatically at power-up and retries after errors. |

The ISP task targets **Pico 2 W**; the test task targets **Pimoroni Pico
Plus 2 W** (`pimoroni_pico_plus2_w_rp2350`). Neither targets the Z80
supervisor. They ask only for a safety confirmation. SDK and toolchain
defaults match the other Pico builds. The task checks for a single RP2350
in BOOTSEL first. If none is accessible and picotool reports a reset-capable
running RP2350, it discovers the unique USB serial port instead. It prints
the selected serial number; no serial-number entry is needed.

Label the Picos **FLASH** and **TEST**. Connect only the intended board by
USB; leave the other board off and do not select the Z80 supervisor.
Ambiguous discovery, missing devices and USB access errors stop the task
before building or flashing. Discovery identifies the chip, not the board
brand: choose the matching ISP/test task yourself.

Before each task, power off and check the matching board's wiring guide.
Disconnect any other power source, then connect that board's USB cable
normally. **The safety confirmation authorizes interrupting its running
firmware.** The maintained flash tool reboots a running SDK USB device into
BOOTSEL automatically using `--force`, without a Z80 stop/flush handshake.
An already-BOOTSEL Pico is flashed without forcing a reset. The selected
serial number is passed explicitly to the flash tool and tracked when USB
returns. Only the Pico firmware region is replaced.

For blank boards or firmware without the SDK USB reset interface, hold
BOOTSEL (Pimoroni **BOOT**) while connecting USB, then rerun the task.

For command-line selection with multiple devices, use
`python scripts/pico_stage11.py isp --serial SERIAL --confirmed` (or
`test` instead of `isp`), after activating `.venv-flash` and checking
`import sys, serial` as in the task. This explicit selection bypasses
automatic selection but still checks whether that device is in BOOTSEL
or normal SDK USB mode.

The tasks require `.venv-flash` with
[`scripts/requirements-flash.txt`](../../scripts/requirements-flash.txt),
`picotool`, `picocom`, CMake, Ninja, the Arm GNU toolchain and the AVR
toolchain described above. The existing build helper discovers the Arm
toolchain from `PATH`, `ARM_GNU_TOOLCHAIN_BIN`, or a local installation.
No extra external 5 V supply is needed for this isolated USB-powered setup.

Keep each Pico on its own board and use its matching task. The tasks do
not move the ATtiny85 or change wiring. USB output alone does not establish
correct physical behavior; check the piezo on the testing board.

## Host regression checks

```sh
cmake -S src/stage11_extension_board/tests -B build/stage11-tests -G Ninja
cmake --build build/stage11-tests
ctest --test-dir build/stage11-tests --output-on-failure
```

Tests compile the actual programmer/target sources against small hardware
fakes. They cover ISP byte/word/page boundaries, padding, full-flash
verification, signature/fuse/transport/timeout failures and RESET release;
USI receive/transmit states, master ACK/NACK, partial/maximal packets,
mailbox backpressure, coherent status reads, acknowledged events/overflow,
all nonzero 16-bit frequencies, effect boundaries, repeats, rests, clock wrap,
priority/FIFO policy, live updates and cancellation. The Pico test runs
against the actual target state machine through fake I2C, including
timeout/short writes at every command, failed reads and recovery.
They do not simulate electrical USI edges or replace hardware acceptance testing.
