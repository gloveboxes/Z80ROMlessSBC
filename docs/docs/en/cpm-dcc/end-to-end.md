# D.8 End-to-End Z80 Optimization, Image Build, and Flash Test

The active bootable system is Z80-optimized throughout its resident software:
the CCP and BDOS are emitted as `cpm64_z80.asm`, and the board BIOS is
implemented by `z80_bios.asm`. The immutable `cpm64_system.bin` is retained as
the fingerprinted Burcon reference and as the input to the translation audit;
it is not copied directly into the deployed image. The optimized CCP/BDOS port
uses Z80 relative branches, guarded `DJNZ`, block moves, native arithmetic,
tail transfers, and safe register-use rewrites. The BIOS uses the Z80
instruction set for its jump table, cold/warm boot paths, console polling, and
disk transfers, including `INIR`/`OTIR`, `SBC`, relative branches, and `EXX`.
The resulting image keeps the CP/M ABI and fixed resident boundaries while
leaving the transient program area at `0x0100`-`0xE6FF`.

Build the complete CP/M artifact set from the repository root:

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

The builder assembles the optimized CCP/BDOS and BIOS, constructs the
reset-ready 64 KiB `z80boot.img`, wraps it as `z80boot.pkg` with a manifest and
CRC, and replaces only Drive A's reserved system tracks in
`drive_a_cpm63k-z80.img`. Drives B-D remain the converted native 320 KiB
images. With a Stage 10 firmware binary, the same builder can compose
`z80romless-flash.bin`, a complete 4 MiB image:

```sh
python3 src/cpm/build_images.py --output-dir build/cpm \
  --firmware build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.bin
```

The BIOS and flash backend share one deliberately narrow contract. CP/M passes
drive, track, sector, DMA address, and write type to the BIOS; the BIOS
converts the request to one 128-byte linear-record transfer through ports
`0x10`-`0x14`. The Pico maps those records into the four exact 320 KiB flash
slots at `0x2C0000`, `0x310000`, `0x360000`, and `0x3B0000`. Normal writes can
coalesce in a 4 KiB cache, while directory and warm-boot writes are journaled;
the write type is preserved so the Pico can choose the safe persistence path.
On cold boot, the Pico recovers the journal, validates the package, copies the
image into SRAM while RESET# is held, verifies it, and releases the Z80. The
BIOS then installs the page-zero warm-boot and BDOS vectors. On a later warm
boot, the BIOS reloads the 44 CCP/BDOS records (5,632 bytes) from Drive A.
This avoids a second storage bus while keeping Z80 disk semantics and Pico
flash erase/program operations separate.

Run the reproducibility and structure tests with:

```sh
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

These tests assemble both optimized Z80 components and verify the CP/M 2.2
version byte, resident placement, BIOS jump-table ABI, boot-package header and
CRCs, exact 64 KiB and 320 KiB geometries, Drive A system-track replacement,
and every flash-region boundary. The end-to-end DCC debug-host test additionally
boots the optimized image, observes the CP/M banner and `A>` prompt, runs
`DIR`, executes `LS.COM` to its natural exit, confirms BIOS warm reload, selects
another drive, loads a multi-extent transient, and copies files across drives.
After building the project-owned `z80-debug-host`, run the end-to-end test with:

```sh
python3 src/cpm/test_z80_debug_host.py \
  --host build/cpm/z80_debug_host/z80-debug-host
```

This is host/emulator evidence, not a claim of completed electrical
qualification. Physical proof still requires the [Phase 8](../implementation/phase-8-virtual-io.md),
[Phase 9](../implementation/phase-9-flash-storage.md), and
[Phase 10](../implementation/phase-10-websocket.md) bus, flash fault-injection,
power-cycle recovery, and logic-analyzer tests listed in the
[required qualification](validation.md) and
[implementation plan](../implementation/index.md).
