# 7. Reference Firmware Implementations

The maintained, buildable firmware is the canonical implementation:
[browse the source tree](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src)
or use the [complete source index](../reference/source-index.md). The
corresponding [implementation phase pages](../implementation/index.md) provide
design-level excerpts for
the safety invariants and integration order: variable-frequency clock
generation, bus acquisition, synchronous I/O trapping, flash image loading,
and terminal integration. Do not copy those excerpts in place of the
maintained source.

## Choose the image for your current phase

There are two processors and two kinds of software: **Pico firmware** runs
the supervisor and diagnostics; **Z80 programs** run from SRAM after the Pico
loads them. Programming the Pico does not program the GAL. The ATF22V10 needs
a separate programmer and compiled JEDEC file in Phase 2.

During construction, load only the Pico stage matching the hardware you have
installed. Stage 10 and the complete flash image assume the whole circuit is
ready; they are not substitutes for Stage 1 diagnostics. The phase pages link
their maintained applications, and the [source index](../reference/source-index.md)
lists the stage directories.

## 7.1 Firmware and CP/M Build

The repository implements the ten cumulative firmware stages under `src/`.
Stage 10 includes the journaled flash disks, CP/M boot loader, and WebSocket
terminal. Its host build also assembles the board-native CP/M 2.2 BIOS,
assembles the optimized CCP/BDOS from `cpm64_z80.asm`, and emits all
provisionable images.

Install CMake, Ninja, Python 3, `picotool`, and `z80asm`. The firmware needs a
complete Arm GNU bare-metal toolchain with Newlib; the Homebrew compiler alone
does not provide the required runtime. Install the
[Pico SDK](https://github.com/raspberrypi/pico-sdk) with its Git submodules
initialized. Run these commands from the repository root, replacing the SDK
and toolchain paths with your local installations; the checked-in default SDK
path is specific to the author's machine. A known working macOS setup is:

```sh
brew install cmake ninja picotool z80asm
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

The command above builds the first hardware diagnostic. When you reach the
storage/terminal phases, build the full image set with:

```sh
cmake --build build --target z80_cpm_images -j
```

The
`z80_cpm_images` target builds Stage 10 and writes these files to `build/cpm/`:

| Artifact | Purpose |
| --- | --- |
| `z80boot.pkg` | Manifest, CRCs, and the reset-ready 64 KiB Z80 image |
| `drive_a_cpm63k-z80.img` | Native CP/M system disk with the Z80 SBC BIOS |
| `drive_b_bdsc.img` through `drive_d_blank.img` | Converted 320 KiB disks |
| `z80romless-flash.bin` | Complete 4 MiB initial-provisioning image |
| `manifest.json` | Geometry, addresses, sizes, and SHA-256 values |

Run the host regression checks with:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

### Load a stage and open its console

1. Disconnect the bench supply and USB before changing installed devices.
  For initial Stage 1 loading, program the Pico off the breadboard so USB
  cannot energize untested wiring.
2. Hold the Pico's **BOOTSEL** button while connecting its USB data cable to
  the computer, then release it. It appears as a removable drive in this
  mode, not as the application's serial port. BOOTSEL does not erase flash
  by itself.
3. Load the stage UF2 and reboot using the Stage 1 commands below.
4. Disconnect USB before placing the Pico in the board. Apply power as
  specified by the current phase, then open its USB serial port using a
  serial terminal. On macOS, inspect `ls /dev/cu.usbmodem*`; identify the
  port belonging to this Pico. No external USB-to-UART adapter is required.
5. Select 115200 baud, 8 data bits, no parity, 1 stop bit, and no flow control
  if the terminal asks. This is USB CDC, so baud rate does not set Z80 clock
  speed. Commands are single characters, such as Stage 1 `s` for status and
  `w` for walking outputs; no Enter is required. Only one terminal program
  can use the port at a time.

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

## 7.2 Flash Provisioning

For a new or disposable flash, put the Pico in BOOTSEL mode and write the
complete image. This erases existing CP/M disk contents and the journal:

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
counts and disk/fatal status. CP/M disk writes are persistent, so retain host
backups before full reprovisioning.
