# D.9 Interactive CP/M Test

Run the optimized CP/M image and board BIOS with host-backed console and disk
I/O. Requires CMake, a C/C++ compiler, Python 3.10+, `z80asm`, and `cpmtools`.

## Start in VS Code

1. Open **Run and Debug**, select **Interactive CP/M emulator**, and press **Start**.
2. Wait for `64K CP/M 2.2` and the `A>` prompt.
3. Enter CP/M commands. Press `Ctrl+]` to stop the session.

The pre-launch task builds the project’s Z80 host, I/O adapter, and images in
`build/cpm`. The launcher loads the 64 KiB payload from `z80boot.pkg` and
attaches Drives A–D. No Pico or external Altair checkout is needed.

## Try the software path

```text
A>CLEAR
A>DIR
A>LS
A>C:
C>ATTNC11
C>A:PIP D:ATTNC11.COM=C:ATTNC11.COM
C>A:PIP D:ATTN.WTS=C:ATTN.WTS
C>D:
D>DIR
```

This exercises console output, directories, transient loading and exit,
BIOS warm reload, multi-extent programs, and cross-drive writes.

Disk changes are saved to the generated files in `build/cpm`. The preparation
task regenerates them on the next launch; copy out results you want to retain.

The terminal maps arrow keys to Ctrl-E/X/D/S, Insert/Delete to Ctrl-O/G,
Page Up/Down to Ctrl-R/V, and Backspace to Ctrl-H. Standalone Escape uses a
30 ms grace period to distinguish it from ANSI key sequences.

## Optional HTTP(S) file-transfer test

The host adapter also emulates Altair file-transfer, time/timer, utility,
weather, environment-variable, and chat ports. Network features require curl
support and relevant configuration; they do not establish Pico support.

With curl development headers/libraries installed, build the host and adapter:

```sh
cmake -S src/cpm/z80_debug_host -B build/cpm/z80_debug_host \
  -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build build/cpm/z80_debug_host
cmake -S src/cpm/dcc_io_adapter -B build/cpm/dcc_io_adapter
cmake --build build/cpm/dcc_io_adapter
python3 src/cpm/test_http_ft.py --https
```

The test requires `FT.ASM` from the sibling ESP32 Altair checkout (default:
`../esp32/esp32-altair-8800/Apps/FT/FT.ASM`; override with `--ft-source`).
It assembles FT with CP/M MAC/LOAD, tests local HTTP binary/text transfers,
empty files, error recovery, and prompted filenames. `--https` also downloads
`LOVE.BAS` and runs it in MBASIC.

Artifacts, hashes, and transcripts go to `build/cpm-http-ft`; ordinary
`build/cpm` disks remain unchanged. The adapter uses the production HTTP
mailbox/parser with a native curl transport.

When using cpmtools directly, use `build_images.cpm_diskdefs()`: its explicit
`logicalextents 1` is required for files larger than 16 KiB.

These are emulator tests. Pico loading, flash journaling and power-loss
recovery, electrical behavior, and bus timing require
[hardware qualification](validation.md#required-hardware-qualification).
