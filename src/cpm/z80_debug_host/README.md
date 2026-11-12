# Local Z80 CP/M debug host

This directory contains the standalone `z80-debug-host` sources used by the
CP/M end-to-end test and interactive debugger. Its debugger protocol and Z80
engine derive from DCC, but the project-specific host and process use the Z80
name. The Z80 engine files come from the sibling DCC checkout:

- `x80.cxx`
- `x80.hxx`
- `z80.h`

`x80.hxx` and `z80.h` are direct copies. `x80.cxx` additionally counts
documented Z80 T-states; [timing/README.md](timing/README.md) describes that
accounting, the evidence for it and how it differs from the DCC copy.

## Undocumented behavior

`x80.cxx` also carries the undocumented Z80 behavior of the ntvcm engine, which
the DCC copy lacks. `emulator_z80_test.py` checks each item against an
independent reference:

- `IN r,(C)` and `OUT (C),r` (`ED 40`-`ED 79`) go through the port hooks with
  `B:C` as the address; `ED 70` sets only the flags and `ED 71` writes 0.
- Undefined `ED` opcodes are two `NOP`s.
- Repeated or mixed `DD`/`FD` prefixes: the last one selects `IX` or `IY`, and
  each extra prefix is one more fetch. Memory holding only prefixes stops the
  machine.
- `DD CB d` and `FD CB d` with a register in the low three bits operate on
  `(IX+d)` and copy the result to that register; `BIT` ignores the register.
- `INI`, `IND`, `OUTI`, `OUTD` and their repeating forms set every flag from the
  byte moved, `B` and `L` or `C`. The port address carries `B` before the
  decrement for input and after it for output.
- A block-instruction pass that repeats takes `Y` and `X` from bits 13 and 11
  of the instruction's address; for `INIR`, `INDR`, `OTIR` and `OTDR` it also
  adjusts `H` and P/V.
- `BIT b,(HL)` takes `Y` and `X` from MEMPTR and leaves MEMPTR unchanged.
- `R` counts opcode fetches: one for an unprefixed instruction, two for `CB`,
  `ED`, `DD`, `FD`, `DD CB d` and `FD CB d` forms, one more for each extra
  prefix, and two for each pass of a repeating block instruction.
- `LD A,R` returns bit 7 as last written by `LD R,A`.

`SCF` and `CCF` follow the NEC rule (`Y` and `X` from `A`); the Zilog rule that
ntvcm offers behind `Z80_ZILOG_SCF_CCF` is not implemented.

The engine is kept in this project so the test does not silently use a
different installed emulator. Update `x80.hxx` and `z80.h` only by copying the
matching current versions from `dcc/src/dcc_debug_host/` and verify them with
`cmp`. When taking a newer `x80.cxx` from DCC, carry the T-state accounting
across and rerun `src/common/tests/emulator_z80_test.py`.
`cpu_x80_adapter.cpp` is the project-specific bridge around that engine. It
adds the native SBC console/disk routing and loader activation on port 21.
Activation is sticky until CPU reset so the FT commands on port 21 do not
switch back to Altair I/O routing.
The host uses the DCC Z80 engine for instruction execution, while the project
adapter supplies the SBC console and ports `0x10`-`0x14` backed by the generated
512 KiB disk images.

Build it from the repository root:

```sh
cmake -S src/cpm/z80_debug_host -B build/cpm/z80_debug_host \
  -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF
cmake --build build/cpm/z80_debug_host
```

`--bank1-image FILE` loads a 64 KiB bank-1 image (built by
`build_images.build_bank1_image`) and makes bank 1 available, as the firmware
does after loading it; with `--direct-loader` the payload then runs banked.
Without it, port 7 reports no bank 1 and bank selection is ignored. The MI
debugger reads and writes only the active bank.

Run the project end-to-end test with Python 3.10 or newer:

```sh
python3 src/cpm/test_z80_debug_host.py \
  --host build/cpm/z80_debug_host/z80-debug-host \
  --assembler z80asm
```

The test builds a fresh CP/M image set, extracts and loads the 64 KiB payload
from `z80boot.pkg`, attaches the project disk adapter, and exercises cold boot,
`DIR`, transient execution, BIOS warm boot, multi-extent loading, a
cross-drive copy, and a bank-1 allocate/write/read/free round trip. The test host is an emulator, not electrical qualification
of the physical Pico/Z80 bus.
