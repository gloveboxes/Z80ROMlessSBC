# Toolchain

Run commands from the repository root. Select **Linux**, **macOS**, or
**Windows** below; your selection stays linked across the setup sections.

## Requirements

| Tool | Purpose |
| --- | --- |
| Git | Checkout and recursive submodules |
| Complete Arm GNU Toolchain (`arm-none-eabi`) | Pico C/C++ builds, including Newlib and C++ runtimes |
| CMake, Ninja and a native C/C++ compiler | Firmware builds, SDK host tools and native tests |
| Python 3.11+ with `pip` and `venv` | Build helpers, browser terminal embedding and CP/M images |
| Pico SDK with submodules | RP2350, USB and Wi-Fi libraries |
| `picotool` | RP2350 firmware outputs and USB programming/verification |
| NonGNU `z80asm` | Required at CMake configuration for every firmware phase |
| `cpmtools` (`cpmcp`, `cpmrm`) | CP/M image generation |
| `picocom` or a Windows serial terminal | USB console |
| `pyserial`; `lsof` and `ps` on Linux/macOS | Project USB utilities and serial ownership checks |
| `z80dasm` | Optional Z80 disassembly/audit checks |
| VS Code | Optional editor and build/flash tasks |
| MkDocs packages | Documentation builds only |

`picotool` programs the Pico; `picocom` is a serial terminal. Install them separately.

## Install the host tools

=== "Linux"

    Debian/Ubuntu:

    ```sh
    sudo apt update
    sudo apt install build-essential git cmake ninja-build python3 python3-venv \
      python3-pip picocom z80asm cpmtools lsof pkg-config libusb-1.0-0-dev
    ```

    - Check `python3 --version` is 3.11+.
    - Install RP2350-capable `picotool` from the
      [SDK tools releases](https://github.com/raspberrypi/pico-sdk-tools/releases)
      or [build from source](https://github.com/raspberrypi/picotool/blob/master/BUILDING.md).
      Add it to `PATH` and follow its USB/udev setup.
    - Join your distribution's serial-device group if required for serial access.
    - For optional disassembly, run `sudo apt install z80dasm`.

=== "macOS"

    Install Apple's Command Line Tools if absent, then use [Homebrew](https://brew.sh/):

    ```sh
    xcode-select --install
    brew install git cmake ninja python picotool picocom z80asm cpmtools
    ```

    - Install the complete Arm toolchain below, including Newlib and C++ runtimes.
    - For optional disassembly, run `brew install z80dasm`.

=== "Windows"

    1. Install [Git](https://git-scm.com/downloads/win),
       [CMake](https://cmake.org/download/) and
       [Ninja](https://github.com/ninja-build/ninja/releases); enable their `Path` options.
    2. Install Visual Studio Build Tools with the **C++ desktop** workload.
       Use Developer PowerShell for SDK host tools.
    3. Install the [Python install manager](https://www.python.org/downloads/windows/),
       then a Python 3.11+ runtime (example: 3.14):

    ```powershell
    py install 3.14
    py -3.14 --version
    py -3.14 -m pip --version
    ```

    4. Install `picotool` from the
       [SDK tools releases](https://github.com/raspberrypi/pico-sdk-tools/releases).
       Keep its runtime dependencies beside `picotool.exe` and follow the
       [USB driver setup](https://github.com/raspberrypi/picotool).

    Existing Python: check `py -3 --version` and `py -3 -m pip --version`.
    For setup details, see the [Python guide](https://docs.python.org/3/using/windows.html).
    Reopen VS Code after changing `Path`.

    Run project build/flash tasks, USB scripts and CP/M image generation in
    a Linux environment. Configure USB passthrough when using virtual Linux.
    For native Windows builds, install compatible NonGNU `z80asm` on `Path`
    and use direct CMake commands. For initial programming, copy the UF2 to
    the Pico's BOOTSEL drive and follow the
    [power requirements](system/firmware-build.md#deployment-task-requirements).

## Z80 assembly and disassembly tools

- Use [NonGNU `z80asm`](https://www.nongnu.org/z80asm/) (tested: **1.8**),
  and install it before configuring any firmware phase.
- Install `cpmtools` for CP/M image generation.
- Use [`z80dasm`](https://www.tablix.org/~avian/blog/articles/z80dasm/)
  for optional binary inspection and round-trip audit checks.
- For CP/M listings, Z80 source and audit reports, run the bundled
  [`disassemble_cpm64.py`](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/disassemble_cpm64.py):
  `python3 src/cpm/disassemble_cpm64.py --help` (use `py -3` on Windows).
  It uses Python's standard library. Pass explicit output paths for review copies.
- Use `arm-none-eabi-objdump` and `arm-none-eabi-objcopy` for Pico firmware inspection.

## Install the complete Arm GNU toolchain

Use the [official Arm GNU Toolchain releases](https://gitlab.arm.com/tooling/gnu-toolchains-for-arm)
and [Arm's installation guide](https://learn.arm.com/install-guides/gcc/arm-gnu/).
Choose **`arm-none-eabi` with C and C++ support** for the Pico 2 W's Arm Cortex-M33.
Tested release: **Arm GNU Toolchain 15.3.Rel1 / GCC 15.3.1** (Phase 6 and 10 builds).

For archive installs, replace the example filename with your download and
extract into an empty directory. Use a fresh directory when changing releases.

=== "Linux"

    Download the Linux `.tar.xz`: **`x86_64`** for PCs or **`aarch64`** for 64-bit Arm hosts.

    ```sh
    arm_archive="$HOME/Downloads/arm-gnu-toolchain-RELEASE-HOST-arm-none-eabi.tar.xz"
    arm_install="$HOME/.local/share/arm-gnu-toolchain"
    mkdir -p "$arm_install"
    tar -xJf "$arm_archive" -C "$arm_install" --strip-components=1
    export PATH="$arm_install/bin:$PATH"
    ```

    Add `export PATH="$HOME/.local/share/arm-gnu-toolchain/bin:$PATH"` to your
    shell's startup file for future sessions.

=== "macOS"

    Download **`darwin-arm64`** for Apple Silicon or **`darwin-x86_64`** for Intel.

    ```sh
    arm_archive="$HOME/Downloads/arm-gnu-toolchain-RELEASE-HOST-arm-none-eabi.tar.xz"
    arm_install="$HOME/.local/share/arm-gnu-toolchain"
    mkdir -p "$arm_install"
    tar -xJf "$arm_archive" -C "$arm_install" --strip-components=1
    export PATH="$arm_install/bin:$PATH"
    ```

    Add `export PATH="$HOME/.local/share/arm-gnu-toolchain/bin:$PATH"` to `~/.zshrc`.
    Alternatively, use the `.pkg` installer and add its installed `bin` directory
    to `PATH` (often under `/Applications/ArmGNUToolchain/`).

=== "Windows"

    Use the Windows installer or ZIP for your host architecture. Keep the full
    directory tree and add its `bin` directory to your user `Path`.
    Reopen PowerShell, or set it temporarily (adjust the example path):

    ```powershell
    $armBin = 'C:\Tools\ArmGNUToolchain\bin'
    $env:Path = "$armBin;$env:Path"
    ```

### Verify the compiler and runtimes

Run these checks after updating `PATH`; the same commands work in PowerShell:

```text
arm-none-eabi-gcc --version
arm-none-eabi-g++ --version
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -print-file-name=libc.a
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -print-file-name=libg.a
arm-none-eabi-g++ -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -print-file-name=libstdc++.a
```

Confirm the last three commands print full paths to installed libraries.
Check the selected compiler with `command -v arm-none-eabi-gcc` (Linux/macOS)
or `Get-Command arm-none-eabi-gcc` (PowerShell).

## Checkout and Pico SDK

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/gloveboxes/Z80ROMlessSBC.git
cd Z80ROMlessSBC
```

Existing checkout: `git submodule update --init --recursive`.
Install the [Pico SDK](https://github.com/raspberrypi/pico-sdk) with submodules,
then configure from the project root:

=== "Linux"

    ```sh
    mkdir -p "$HOME/GitHub/pico"
    git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git \
      "$HOME/GitHub/pico/pico-sdk"
    cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w \
      -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk"
    ```

=== "macOS"

    ```sh
    mkdir -p "$HOME/GitHub/pico"
    git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git \
      "$HOME/GitHub/pico/pico-sdk"
    cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w \
      -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk"
    ```

=== "Windows"

    ```powershell
    git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git "$env:USERPROFILE\GitHub\pico\pico-sdk"
    cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w "-DPICO_SDK_PATH=$env:USERPROFILE\GitHub\pico\pico-sdk"
    ```

Set `-DPICO_SDK_PATH` to your own SDK directory. Build Phase 1, for example:

```sh
cmake --build build --target z80_stage01_supervisor -j
```

Choose the target for your hardware's current phase from the
[firmware target table](system/firmware-build.md#choose-the-image-for-your-current-phase).

## Python packages and USB console

Create `.venv-flash` and install `pyserial` (import name: `serial`) using the
commands below before running the VS Code flash tasks. The tasks activate this
environment automatically on Linux/macOS, but do not create it.
Use your Pico's actual serial port: **115200 baud, 8N1, flow control disabled**.

=== "Linux"

    ```sh
    python3 -m venv .venv-flash
    .venv-flash/bin/python -m pip install -r scripts/requirements-flash.txt
    .venv-flash/bin/python -c 'import sys, serial; print(sys.executable)'

    picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/ttyACM0
    ```

=== "macOS"

    ```sh
    python3 -m venv .venv-flash
    .venv-flash/bin/python -m pip install -r scripts/requirements-flash.txt
    .venv-flash/bin/python -c 'import sys, serial; print(sys.executable)'

    picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/cu.usbmodemXXXX
    ```

=== "Windows"

    ```powershell
    py -3 -m venv .venv-flash
    .\.venv-flash\Scripts\python.exe -m pip install -r scripts/requirements-flash.txt
    .\.venv-flash\Scripts\python.exe -c "import sys, serial; print(sys.executable)"
    ```

    Use [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html)
    with the Pico's `COM` port. Run the project USB scripts in Linux.

Exit `picocom` with **Ctrl+A**, then **Ctrl+X**.
See [USB console setup](system/firmware-build.md#macos-console-with-picocom)
and the [Pico utilities guide](system/firmware-build.md#device-selection-and-diagnostics)
for discovery, flashing and the required power sequence.

## VS Code builds and final checks

After completing [Python packages and USB console](#python-packages-and-usb-console),
on macOS/Linux choose **Terminal → Run Task → Build and flash Pico - Phase 10 firmware**
to deploy while preserving disks and ENV. `pico_build.py` checks runtimes and
searches `ARM_GNU_TOOLCHAIN_BIN`, the cached compiler, `PATH`, then
`~/.local/share/arm-gnu-toolchain*/bin`. Set an override before starting VS Code
if needed:

=== "Linux"

    ```sh
    export ARM_GNU_TOOLCHAIN_BIN="$HOME/.local/share/arm-gnu-toolchain/bin"
    ```

=== "macOS"

    ```sh
    export ARM_GNU_TOOLCHAIN_BIN="$HOME/.local/share/arm-gnu-toolchain/bin"
    ```

=== "Windows"

    Use [direct CMake commands](#checkout-and-pico-sdk) with the Arm toolchain
    on `Path`.

When switching compilers, use a new build directory or `cmake --fresh`;
pass your SDK path and keep the desired compiler first on `PATH`.

Check the installed host tools:

```text
git --version
cmake --version
ninja --version
picotool version
z80asm --version
```

For CP/M images, also check `cpmcp` and `cpmrm` are available.
If installed, check `z80dasm --help`.

Continue with [firmware build and provisioning](system/firmware-build.md),
[firmware testing](system/firmware-testing.md), or
[building the documentation](development/building-docs.md)
(packages: `docs/docs/requirements.txt`).
