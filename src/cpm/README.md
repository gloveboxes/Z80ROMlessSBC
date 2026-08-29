# Native CP/M boot image

The board runs a 64K CP/M 2.2 system generated from the preserved
`src/disks/source-altair/cpm63k.dsk`, but replaces its Altair controller BIOS.
The memory map is:

| Region | Z80 address | Size |
| --- | ---: | ---: |
| CCP | `0xE700` | 2,048 bytes |
| BDOS | `0xEF00` | 3,584 bytes |
| BIOS | `0xFD00` | 710 bytes currently |

The checked-in `cpm64_system.bin` was captured from RAM after the matching
Burcon `MOVCPM 64` and `SYSGEN` utilities produced and booted a 64K system. It
contains the 44 relocated CCP/BDOS records and is protected by a SHA-256 check
in `build_images.py`. The active port relocates page zero's BDOS entry to
`0xEF06`, and the custom BIOS jump table begins at `0xFD00`. The TPA is
`0x0100`-`0xE6FF` (58,880 bytes), 1,024 bytes larger than the initial 64K
SBC layout. All CCP, BDOS, and BIOS workspace remains inside the resident
regions above the TPA.

`disassemble_cpm64.py` recursively disassembles the exact fingerprinted image
from the CCP vectors and all indirect CCP/BDOS dispatch tables. It generates
`cpm64_i8080.asm` with Intel 8080 mnemonics and leaves unvisited strings,
tables, and workspace as byte data. See `cpm64_i8080_audit.md` for the
correctness and optimization review. The boot-proven `cpm64_system.bin` remains
immutable. The builder instead assembles the generated `cpm64_z80.asm` port.
Its unoptimized generation mode reproduces the immutable image byte-for-byte.
The active port combines relative branches, ten guarded `DJNZ` sites, `LDIR`,
native `SBC HL,DE` subtraction, `SUB 2` fusion, a branch inversion, branch
threading, tail transfers, and register-use rewrites to reclaim 263 bytes while
preserving the fixed section sizes. Discovery includes
the BDOS cleanup block reached through a synthetic stack return. Host tests
independently verify relocation and native encodings; `test_dcc_debug_host.py`
boots the optimized image, runs `DIR`, loads a real transient program to its
natural exit, and verifies that BIOS warm boot reloads CP/M and restores the
prompt through the modeled SBC ports. The DCC debug host's `FullCpmHost` model
reports a benign `exited-normally` checkpoint whenever the emulated PC
coincidentally equals `0x0000` or a bootstrap-captured address, even during
completely ordinary execution; the test resumes past it (occasionally dozens
of times, for a single coincidentally-aliased loop) rather than treating it as
a failure.

Use the matching
[Burcon CP/M 2.2 utilities](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/)
when reproducing this artifact: run that archive's `MOVCPM.COM` and
`SYSGEN.COM` as described in its
[Using MOVCPM guide](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/Using%20MOVCPM.pdf).
Burcon reserves `0x0700` bytes for its BIOS and reports `SAVE 38` after
`MOVCPM 64`. Those reference addresses remain the input to the audit and
byte-identical unoptimized mode. The active SBC port symbolically relocates the
same fixed-size sections upward by `0x0400`.

`z80_bios.asm` is assembled at `0xFD00`. It uses terminal data/status ports
`0x00`/`0x01` and disk command, drive, LBA-low, LBA-high, and data ports
`0x10`-`0x14`. Warm boot reloads 44 sequential records from native Drive A.
The source deliberately uses the Z80 instruction set, including `DJNZ`,
relative branches, rotate/carry status polling, carry-to-mask `SBC A,A`, and
`INIR`/`OTIR` block I/O; it is not restricted to the 8080-compatible subset.
`read_record` and `write_record` bank BC/DE/HL through the alternate register
set with a single `EXX` each way instead of three `PUSH`/`POP` pairs; this is
safe because this system never enables interrupts anywhere, so the shadow
registers have no other owner. `write_record`'s write-type selector is read
from `C` into `A` and stashed in memory before the `EXX`, since it is a real
input parameter (not just a value to preserve) and `A` does not survive the
following `prepare_disk_io` call. This trades 4 extra BIOS bytes (`write_record`
now stashes its command byte through memory) for roughly half the T-states of
the previous `PUSH`/`POP` sequence on every disk operation; the BIOS has ample
headroom below Burcon's `0x0700` reservation.
The BIOS uses an identity `SECTRAN` because disk conversion has already removed
the Altair skew. It forwards CP/M's standard write type to the Pico so normal
writes can be coalesced in a 4 KiB cache, while directory writes and warm boot
flush through the journal. A 250 ms idle deadline also persists an isolated
normal overwrite. `LIST`, `PUNCH`, and `READER` remain in their mandatory
jump-table positions, but no devices are implemented: printer and punch output
is discarded, reader input returns CP/M text EOF (`0x1A`), and `LISTST` reports
not ready. The existing aligned
console (`0x00`) and disk (`0x10`) port groups are retained for direct-I/O
compatibility; changing their values would not reduce the number of I/O traps.

Disk geometry lives once in `src/disks/geometry.py`. The host builder converts
it to assembler equates, assembles the BIOS, builds the reset-ready SRAM image,
adds the firmware manifest and CRCs, replaces Drive A's reserved tracks, and
optionally composes the complete 4 MiB flash image.

```sh
python3 src/cpm/build_images.py --output-dir build/cpm
```

With a sibling DCC checkout containing the extended `dcc-debug-host`:

```sh
python3 src/cpm/test_dcc_debug_host.py --dcc-root ../dcc
```

For a self-contained test that uses the vendored current DCC Z80 engine, build
the local host described in
[`dcc_debug_host/README.md`](dcc_debug_host/README.md), then pass its executable
with `--host`. The Python test extracts the SRAM payload from `z80boot.pkg`
instead of loading a separate boot-image artifact.

To interact with CP/M in the emulator, select **Interactive CP/M emulator** in
VS Code's **Run and Debug** view and press Start. Its pre-launch task builds the
local host and regenerates the CP/M images; the launcher then loads the payload
from `z80boot.pkg` and connects the integrated terminal to the emulated CP/M
console. Wait for `A>`, then type commands normally; `DIR`, `LS`, `C:`,
`ATTNC11`, and cross-drive `PIP` copies provide progressively broader checks of
CCP, BDOS, BIOS warm boot, multi-extent loading, and disk writes. `Ctrl+]`
detaches from the terminal and stops the session.

The pre-launch task builds the complete project-owned
[`dcc_io_adapter/`](dcc_io_adapter/) shared library and the debug configuration
always loads it. In addition to the SBC disk ports, it mirrors all active ports
in the ESP32 Altair `port_drivers` dispatcher for time, utility, weather,
interrupt timer, file transfer, environment, chat, and shared response data.
Its terminal pipeline translates cursor, Insert/Delete, and Page Up/Page Down
ANSI sequences to the CP/M control keys, maps Backspace/Delete to Ctrl-H, and
preserves standalone Escape after a 30 ms sequence timeout. See
[Appendix D.9](../../README.md#d9-interactive-end-to-end-cpm-user-test) for the
complete procedure, expected results, and the boundary between emulator
coverage and Pico/physical-board qualification.

The sibling `dcc-debug-host` must implement DD/FD-prefixed opcodes that don't
reference `H`/`L`/`(HL)` as executing exactly as if unprefixed (the real Z80
behavior); a build missing this aborts with `bugbug: not-implemented z80
instruction` on some historical `.COM` files. It is fixed in `x80.cxx`'s
`z80_execute_unprefixed`, alongside two related pre-existing mask bugs found
while diagnosing it (`ld r,(i+d)` and IXH/IXL math wrongly also matching ALU
immediates and register B/C forms respectively); see `z80_core_test.cpp` for
the regression coverage.

`z80boot.pkg` has a 20-byte little-endian manifest at offset zero, `0xFF`
padding through offset `0x0FFF`, and a 65,536-byte SRAM image at offset
`0x1000`. The SRAM image starts with `JP 0xFD00`; cold boot then installs the
normal CP/M warm-boot and BDOS vectors before entering the CCP.
