# 7. Reference Firmware Implementations

The maintained, buildable firmware is the canonical implementation:
[browse the source tree](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src)
or use the [complete source index](../reference/source-index.md). The
corresponding [implementation phase pages](../implementation/index.md) embed
complete source files in collapsible sections alongside explanations of
the safety invariants and integration order: variable-frequency clock
generation, bus acquisition, synchronous I/O trapping, flash image loading,
and terminal integration. The code is included directly from the checkout at
documentation build time. Build from the repository rather than copying
displayed files into a separate project.

For one complete fresh-checkout workflow covering native C tests, hardware
test replacements, Z80 emulation, real SDK cross-builds and their limitations,
see [Building and Testing Pico C Firmware](firmware-testing.md).

## Breadboard-first, shared Pico firmware

The staged Pico applications remain focused on the breadboard construction
and bring-up plan. The existing PCB is for the superseded circuit and is not
compatible with this firmware. Its migration is deferred; do not manufacture
it or use the new firmware on that layout without separately updating wiring.

Keep the shared defaults conservative. Stage 10 starts the Z80 at 1 MHz;
the PCB's higher qualification target must not silently raise that default
or bypass startup, bus-ownership, or timing safeguards. Any future
hardware-specific configuration must be explicit and preserve the breadboard
defaults. Operating-frequency qualification remains specific to each
physical build.

All stages configure `clk_sys` to 144 MHz at SDK startup. The 12 MHz crystal
feeds PLL_SYS with reference divider 1, a 1440 MHz VCO, and post-dividers 5 and 2.
This stays within the RP2350's rated limits and lets integer PWM generate a
nominally exact 8 MHz Z80 clock (`144 MHz / 18`) with 50% duty cycle. USB keeps
its separate 48 MHz PLL; peripheral/PIO timing uses the configured system clock.
This does not change the 1 MHz Z80 startup default or qualify 8 MHz hardware
operation. See the [frequency qualification plan](../implementation/frequency-qualification.md).

Stage 9 is an explicit exception: after safe startup and before initializing
USB it selects a 120 MHz supervisor clock, then uses nominal 20 MHz PWM only
after complete SRAM loading/verification. The diagnostic variant starts on
command; the automatic loader starts on Pico reset without waiting for USB.
Its clock/CPU/trap chain is compiled
separately with the existing 20 MHz opt-in; other stages retain their own
limits/defaults. This is an operating assumption, not new bench qualification.

Shared firmware does not make every early-stage diagnostic safe on a fully
populated PCB. Respect each stage's required device population and test
preconditions, especially walking-output tests intended for empty destination
sockets.

## Choose the image for your current phase

There are two processors and two kinds of software: **Pico firmware** runs
the supervisor and diagnostics; **Z80 programs** run from SRAM after the Pico
loads them by feeding instructions to the Z80. No PLD or external programmer
is required; the HCT32 implements fixed combinational logic.

During construction, load only the Pico stage matching the hardware you have
installed. Stage 10 and the complete flash image assume the whole circuit is
ready; they are not substitutes for Stage 1 diagnostics. The phase pages link
their maintained applications, and the [source index](../reference/source-index.md)
lists the stage directories.

Phase numbers match the `stageNN_*` directory numbers, not the documentation's
section numbers (for example, section 8.2 is Phase 1). Directory names describe
the firmware diagnostic, so they need not repeat the hardware phase title.
`src/stage00_power/` contains only a checklist: Phase 0 has no Pico firmware
or build target.

| Phase | Source directory | CMake target |
| ---: | --- | --- |
| 1 | `src/stage01_supervisor/` | `z80_stage01_supervisor` |
| 2 | `src/stage02_buffers_clock/` | `z80_stage02_buffers_clock` |
| 3 | `src/stage03_mcp23s17/` | `z80_stage03_control_inputs` |
| 4 | `src/stage04_address_bus/` | `z80_stage04_address_bus` |
| 5 | `src/stage05_data_bus/` | `z80_stage05_data_bus` |
| 6 | `src/stage06_sram_dma/` | `z80_stage06_sram_loader` |
| 6 bus grants | `src/stage06_sram_dma/` | `z80_stage06_bus_grant` |
| 8 | `src/stage08_virtual_io/` | `z80_stage08_virtual_io` |
| 9 | `src/stage09_flash_storage/` | `z80_stage09_flash_storage` |
| 9 automatic loader | `src/stage09_flash_storage/` | `z80_stage09_cpm_loader` |
| 10 | `src/stage10_websocket_terminal/` | `z80_stage10_websocket_terminal` |

Build the target in the table for your phase. Its UF2 is written to
`build/src/<source-directory-name>/<target>.uf2`; for example, Phase 3 uses
`build/src/stage03_mcp23s17/z80_stage03_control_inputs.uf2`.

## 7.1 Firmware and CP/M Build

The repository implements the cumulative firmware stages under `src/`.
The former Phase 7 CPU fixture is now the Phase 6 bus-grant diagnostic;
Phases 8-10 keep their existing numbers and target names.
Stage 10 includes the journaled flash disks, CP/M boot loader, and WebSocket
terminal. Its host build also assembles the board-native CP/M 2.2 BIOS,
assembles the optimized CCP/BDOS from `cpm64_z80.asm`, and emits all
provisionable images.

Install CMake, Ninja, Python 3.11 or newer, `picotool`, `z80asm`, and
`cpmtools` (`cpmcp` and `cpmrm` install the native AUTORUN utility into drive A).
The firmware needs a
complete Arm GNU bare-metal toolchain with Newlib; the Homebrew compiler alone
does not provide the required runtime. Install the
[Pico SDK](https://github.com/raspberrypi/pico-sdk) with its Git submodules
initialized. Run these commands from the repository root, replacing the SDK
and toolchain paths with your local installations; the checked-in default SDK
path is specific to the author's machine. A known working macOS setup is:

```sh
brew install cmake ninja picotool z80asm cpmtools
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk" \
  -DZ80_WIFI_SSID='your-network' \
  -DZ80_WIFI_PASSWORD='your-password'
cmake --build build --target z80_stage01_supervisor -j
```

Wi-Fi credentials are written only to the generated build tree. An empty SSID
leaves networking disabled while flash-disk service continues to operate.
Do not share the generated credentials or CMake cache in diagnostic logs.

### Building from the Stage 1 directory

The stage directories are not standalone CMake projects: they depend on the
repository's top-level CMake configuration. `cmake --build` requires an
already configured build tree; `mkdir build` alone does not create one.
If it reports that the build directory is missing or that `CMakeCache.txt`
is missing, run the configure step first.

To keep a local build tree while working in `src/stage01_supervisor`, run:

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
cmake -S ../.. -B build -G Ninja \
  -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk" \
  -DZ80_WIFI_SSID= \
  -DZ80_WIFI_PASSWORD=
cmake --build build --target z80_stage01_supervisor -j
```

As above, adjust the toolchain and SDK paths for your installation. Here,
`-S ../..` selects the repository root, while `-B build` writes the generated
build tree inside the current stage directory. The UF2 is therefore at
`src/stage01_supervisor/build/src/stage01_supervisor/z80_stage01_supervisor.uf2`
relative to the repository root. Run subsequent build commands from the same
stage directory; root-level build and flashing examples elsewhere on this
page assume a repository-root `build/` tree instead.

### Building the full image set

The Stage 1 target builds the first hardware diagnostic. When you reach the
storage/terminal phases, use the repository-root configuration above and run
this from the repository root to build the full image set:

```sh
cmake --build build --target z80_cpm_images -j
```

The
`z80_cpm_images` target builds Stage 10 and writes these files to `build/cpm/`:

| Artifact | Purpose |
| --- | --- |
| `z80boot.pkg` | Manifest, CRCs, and the reset-ready 64 KiB Z80 image |
| `drive_a_cpm63k-z80.img` | Native CP/M system disk with the Z80 SBC BIOS and native AUTORUN |
| `AUTORUN.COM` | Native auto-run configuration utility, also installed on drive A |
| `drive_b_bdsc.img` through `drive_d_blank.img` | Converted 320 KiB disks |
| `z80romless-flash.bin` | Complete 4 MiB initial-provisioning image |
| `manifest.json` | Geometry, addresses, sizes, and SHA-256 values |

The native utility supports cold-only, warm-only, both, and disabled boot modes.
Its settings use drive A's final reserved system record, not a CP/M file or
boot-code location. Freshly generated drive A images initialize auto-run
disabled. See [native auto-run and recovery](../implementation/phase-9-flash-storage.md#native-cpm-auto-run).

For the USB-only Phase 9 baseline, build this instead:

```sh
cmake --build build --target z80_stage09_cpm_images -j
```

It writes the same artifact names to `build/cpm-stage09/`, but the complete
flash image contains Stage 9 firmware, not Stage 10. Stage 9 waits with RESET
LOW until Ctrl+] `l` loads/verifies SRAM and Ctrl+] `g` starts at nominal
20 MHz. Its optional Ctrl+] `t` read-only Z80 disk test requires a reboot
and fresh load before CP/M execution. See the
[Phase 9 USB workflow](../implementation/phase-9-flash-storage.md#controlled-usb-workflow).

For normal operation without diagnostic load/start commands, build the
separate automatic loader:

```sh
cmake --build build --target z80_stage09_cpm_loader -j
```

Its firmware is
`build/src/stage09_flash_storage/z80_stage09_cpm_loader.uf2`. Every Pico reset
recovers/validates storage, loads and verifies all SRAM, resets the Z80 and
starts CP/M without requiring a connected terminal. Successful boot retains
the CP/M banner/prompt for picocom and suppresses the diagnostic menu.
The [automatic-loader workflow](../implementation/phase-9-flash-storage.md#automatic-cpm-loader)
documents startup, failure reporting and safe restart.

The optional `z80_stage09_cpm_loader_images` target emits the corresponding
complete provisioning set in `build/cpm-stage09-loader/`.

Run the host regression checks with:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

### Load a stage and open its console

1. Disconnect the bench supply and USB before changing installed devices.
  For initial Stage 1 loading, program the Pico off the breadboard so USB
  cannot energize untested wiring. Do this before fitting it and adding
  signal jumpers in Phase 1.
2. Hold the Pico's **BOOTSEL** button while connecting its USB data cable to
  the computer, then release it. It appears as a removable drive in this
  mode, not as the application's serial port. BOOTSEL does not erase flash
  by itself.
3. Load your phase's UF2 and reboot. The commands below are for Stage 1;
  for later phases, use the UF2 path for that phase from the table above.
4. For initial Stage 1 setup, disconnect USB, fit the Pico, and complete the
  [Phase 1 wiring checks](../implementation/phase-1-supervisor.md#wiring-pico-2-w)
  with all power disconnected. Apply external +5 V before reconnecting USB,
  then open its USB serial port using a
  serial terminal. On macOS, inspect `ls /dev/cu.usbmodem*`; identify the
  port belonging to this Pico. No external USB-to-UART adapter is required.
5. Select 115200 baud, 8 data bits, no parity, 1 stop bit, and no flow control
  if the terminal asks. This is USB CDC, so baud rate does not set Z80 clock
  speed. Commands are single characters, such as Stage 1 `s` for status and
  `w` for walking outputs; no Enter is required. Only one terminal program
  can use the port at a time.

For subsequent in-board programming, keep external +5 V applied before
connecting USB and throughout programming. Disconnect USB before removing
external +5 V. Do not program or run the populated board from USB alone:
U9's AHCT245 data ports lack power-off isolation. Remove the Pico with both
supplies disconnected if USB-only programming is needed.

Stage 1 programming commands for step 3, run from the repository root:

```sh
picotool load -v build/src/stage01_supervisor/z80_stage01_supervisor.uf2
picotool reboot
```

The port disappears during BOOTSEL/reboot and may get a different name when
the application returns. Reconnect the terminal if needed. A missed startup
banner is not a failed boot: try the stage's status command. If no port
appears, first check that the cable carries data, the UF2 is for Pico 2 W,
and the application has left BOOTSEL mode.

### macOS console with picocom

Install `picocom` once, then list the USB serial ports with the Pico running
the stage firmware (not in BOOTSEL mode):

```sh
brew install picocom
ls /dev/cu.usbmodem*
```

Identify the port belonging to the Pico, then start the terminal, replacing
the example device name with the one listed on your computer:

```sh
picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/cu.usbmodemXXXX
```

Follow the power sequence above before connecting USB. Keep `picocom` open
during console-driven tests. For Stage 1, type `s` to sample the inputs and
`w` to run the control-output walking-one test; neither command needs Enter.
Run `w` only with the required Phase 1 hardware population. To exit `picocom`,
press **Ctrl+A**, then **Ctrl+X**. Close it before programming the Pico again
or opening another terminal on the same port.

## 7.2 Flash Provisioning

!!! warning "Finish any running hardware test before provisioning"
    Building does not deploy. Do not replace firmware, open a second serial
    owner, or reset the Pico during an active soak. Disk/full-image replacement
    is destructive. This non-production development workflow creates no backups.

### Reusable Python flashing tools

Routine updates use `scripts/pico_flash.py`; standalone serial discovery and
Stage 9 commands use `scripts/pico_supervisor.py`. Their complete setup,
same-shell `.venv-flash` activation/preflight, examples, command options and
troubleshooting now live under
[Tools: Pico flashing and USB supervisor](../tools/pico-tools.md).

For an already matching installation, follow the
[firmware-only update procedure](../tools/pico-tools.md#firmware-updates).
It confirms stop/flush, gracefully closes verified picocom owners, and verifies
the programmed bytes without post-flash boot checks. Automatic stop speaks the
currently running Stage 9 protocol; other running phases require their own safe
stop before using `--stopped`.

#### Selective disk updates without backups

Use the [selective disk procedure](../tools/pico-tools.md#selective-disk-updates-without-backups)
to replace only selected A-D slots without a firmware rebuild, backups or backup
prompts. It includes native/Altair validation, Drive A compatibility checks,
journal checks and read-back verification. Firmware, boot data, the journal,
environment and unselected drives are not written.

### Full provisioning and coordinated system updates

Firmware-only updates leave the boot package, journal, persistent environment and all four disks unchanged. Do not
overwrite Drive A or use a complete flash image merely to change startup mode.
On new/disposable storage, build `z80_stage09_cpm_loader_images` and use its
`build/cpm-stage09-loader/z80romless-flash.bin` instead of the manual image in
the full-provisioning example below.

For **Stage 9**, build `z80_stage09_cpm_images`, enter BOOTSEL with the
documented external-5-V-before-USB sequence, then provision new/disposable
storage using:

```sh
picotool load -v build/cpm-stage09/z80romless-flash.bin -t bin -o 0x10000000
picotool reboot
```

To preserve drives B-D and the journal, selectively update the Stage 9
firmware, boot package and matching Drive A system image:

```sh
picotool load -v build/src/stage09_flash_storage/z80_stage09_flash_storage.uf2
picotool load -v build/cpm-stage09/z80boot.pkg -t bin -o 0x102A0000
picotool load -v build/cpm-stage09/drive_a_cpm63k-z80.img -t bin -o 0x102C0000
picotool reboot
```

Updating Drive A replaces its files; no backup is created by this workflow. Updating only the boot
package leaves stale BIOS bytes in Drive A's reserved tracks and is not a
complete matching-system update. Open USB CDC and follow the Phase 9
load/start procedure; no Wi-Fi credentials or browser are needed.

Before a selective disk-image update, complete a clean stop/flush. If a fault
left a committed journal entry, recover and verify that old/new block first;
do not let a pending replay overwrite part of a newly provisioned disk image.

The remaining examples provision the final **Stage 10** network image.
For a new or disposable flash, put the Pico in BOOTSEL mode and write the
complete image. This erases existing CP/M disk contents, the journal and
[persistent environment variables](io-ports.md#persistent-environment-api):

```sh
picotool load -v build/cpm/z80romless-flash.bin -t bin -o 0x10000000
picotool reboot
```

For normal updates, load only the firmware and selected storage regions. These
commands preserve regions not named by the input file:

```sh
picotool load -v build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.uf2
picotool load -v build/cpm/z80boot.pkg -t bin -o 0x102A0000
picotool load -v build/cpm/drive_a_cpm63k-z80.img -t bin -o 0x102C0000
picotool load -v build/cpm/drive_b_bdsc.img -t bin -o 0x10310000
picotool load -v build/cpm/drive_c_escape.img -t bin -o 0x10360000
picotool load -v build/cpm/drive_d_blank.img -t bin -o 0x103B0000
picotool reboot
```

After association, browse to `http://<pico-dhcp-address>:8088/`. The USB serial
console reports Stage 10 startup and accepts `s` to print terminal queue/drop
counts and disk/fatal status. CP/M disk writes are persistent; full reprovisioning
replaces their contents without backups in this development workflow.
