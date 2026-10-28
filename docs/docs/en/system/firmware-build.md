# 7. Reference Firmware Implementations

Build from the repository’s [maintained sources](../reference/source-index.md).
The [phase pages](../implementation/index.md) include current code directly
from the checkout. For native tests, Z80 emulation, and SDK cross-builds, see
[Building and Testing Pico C Firmware](firmware-testing.md).

## Breadboard-first, shared Pico firmware

Firmware targets the breadboard circuit. Match firmware to the installed
hardware; early walking-output diagnostics require their specified population.

SDK startup uses a 144 MHz system clock and independent 48 MHz USB clock.
Phases 9/10 switch to 120 MHz before USB initialization and run the Z80 at
20 MHz after SRAM loading/verification. Other phases retain their own clock
limits and defaults. See [frequency qualification](../implementation/frequency-qualification.md).

Phase 10 enables PIO clock/bank switching by default after a successful
second-bank probe. Without bank hardware or PIO resources, runtime banking is
unavailable; other I/O retains PWM/software handling. The diagnostic Phase 9
image starts CP/M on command; its automatic-loader variant and Phase 10 start
on reset without waiting for USB.

## Choose the image for your current phase

**Pico firmware** supervises the board; **Z80 programs** execute from SRAM
loaded through injected Z80 instructions. Build only the phase matching your
installed circuit. Phase 0 is a checklist with no firmware target; the former
Phase 7 fixture is now the Phase 6 bus-grant diagnostic.

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

## VS Code deployment options

Open **Tasks: Run Task**. Task names below match
[.vscode/tasks.json](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/.vscode/tasks.json).

### Phase diagnostics

Each entry has both **Build Pico - …** (build only) and **Flash Pico - …**
(build and flash firmware) tasks. Flashing preserves CP/M storage and ENV.

| Task suffix (after either prefix) | Description |
| --- | --- |
| Phase 1 supervisor | Check safe control outputs and sampled inputs. |
| Phase 2 buffers and clock | Check buffer isolation, clock generation, and reset. |
| Phase 3 control inputs | Check control signals and I/O address sampling. |
| Phase 4 address bus | Check address-trunk wiring. |
| Phase 4 reset meter | Hold reset and static control levels for meter checks. |
| Phase 5 data bus | Check data translation and buffer direction/isolation. |
| Phase 6 SRAM loader | Load and verify SRAM through injected Z80 instructions. |
| Phase 6 bus grants | Check BUSREQ/BUSACK ownership and release. |
| Phase 6 stepped loader | Run stepped SRAM diagnostics and full-memory checks. |
| Phase 6 PWM memory | Screen SRAM with test patterns under PWM clocking. |
| Phase 6 PWM 20 MHz | Screen all 64 KiB with memory patterns at 20 MHz. |
| Phase 6 one-hour 20 MHz soak | Run one hour of Z80 arithmetic and SRAM checks. |
| Phase 6 14-hour 20 MHz soak | Run the extended 14-hour arithmetic/SRAM soak. |
| Phase 8 virtual I/O | Check trapped Z80 I/O and USB terminal handling. |
| Phase 6 20 MHz scope diagnostic | Run a no-I/O Z80/SRAM loop for oscilloscope measurements. |

### System and storage deployment

| Task | Effect |
| --- | --- |
| **Build and flash Pico - Phase 9 firmware** | Select Diagnostic or Automatic loader; update firmware only |
| **Build and flash Pico - Phase 10 firmware** | Update 20 MHz PIO firmware only |
| **Build and flash Pico - Phase 9 firmware and disks** | Select variant; replace all flash, including disks and ENV |
| **Build and flash Pico - Phase 10 firmware and disks** | Replace all flash, including disks and ENV |
| **Build and flash Pico - CP/M system and Drive A** | Rebuild and replace matching boot package and Drive A; preserve firmware, B–D, journal, and ENV |
| **Flash Pico - All disks (A-D)** | Regenerate/convert all four disks; validate and replace matching boot package and A–D; preserve firmware and ENV |
| **Flash Pico - Drive A** | Regenerate native system disk; check installed boot compatibility; replace A only |
| **Flash Pico - Drive B** | Convert BDS C source disk and replace B only |
| **Flash Pico - Drive C** | Convert Escape source disk and replace C only |
| **Flash Pico - Drive D** | Convert blank source disk and replace D only |

Firmware-only tasks preserve the boot package, disks, journal, and ENV.
Deployment tasks force entry without a running-firmware stop handshake;
finish active work and follow the
[task requirements](../system/firmware-build.md#deployment-task-requirements).
Disk and full-flash replacements create no backups.

### Host emulator

**Prepare interactive CP/M emulator** builds the host, adapter, and images
without accessing the Pico. The **Interactive CP/M emulator** Run and Debug
configuration runs it automatically; see the [interactive test](../cpm-dcc/interactive.md).

## 7.1 Firmware and CP/M Build

Phase 10 combines flash disks, the CP/M loader, and WebSocket terminal.
The image target also assembles the optimized CCP/BDOS and board BIOS.

Install and configure the prerequisites using [Toolchain](../toolchain.md).
Use the [VS Code tasks above](#vs-code-deployment-options) to build/deploy;
the tasks select the toolchain and configure CMake automatically.

Phase 10 opens USB configuration when Wi-Fi credentials are missing; use
**Ctrl-] c** later. Settings are stored in ENV, including `WIFI_SSID`,
`WIFI_PASSWORD`, and optional `FS_SERVER`; keep credentials out of build
arguments. USB CP/M remains available without Wi-Fi. The shell also configures
chat, weather, and timezone. Quitting after saved changes flushes disks and
reboots; save application work first. See [Phase 10 setup](../implementation/phase-10-websocket.md).
Remote-FS I/O remains deferred.

### Building the full image set

From the configured repository root:

```sh
cmake --build build --target z80_cpm_images -j
```

The `z80_cpm_images` target builds Phase 10 and writes these files to `build/cpm/`:

| Artifact | Purpose |
| --- | --- |
| `z80boot.pkg` | Header, CRCs, and reset-ready 64 KiB Z80 image |
| `drive_a_cpm63k-z80.img` | Native CP/M system disk with the Z80 SBC BIOS and native AUTORUN |
| `AUTORUN.COM` | Native auto-run configuration utility, also installed on drive A |
| `drive_b_bdsc.img` through `drive_d_blank.img` | Converted 512 KiB disks |
| `z80romless-flash.bin` | Complete 4 MiB initial-provisioning image |
| `manifest.json` | Geometry, addresses, sizes, and SHA-256 values |

AUTORUN supports cold-only, warm-only, both, and disabled modes. Fresh Drive A
images disable it; settings occupy the final reserved system record. See
[native auto-run](../implementation/phase-9-flash-storage.md#native-cpm-auto-run).

For USB-only Phase 9, choose one of these targets:

| Target | Output directory | Startup |
| --- | --- | --- |
| `z80_stage09_cpm_images` | `build/cpm-stage09/` | Ctrl+] `l` loads/verifies SRAM; Ctrl+] `g` starts CP/M |
| `z80_stage09_cpm_loader_images` | `build/cpm-stage09-loader/` | Automatically loads/verifies SRAM and starts CP/M on reset |

Both produce the same artifact names with their respective Phase 9 firmware.
For firmware alone, use **Build and flash Pico - Phase 9 firmware** and
select **Automatic loader**. The manual variant’s Ctrl+] `t` disk test requires a reboot and fresh load before
CP/M. See the [Phase 9 workflows](../implementation/phase-9-flash-storage.md#controlled-usb-workflow).

Run the host regression checks with:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

### macOS console with picocom

Install a serial terminal using [Toolchain](../toolchain.md#install-the-host-tools).
With the phase firmware running (not BOOTSEL), list its serial ports:

```sh
ls /dev/cu.usbmodem*
```

Identify the port belonging to the Pico, then start the terminal, replacing
the example device name with the one listed on your computer:

```sh
picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/cu.usbmodemXXXX
```

Follow the [power requirements](../system/firmware-build.md#deployment-task-requirements)
before connecting USB. Keep `picocom` open
during console-driven tests. For Phase 1, type `s` to sample the inputs and
`w` to run the control-output walking-one test; neither command needs Enter.
Run `w` only with the required Phase 1 hardware population. To exit `picocom`,
press **Ctrl+A**, then **Ctrl+X**. Close it before programming the Pico again
or opening another terminal on the same port.

## 7.2 Flash Provisioning

!!! warning "Finish any running hardware test before provisioning"
    Building does not deploy. Do not replace firmware, open a second serial
    owner, or reset the Pico during an active soak. Disk/full-image replacement
    is destructive. This non-production development workflow creates no backups.

### Update tools

VS Code tasks use `scripts/pico_build.py` for firmware builds and
`scripts/pico_flash.py` for firmware, disk, and CP/M updates. The flashing
script uses `pico_supervisor.py` for serial discovery/control. Full-flash
provisioning tasks call `picotool` directly. See [Toolchain](../toolchain.md#python-packages-and-usb-console) for setup
and [device selection and diagnostics](#device-selection-and-diagnostics) below.

| Change | Procedure / VS Code task | Replaced data |
| --- | --- | --- |
| Firmware only | **Build and flash Pico - Phase 9/10 firmware** | Firmware; preserves CP/M, disks, journal, and ENV |
| Selected disks | [VS Code disk tasks](#system-and-storage-deployment) | Selected drives only |
| CP/M assembly | **Build and flash Pico - CP/M system and Drive A** | Matching boot package and Drive A, including its files |
| Initial provisioning / old 320 KiB layout | **Build and flash Pico - Phase 10 firmware and disks** | Entire flash, including disks and ENV |

Selective storage updates require matching `z80sbc-storage:` firmware metadata
and compatible BIOS geometry. For an existing 512 KiB installation without
metadata, update firmware first. A 320 KiB installation requires full
provisioning. Stop/flush and recover any pending journal before replacing disks.
Updating the boot package alone leaves stale system records on Drive A.

### Full provisioning

Save required disks/settings first; full provisioning replaces all 4 MiB
without backups. Use **Build and flash Pico - Phase 10 firmware and disks**,
or its Phase 9 equivalent (select Diagnostic or Automatic loader).
Follow the task’s device-selection and power prompts.

For routine updates, use the firmware-only, CP/M-system, or selected-disk tasks
above. These preserve unselected regions.

Flash offsets (picotool addresses include the `0x10000000` XIP base):

| Region | Flash offset |
| --- | --- |
| Boot package | `0x1E0000` |
| ENV (preserve during boot updates) | `0x1FE000`–`0x1FFFFF` |
| Drive A / B / C / D | `0x200000` / `0x280000` / `0x300000` / `0x380000` |

After Phase 10 joins Wi-Fi, open `http://<pico-dhcp-address>:8088/`.
Use **Ctrl-] s** for supervisor status. Disk writes persist across reboots;
full provisioning replaces them.

### Phase 10 PIO banking

**Build and flash Pico - Phase 10 firmware** now includes the board-validated
PIO banking path at 20 MHz. It writes firmware only and preserves the matching
CP/M boot package, disks and ENV. To build without flashing:

```sh
.venv-flash/bin/python scripts/pico_build.py --build-dir build \
  --target z80_stage10_websocket_terminal
```

The helper explicitly enables PIO for Phase 10 even if the build directory
previously cached it OFF. Other targets retain their software/PWM default in
the helper. Use the CMake option to enable PIO for a custom target.
Direct CMake configuration uses `Z80_ENABLE_BANKED_PIO=ON` by default; pass
`-DZ80_ENABLE_BANKED_PIO=OFF` to disable it.

See the [board verification notes](../hardware/address-interface.md#board-verification-and-recovery).

## Deployment task requirements

- Save application work and finish active tests before deployment. Tasks force
  BOOTSEL entry without a running-firmware stop/flush handshake.
- Apply external **+5 V before USB**, keep it applied during programming, and
  disconnect USB before removing +5 V. Program Phase 1 off-board initially.
- If USB BOOTSEL entry fails, hold BOOTSEL while connecting the Pico as directed
  by the phase guide. Build tasks do not access hardware.
- Flash tasks verify bytes and reboot; verification does not prove CP/M boots.
  Disk/full-flash replacement creates no backups.

### Storage update checks

Disk tasks regenerate native Drive A and convert B–D from `src/disks/source-altair/`.
They do not compile applications; update source disks first. The CP/M system
task assembles current `.asm` files without regenerating them through
`disassemble_cpm64.py`; outputs go to `build/cpm-system-update/`.

Selective updates check installed layout metadata, boot/Drive A compatibility,
and journal state before writing. A pending journal must be recovered first.
Failed writes leave BOOTSEL without automatic retry/reboot. Multi-drive writes
are not atomic: an earlier drive may already be replaced when a later one fails.

### Device selection and diagnostics

Tools select a unique Pico automatically. With several connected, specify
`--serial DEVICE_SERIAL`. Close the serial terminal before standalone
supervisor commands; flashing can close verified picocom owners but rejects
unknown owners.

| Command after `.venv-flash/bin/python scripts/pico_supervisor.py` | Purpose |
| --- | --- |
| `ports` | List devices without opening them |
| `status` / `help` | Read status or the running firmware’s command menu |
| `stop` | Stop/flush CP/M and confirm clean storage |
| `wait-boot` | Await a fresh prompt and check status; does not reset |

Supervisor commands use the Phase 9/10 USB protocol. Earlier firmware uses its
phase-specific controls. See each script’s `--help` for current options.

For custom deployments, `pico_flash.py` supports `firmware`, `system`, and
`disks`. Without `--force`, it requests a clean stop/flush. Useful options:

| Option/command | Purpose |
| --- | --- |
| `--port PORT` / `--serial SERIAL` | Restrict device selection |
| `--no-disconnect` | Refuse a busy serial port |
| `--stopped` | Use an operator-confirmed clean stop |
| `--bootsel --stopped --serial SERIAL` | Select a device already in BOOTSEL |
| `--no-reboot` | Remain in BOOTSEL after writes |
| `prepare --power-confirmed` | Stop/flush, enter BOOTSEL, and check journal without writing |
| `journal --serial SERIAL` | Read-check journal headers in BOOTSEL |

`prepare` changes device state. Investigate failed stops or journal checks;
do not bypass them. Custom disk inputs use repeated `--drive DRIVE=IMAGE`:
native images are 524,288 bytes; Altair sources are accepted for B–D, while
A requires a prepared native image matching the installed boot package.

- **Missing `serial`:** install `scripts/requirements-flash.txt` in `.venv-flash`.
- **No unique Pico:** list ports and select the intended serial number.
- **Failed stop/write:** retain diagnostics and investigate before retrying.

Host-only script checks (no Pico access):

```sh
.venv-flash/bin/python -m unittest discover -s scripts -p 'test_pico*.py' -v
.venv-flash/bin/python -m unittest discover -s src/disks -p 'test_*.py' -v
```
