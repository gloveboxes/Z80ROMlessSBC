# D.8 Image Build and End-to-End Test

The deployed image assembles `cpm64_z80.asm` (CCP/BDOS) and `z80_bios.asm`
(board BIOS). The immutable Burcon binary remains the audit reference.
See [image provenance](image-provenance.md) and the [memory map](memory-map.md).

## Build images

Requires Python 3, `z80asm`, `cpmtools`, and the converted disks in
`src/disks/generated/`. Run from the repository root:

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

Outputs include `bios.bin`, the 64 KiB `z80boot.img`, CRC-protected
`z80boot.pkg`, `AUTORUN.COM`, four 512 KiB disk images, and `manifest.json`.
Drive A receives the native system tracks and AUTORUN utility/configuration;
Drives B–D are unchanged copies of the converted images.

To also create a complete 4 MiB `z80romless-flash.bin`, supply Phase 10 firmware:

```sh
python3 src/cpm/build_images.py --output-dir build/cpm \
  --firmware build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.bin
```

## Run tests

Check reference reproducibility, resident layout, BIOS ABI, package CRCs,
disk construction, and flash-region boundaries:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

Build the project-owned Z80 host, then run the end-to-end test with Python
3.10 or newer:

```sh
cmake -S src/cpm/z80_debug_host -B build/cpm/z80_debug_host \
  -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build build/cpm/z80_debug_host
python3 src/cpm/test_z80_debug_host.py \
  --host build/cpm/z80_debug_host/z80-debug-host
```

The test builds fresh images and executes the board BIOS through emulated
console/disk ports. It checks boot to `A>`, `CLEAR`, `DIR`, `LS.COM` exit and
warm reload, multi-extent `ATTNC11.COM` loading, and cross-drive `PIP` copies.
Warm boot reloads 44 CCP/BDOS records (5,632 bytes) from Drive A.

These tests establish host/emulator behavior. Physical bus timing, Pico I/O,
flash fault recovery, and power-cycle persistence require the
[hardware qualification checks](validation.md#required-hardware-qualification).
