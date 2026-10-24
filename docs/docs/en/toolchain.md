# Toolchain

Install these tools before building the Pico firmware. The Arm compiler builds
the Pico application; a separate Z80 assembler builds the programs that run on
the Z80. Python runs the build helpers, embeds the browser terminal in firmware,
and prepares CP/M disk images.

Run project commands from the repository root. The checked-in VS Code tasks
use macOS/Linux shell syntax. Native Windows setup and its current limitations
are described below.

## Requirements

| Tool | Used for | When required |
| --- | --- | --- |
| Git | Checkout, Pico SDK and recursive submodules | All firmware builds |
| Complete Arm GNU Toolchain (`arm-none-eabi`) | Pico C/C++ compilation, linking and binary utilities; includes Newlib and the C++ runtime | All firmware builds |
| CMake and Ninja | Configure and build the firmware | All firmware builds |
| Native C/C++ compiler | Build host tools such as `pioasm`; run native firmware tests | SDK tools and host tests |
| Python 3.11 or newer, with `pip` and `venv` | Build scripts, embedded HTML, CP/M image generation and USB utilities | All firmware builds |
| Raspberry Pi Pico SDK, including submodules | RP2350, USB and Pico 2 W Wi-Fi libraries | All firmware builds |
| `picotool` | Generate RP2350 firmware outputs and program/verify the Pico over USB | Firmware builds and flashing |
| `z80asm` | Assemble Z80 diagnostics, BIOS, CCP/BDOS and AUTORUN | Top-level firmware CMake configuration, Phase 6 scope/soak targets and CP/M images |
| `z80dasm` | Independent Z80 binary disassembly and assembly/disassembly checks | Optional inspection and CP/M audit work |
| Bundled `disassemble_cpm64.py` | Disassemble/audit the preserved CP/M image and generate its Z80 source port | CP/M source regeneration/audit; Python standard library only |
| `cpmtools` (`cpmcp`, `cpmrm`) | Install native CP/M programs into disk images | CP/M image generation |
| `picocom`, or a Windows serial terminal | USB console and supervisor commands | Hardware bring-up and USB console use |
| `pyserial` | Discover and communicate with USB serial devices from Python | Project USB utilities |
| `lsof` and `ps` | Check serial ownership and release `picocom` before flashing | Project flash/supervisor scripts on macOS/Linux |
| VS Code | Editor and convenient build/flash tasks | Optional |
| MkDocs and the project's documentation packages | Build or preview this website | Documentation work only |

`picotool` is Raspberry Pi's device/binary utility. `picocom` is a serial
terminal. They serve different purposes; installing one does not install the
other. The utility's command is **`picotool`**, singular.

## Install the host tools

### macOS

Install Apple's Command Line Tools if absent, then install the host packages
using [Homebrew](https://brew.sh/):

```sh
xcode-select --install
brew install git cmake ninja python picotool picocom z80asm cpmtools
```

The Command Line Tools supply the native Clang compiler and developer tools.
Install the complete Arm compiler separately using the next section. The
Homebrew **`arm-none-eabi-gcc` formula alone** lacks the Newlib runtime required
by this project; the project has encountered missing `-lc` and `-lg` link
errors with that installation.

### Linux (Debian/Ubuntu)

Install the host tools and USB development library:

```sh
sudo apt update
sudo apt install build-essential git cmake ninja-build python3 python3-venv \
  python3-pip picocom z80asm cpmtools lsof pkg-config libusb-1.0-0-dev
```

Check `python3 --version` is at least 3.11. Install the complete Arm toolchain
below, then install a current RP2350-capable `picotool` from Raspberry Pi's
[prebuilt SDK tools](https://github.com/raspberrypi/pico-sdk-tools/releases),
or follow its [source build instructions](https://github.com/raspberrypi/picotool/blob/master/BUILDING.md).
Put the directory containing `picotool` on `PATH`. Follow those instructions for
Linux USB permissions/udev rules as well; serial access may also require your
distribution's serial-device group membership.

### Windows

Install [Git for Windows](https://git-scm.com/downloads/win),
[CMake](https://cmake.org/download/),
[Ninja](https://github.com/ninja-build/ninja/releases), and the complete Arm
toolchain described below. Enable the installers' `PATH` options, or add the
directories containing their executables to your user `Path`. Install a native
C/C++ build environment, such as Visual Studio Build Tools with its C++ desktop
workload, for SDK host tools. Use its Developer PowerShell when those tools
need the native compiler.

Windows needs a Python installation in addition to the Arm compiler. Install
the [Python install manager from Python.org](https://www.python.org/downloads/windows/),
then install a runtime and check it in a new PowerShell window. The example
uses Python 3.14; the project requires 3.11 or newer:

```powershell
py install 3.14
py -3.14 --version
py -3.14 -m pip --version
```

For an existing Python installation, `py -3 --version` and `py -3 -m pip
--version` check the selected interpreter. Follow the
[official Windows Python guide](https://docs.python.org/3/using/windows.html)
if `py` or `python` is not found; reopen VS Code after changing `Path`.

Install `picotool` from Raspberry Pi's
[SDK tools releases](https://github.com/raspberrypi/pico-sdk-tools/releases).
Make `picotool.exe` and its supplied runtime dependencies available together,
and follow the [picotool documentation](https://github.com/raspberrypi/picotool)
for Windows USB driver setup. Use a Windows serial terminal such as
[PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html) for the
Pico's `COM` port; `picocom` is used in the macOS/Linux examples.

The checked-in flash tasks and `pico_flash.py`/`pico_supervisor.py` workflows
currently depend on POSIX shell commands, `lsof` and `ps`. Installing Python
does not make those workflows native Windows compatible. The automatic
toolchain checks in `pico_build.py` also currently use Unix executable names.
With a compatible `z80asm` also available on `PATH`, use direct CMake commands
for native Windows firmware builds, and the
[manual BOOTSEL/UF2 procedure](system/firmware-build.md#load-phase-firmware-and-open-its-console)
for initial programming. Use a Linux environment for the documented
`z80asm`/`cpmtools` CP/M image workflow; USB access from a virtual Linux
environment needs separate configuration.

## Z80 assembly and disassembly tools

The project's external assembler is
[NonGNU `z80asm`](https://www.nongnu.org/z80asm/). The tested installation
reports **Z80 assembler version 1.8**. `build_images.py` invokes it with
`z80asm -o <binary> <source>`; CMake also invokes it for the Phase 6 scope/soak
and Phase 9/10 disk-diagnostic programs. The checked-in source uses this
assembler's syntax and directives. Other assemblers, including the separate
z88dk assembler, should not be substituted solely because their executable
names look similar.

The top-level CMake project includes the Phase 9/10 definitions during
configuration. Their disk-diagnostic generation requires `z80asm`, so install
it before configuring even when your selected build target is an earlier
phase. `cpmtools` is additionally required when generating CP/M disk images.

[`z80dasm`](https://www.tablix.org/~avian/blog/articles/z80dasm/) is an optional
independent Z80 disassembler, used for the disassembly/assembly round-trip
check recorded in the CP/M audit. The current firmware build and Python image
scripts do not invoke it automatically. Add it when inspecting binaries or
repeating those independent checks:

```sh
# macOS
brew install z80asm z80dasm

# Debian/Ubuntu
sudo apt install z80asm z80dasm
```

Check the installed tools:

```sh
z80asm --version
z80dasm --help
```

The repository also includes
[`src/cpm/disassemble_cpm64.py`](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/disassemble_cpm64.py).
This Python tool decodes the preserved Burcon CP/M image, emits an Intel 8080
listing and a Z80 source port, and generates the audit report. It uses Python's
standard library and does not require a disassembler package from `pip` or
the `z80dasm` executable. Its default outputs are checked-in source/report
files; pass output paths when generating a separate review copy. Inspect its
options with:

```sh
python3 src/cpm/disassemble_cpm64.py --help
```

The `arm-none-eabi-objdump` and `arm-none-eabi-objcopy` utilities are supplied
with the Arm toolchain. Pico SDK builds use them to produce listings and
binary outputs for the Pico firmware. Use `z80dasm` for inspecting Z80 code.

## Install the complete Arm GNU toolchain

Use the [official Arm GNU Toolchain releases](https://gitlab.arm.com/tooling/gnu-toolchains-for-arm)
and [Arm's installation guide](https://learn.arm.com/install-guides/gcc/arm-gnu/).
Select the **`arm-none-eabi` bare-metal target**, with both C and C++ support.
Match the download's **host** to the computer doing the build:

| Build computer | Host package to select | Target |
| --- | --- | --- |
| Apple Silicon Mac | `darwin-arm64` | `arm-none-eabi` |
| Intel Mac | `darwin-x86_64` | `arm-none-eabi` |
| Linux PC | `x86_64` Linux | `arm-none-eabi` |
| 64-bit Linux on Raspberry Pi/Arm | `aarch64` Linux | `arm-none-eabi` |
| Windows PC | Windows installer/archive for the supported host architecture | `arm-none-eabi` |

The host architecture does not change the target: the Pico 2 W build uses
RP2350 Arm Cortex-M33 code. Select `arm-none-eabi` rather than the Linux-target
or AArch64-target toolchains. Arm GNU Toolchain 15.3.Rel1 / GCC 15.3.1 has passed
the project's Phase 6 and Phase 10 builds; the VS Code tasks do not pin that
release.

### macOS and Linux archive installation

1. Download the matching `.tar.xz` archive from the release page.
2. Extract it into an empty toolchain directory. The example below uses a
   stable directory name so your shell configuration need not contain a
   release number.
3. Add its `bin` directory to `PATH` and verify the runtime libraries.

Replace the archive path with the actual downloaded filename:

```sh
arm_archive="$HOME/Downloads/arm-gnu-toolchain-RELEASE-HOST-arm-none-eabi.tar.xz"
arm_install="$HOME/.local/share/arm-gnu-toolchain"
mkdir -p "$arm_install"
tar -xJf "$arm_archive" -C "$arm_install" --strip-components=1
export PATH="$arm_install/bin:$PATH"
```

Use a fresh directory when installing a different release to avoid mixing
toolchains. A versioned name such as `arm-gnu-toolchain-<release>` also works.
To keep the example installation on `PATH`, add this line to `~/.zshrc` on
macOS or your Linux shell's startup file, then open a new terminal:

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain/bin:$PATH"
```

The macOS `.pkg` installer is an alternative. If you use it, add its installed
`bin` directory to `PATH`; it may be under `/Applications/ArmGNUToolchain/`.

### Windows installer or ZIP installation

Run Arm's Windows installer, or extract the Windows ZIP archive into a chosen
directory. Keep the entire directory tree, including the libraries beside
`bin`. Add the directory containing `arm-none-eabi-gcc.exe` to your user
`Path`, then open a new PowerShell window. For a temporary session, replace
the example below with the actual installation directory:

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

The last three commands must print paths to installed library files. An output
of just `libc.a`, `libg.a` or `libstdc++.a` means the selected compiler cannot
find that runtime. Check which compiler is selected with
`command -v arm-none-eabi-gcc` on macOS/Linux, or
`Get-Command arm-none-eabi-gcc` in PowerShell.

## Checkout and Pico SDK

Clone the project and its WebSocket library submodule:

```sh
git clone --recurse-submodules https://github.com/gloveboxes/Z80ROMlessSBC.git
cd Z80ROMlessSBC
```

For an existing checkout, use `git submodule update --init --recursive`.
Install a complete [Pico SDK](https://github.com/raspberrypi/pico-sdk), including
its submodules, if one is not already installed. For macOS/Linux:

```sh
mkdir -p "$HOME/GitHub/pico"
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git \
  "$HOME/GitHub/pico/pico-sdk"
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$HOME/GitHub/pico/pico-sdk"
```

For native Windows, run the equivalent from the project root in PowerShell:

```powershell
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git "$env:USERPROFILE\GitHub\pico\pico-sdk"
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w "-DPICO_SDK_PATH=$env:USERPROFILE\GitHub\pico\pico-sdk"
```

Reuse an existing SDK by passing its directory to `-DPICO_SDK_PATH`; the
repository's default path belongs to the author's machine. Configure with
your own path explicitly. For example, build the first-phase firmware with:

```sh
cmake --build build --target z80_stage01_supervisor -j
```

Choose the target matching your hardware's current phase using the
[firmware target table](system/firmware-build.md#choose-the-image-for-your-current-phase).

## Python packages and USB console

Create a separate Python environment for the project USB utilities. On
macOS/Linux, run:

```sh
python3 -m venv .venv-flash
.venv-flash/bin/python -m pip install -r scripts/requirements-flash.txt
.venv-flash/bin/python -c 'import sys, serial; print(sys.executable)'
```

The dependency is `pyserial`; its Python import name is `serial`. The VS Code
flash tasks activate this environment. Native Windows uses a different
environment layout; for Python dependencies and serial-library checks:

```powershell
py -3 -m venv .venv-flash
.\.venv-flash\Scripts\python.exe -m pip install -r scripts/requirements-flash.txt
.\.venv-flash\Scripts\python.exe -c "import sys, serial; print(sys.executable)"
```

For a USB terminal, select 115200 baud, 8 data bits, no parity, one stop bit
and no flow control. Identify the Pico's actual serial port first. A macOS
example is:

```sh
picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/cu.usbmodemXXXX
```

Linux commonly uses `/dev/ttyACM0`; Windows uses a `COM` port. Replace these
examples with your discovered port. Exit `picocom` with **Ctrl+A**, then
**Ctrl+X**. See [USB console setup](system/firmware-build.md#macos-console-with-picocom)
and the [Pico utilities guide](tools/pico-tools.md) for device discovery,
flashing and the board's required power sequence.

## VS Code builds and final checks

On macOS/Linux, choose **Terminal → Run Task → Build and flash Pico - Phase 10 firmware**
to build and deploy Phase 10 firmware. Use **Build and flash Pico - Phase 10 firmware and disks**
for a complete installation, replacing disks and ENV as well. These tasks use
`scripts/pico_build.py` for their build step. It searches an explicit `ARM_GNU_TOOLCHAIN_BIN`
override, the build's cached compiler, complete toolchains on `PATH`, then
`~/.local/share/arm-gnu-toolchain*/bin`. It checks the runtimes before building.
For an installation outside these locations, set the override before starting
VS Code, for example:

```sh
export ARM_GNU_TOOLCHAIN_BIN="$HOME/.local/share/arm-gnu-toolchain/bin"
```

An existing CMake build keeps its selected compiler. Use a new build directory
when switching toolchains. An explicit `cmake --fresh` configure also resets
the cache; pass your SDK path and keep the desired compiler first on `PATH`.

Check the host tools without a Pico attached:

```text
git --version
cmake --version
ninja --version
picotool version
```

Also check `z80asm --version` before configuring firmware. For CP/M image
generation, check that `cpmcp` and `cpmrm` are available. For independent binary
inspection, check the optional `z80dasm --help` (its header includes the version).
No attached Pico is needed
to build firmware.

Continue with [firmware build and provisioning](system/firmware-build.md),
[firmware testing](system/firmware-testing.md), or
[building the documentation](development/building-docs.md). Documentation
dependencies are listed separately in `docs/docs/requirements.txt`.
