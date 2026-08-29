# Local Z80 CP/M debug host

This directory contains the standalone `z80-debug-host` sources used by the
CP/M end-to-end test and interactive debugger. Its debugger protocol and Z80
engine derive from DCC, but the project-specific host and process use the Z80
name. The Z80 engine files are direct copies from the sibling DCC checkout:

- `x80.cxx`
- `x80.hxx`
- `z80.h`
- `cpu_x80_adapter.cpp`

The engine is kept in this project so the test does not silently use a
different installed emulator. Update these files only by copying the matching
current versions from `dcc/src/dcc_debug_host/` and verify them with `cmp`.
The host uses the DCC Z80 engine for instruction execution, while the project
adapter supplies the SBC console and ports `0x10`-`0x14` backed by the generated
320 KiB disk images.

Build it from the repository root:

```sh
cmake -S src/cpm/z80_debug_host -B build/cpm/z80_debug_host \
  -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
cmake --build build/cpm/z80_debug_host
```

Run the project end-to-end test with Python 3.10 or newer:

```sh
python3 src/cpm/test_z80_debug_host.py \
  --host build/cpm/z80_debug_host/z80-debug-host \
  --assembler z80asm
```

The test builds a fresh CP/M image set, extracts and loads the 64 KiB payload
from `z80boot.pkg`, attaches the project disk adapter, and exercises cold boot,
`DIR`, transient execution, BIOS warm boot, multi-extent loading, and a
cross-drive copy. The test host is an emulator, not electrical qualification
of the physical Pico/Z80 bus.
