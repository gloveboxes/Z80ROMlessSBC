# D.8 Image Build and End-to-End Test

The deployed image assembles `cpm64_z80.asm` (CCP/BDOS) and `z80_bios.asm`
(board BIOS). The immutable Burcon binary remains the audit reference.
See [image provenance](image-provenance.md) and the [memory map](../system/cpm-execution.md#cpm-memory-map-reference).

## Build images

On Apple silicon macOS, use Apple Container and the disk images in
`src/disks/native/`. Run from the repository root:

```sh
python3 scripts/z80_container.py images --output-dir build/cpm
```

Outputs include `bios.bin`, the 64 KiB `z80boot.img`, CRC-protected
`z80boot.pkg`, `AUTORUN.COM`, four 512 KiB disk images, and `manifest.json`.
Drive A receives the native system tracks and AUTORUN utility/configuration;
other application files are preserved. Drives B, C and D are unchanged copies
of `src/disks/native/`.

To also create a complete 4 MiB `z80romless-flash.bin`, supply Phase 10 firmware:

```sh
python3 scripts/z80_container.py images --output-dir build/cpm \
  --firmware build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.bin
```

For host builds on Linux or Windows, run `src/cpm/build_images.py` with
Python 3, `z80asm` and `cpmtools`.

## Run tests

Check reference reproducibility, resident layout, BIOS ABI, package CRCs,
disk construction, and flash-region boundaries:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

Run the Z80 execution and end-to-end tests in the Apple container:

```sh
python3 scripts/z80_container.py test
```

The test builds fresh images and executes the board BIOS through emulated
console/disk ports. It checks boot to `A>`, `CLEAR`, `DIR`, `LS.COM` exit and
warm reload, multi-extent `ATTNC11.COM` loading, and cross-drive `PIP` copies.
Warm boot reloads 44 CCP/BDOS records (5,632 bytes) from Drive A.

These tests establish host/emulator behavior. Physical bus timing, Pico I/O,
flash fault recovery, and power-cycle persistence require the
[hardware qualification checks](validation.md#required-hardware-qualification).
