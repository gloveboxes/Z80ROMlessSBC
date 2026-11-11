# Z80 Engine T-state Accounting

`sbc_run()` in [sbc_emulator.cpp](../sbc_emulator.cpp) reports how many ticks
the emulated program consumed. A tick is one documented Z80 T-state. The count
is produced by `z80_tstates()` in [x80.cxx](../x80.cxx), and every cycle budget
and benchmark in `src/common/tests` depends on it.

This folder holds the description of that accounting, the evidence for each
way it differs from the DCC copy of the engine, and the tools that reproduce
the evidence:

| File | Purpose |
| --- | --- |
| [check_timing.py](check_timing.py) | Steps every instruction form through the loaded engine; compares the counts with a saved dump or with published timing tables. |
| [dcc_copy_counts.json](dcc_copy_counts.json) | Counts reported by an engine built from the DCC copy of `x80.cxx` (DCC commit `61a13e4c`). |

## What is counted

- One instruction at a time, decided from the CPU state **before** the
  instruction executes (flags, `B`, `BC`, `A` and `(HL)`).
- Instruction execution only. Memory wait states, Pico I/O trap service time
  and interrupt acknowledge cycles are not modeled.
- Only on the budgeted path, `x80_emulate()`, which `sbc_run()` calls once per
  instruction. `x80_emulate_instructions()`, the batch path the debug host
  uses, does not compute timing.
- An opcode the engine cannot execute stops the machine. This includes the
  documented `IN r,(C)` and `OUT (C),r` (`ED 40`-`ED 79`) and 180 undocumented
  `ED` encodings; no timing claim is made for them.

## Timing rules

`z80_main_tstates()` returns the unprefixed timing; `z80_tstates()` handles
prefixes and the state-dependent forms.

| Instruction form | T-states |
| --- | ---: |
| `DJNZ` | 13 taken, 8 not taken |
| `JR e` / `JR cc,e` | 12 / 12 taken, 7 not taken |
| `JP cc,nn` | 10 either way |
| `CALL cc,nn` | 17 taken, 10 not taken |
| `RET cc` | 11 taken, 5 not taken |
| `CB` register / `BIT b,(HL)` / other `(HL)` | 8 / 12 / 15 |
| `ED` block step (`LDI`, `CPI`, `INI`, `OUTI` and decrementing forms) | 16 |
| `ED` block repeat (`LDIR`, `CPIR`, `INIR`, `OTIR` and decrementing forms) | 21 per repeating iteration, 16 for the last |
| `ADC HL,rp`, `SBC HL,rp` | 15 |
| `LD (nn),rp`, `LD rp,(nn)` (`ED` forms) | 20 |
| `NEG`, `IM n` / `RETN`, `RETI` | 8 / 14 |
| `LD I,A`, `LD R,A`, `LD A,I`, `LD A,R` / `RRD`, `RLD` | 9 / 18 |
| `(IX+d)` operand: `LD`, ALU, `LD (IX+d),n` | 19 |
| `INC (IX+d)`, `DEC (IX+d)`, `EX (SP),IX` | 23 |
| `DD CB d`: `BIT` / all others | 20 / 23 |
| `ADD IX,rp`, `PUSH IX` / `LD IX,nn`, `POP IX` | 15 / 14 |
| `LD (nn),IX`, `LD IX,(nn)` / `INC IX`, `DEC IX`, `LD SP,IX` | 20 / 10 |
| Any other `DD`/`FD`-prefixed instruction | unprefixed timing + 4 |
| A `DD`/`FD` prefix overridden by `DD`, `FD` or `ED` | 4 for the prefix |

`IY` forms match `IX`. A repeating block instruction is one iteration per
step: `CPIR`/`CPDR` end on a match or `BC` = 1, the I/O forms on `B` = 1, and
`LDIR`/`LDDR` on `BC` = 1. The remaining unprefixed opcodes use the Zilog
figures, for example `LD HL,(nn)` 16, `LD SP,HL` 6 and `EX AF,AF'` 4.

## Difference from the DCC copy

`x80.hxx` and `z80.h` are identical to the DCC copy. `x80.cxx` differs from
`dcc/src/dcc_debug_host/x80.cxx` only in this accounting:

- **Only in the DCC copy:** the `z80_cycles` opcode table, the `cyclesnt`
  adjustment, and `cycles` bookkeeping inside `z80_emulate()` and
  `z80_execute_unprefixed()`. Here both functions return `void`.
- **Only here:** `z80_main_tstates()` and `z80_tstates()`, called from the
  budgeted loop in `x80_emulate_budget()` before each opcode fetch.

Instruction semantics, flags and the disassembler are identical. Copying
`x80.cxx` from DCC replaces this accounting with the DCC copy's; the
`Z80Timing` tests below fail when that happens.

## Evidence

### Sources

| Key | Source |
| --- | --- |
| UM | Zilog, [*Z80 CPU User Manual*, UM008011-0816](http://www.z80.info/zip/z80cpu_um.pdf) (PDF, 332 pages, © 2016 Zilog, Inc.). Page numbers are the manual's printed pages; each figure is its "M Cycles / T States" line with the per-M-cycle breakdown. |
| CH | ClrHome [*Z80 Opcode Table*](https://clrhome.org/table/), fetched 2026-11-11. Lists documented and undocumented encodings with cycle counts. |
| ZI | [z80time.txt](http://www.z80.info/z80time.txt), Oscar Lindberg, 1996, collected from two printed Z80 references. |
| UD | Sean Young, [*The Undocumented Z80 Documented*](http://www.z80.info/zip/z80-documented.pdf), v0.91, 2005. |

None of these files is stored in the repository. The manual link is the
z80.info copy that was read; Zilog's own site redirects to Littelfuse.

### Method

`check_timing.py` steps each of 1,780 opcode encodings once with its condition
taken (or block instruction repeating) and once not taken: every unprefixed,
`CB`, `ED`, `DD`, `FD`, `DD CB d` and `FD CB d` encoding, 3,560 cases.

- **Engine against the DCC copy.** The same cases were stepped through an
  engine built from the DCC copy of `x80.cxx` and saved as
  `dcc_copy_counts.json`. 1,243 encodings report a different count in at least
  one case: 25 unprefixed, 164 `CB`, 50 `ED`, 246 each for `DD` and `FD`, and
  all 256 each for `DD CB d` and `FD CB d`. 194 `ED` encodings are not executed
  by either build.
- **Engine against CH.** 1,410 table entries: 1,394 compared, 16 not executed
  by the engine (`IN r,(C)`, `OUT (C),r` and the undocumented `ED 70`,
  `ED 71`), **0 differ**. This covers the undocumented `IXH`/`IXL`/`IYH`/`IYL`
  and `DD CB d` register forms that UM does not list.
- **Engine against ZI.** 319 entries: 305 compared, 14 not executed
  (`IN r,(C)`, `OUT (C),r`), **1 differs**: ZI lists `LD HL,(NN)` (`2A`) as
  20, where UM p. 102 and CH give 16. 20 is the figure for the `ED`-prefixed
  `LD dd,(nn)` (UM p. 103).

### Each difference

"Probe" is the instruction bytes and state stepped. "DCC" and "Here" are the
counts the two builds report for that probe. The last column names the
regression test in `Z80Timing` that asserts the value: **U**
`test_unprefixed_instructions`, **B** `test_bit_instructions`, **E**
`test_extended_instructions`, **R** `test_block_repeat_instructions`, **I**
`test_index_instructions`, **P**
`test_index_prefix_adds_one_fetch_to_other_instructions`.

| Instruction form | Probe | DCC | Here | Reference | Test |
| --- | --- | ---: | ---: | --- | --- |
| Block repeat, repeating | `ED B0`, `BC`=0210h | 5 | 21 | UM p. 132 `LDIR`: 5 M cycles, 21 (4,4,3,5,5). Same for `LDDR` p. 136, `CPIR` p. 139, `CPDR` p. 142. `INIR` p. 300, `INDR` p. 304, `OTIR` p. 311, `OTDR` p. 315: 21 (4,5,3,4,5) | R |
| Block repeat, last iteration | `ED B0`, `BC`=0001h | 4 | 16 | Same pages: 4 M cycles, 16 (4,4,3,5); I/O forms 16 (4,5,3,4) | R |
| Block single step | `ED A0` | 4 | 16 | UM `LDI` p. 130, `LDD` p. 134, `CPI` p. 138, `CPD` p. 141: 16 (4,4,3,5). `INI` p. 298, `IND` p. 302, `OUTI` p. 309, `OUTD` p. 313: 16 (4,5,3,4) | E |
| `DJNZ` taken / not taken | `10 e`, `B`=2 / `B`=1 | 3 / 2 | 13 / 8 | UM p. 278: 13 (5,3,5) / 8 (5,3) | U |
| `JR e` | `18 e` | 3 | 12 | UM p. 265: 12 (4,3,5) | U |
| `JR cc,e` taken / not taken | `20 e`, Z clear / set | 3 / 2 | 12 / 7 | UM pp. 267-273: 12 (4,3,5) / 7 (4,3) | U |
| `JP cc,nn` not taken | `C2 nn`, Z set | 4 | 10 | UM p. 264: 10 (4,3,3), one figure for both outcomes | U |
| `CALL cc,nn` not taken | `C4 nn`, Z set | 11 | 10 | UM p. 284: 10 (4,3,3) when cc is false | U |
| `LD HL,(nn)` | `2A nn` | 20 | 16 | UM p. 102: 16 (4,3,3,3,3) | U |
| `LD SP,HL` | `F9` | 5 | 6 | UM p. 112: 1 M cycle, 6 | U |
| `EX AF,AF'` | `08` | 8 | 4 | UM p. 125: 1 M cycle, 4 | U |
| `SLA r`, `SRA r`, `BIT b,r`, `RES b,r` | `CB 20`, `CB 28`, `CB 40`, `CB 80` | 3 | 8 | UM `SLA` p. 231, `SRA` p. 234, `BIT` p. 243, `RES` p. 260: 8 (4,4) | B |
| `SRL r` | `CB 38` | 4 | 8 | UM p. 237: 8 (4,4) | B |
| `SLL r` (undocumented) | `CB 30` | 4 | 8 | CH: 8 | B |
| `BIT b,(HL)` | `CB 46` | 7 | 12 | UM p. 245: 12 (4,4,4) | B |
| `RLC`, `RRC`, `RL`, `RR`, `RES b` on `(HL)` | `CB 06`, `CB 0E`, `CB 16`, `CB 1E`, `CB 86` | 10 | 15 | UM `RLC` p. 215, `RL` p. 222, `RRC` p. 226, `RR` p. 229, `RES` p. 260: 15 (4,4,4,3) | B |
| `SLA (HL)`, `SRA (HL)` | `CB 26`, `CB 2E` | 5 | 15 | UM p. 231, p. 234: 15 (4,4,4,3) | B |
| `SRL (HL)` | `CB 3E` | 4 | 15 | UM p. 237: 15 (4,4,4,3) | B |
| `SLL (HL)` (undocumented) | `CB 36` | 4 | 15 | CH: 15 | B |
| `LD (nn),dd`, `LD dd,(nn)` | `ED 43 nn`, `ED 4B nn` | 8 | 20 | UM p. 108, p. 103: 20 (4,4,3,3,3,3) | E |
| `ADC HL,ss` | `ED 4A` | 4 | 15 | UM p. 190: 15 (4,4,4,3) | E |
| `RETN`, `RETI` | `ED 45`, `ED 4D` | 4 | 14 | UM p. 290, p. 288: 14 (4,4,3,3) | E |
| `IM 0`, `IM 1`, `IM 2` | `ED 46`, `ED 56`, `ED 5E` | 4 | 8 | UM pp. 184-186: 8 (4,4) | E |
| `LD I,A`, `LD R,A`, `LD A,I`, `LD A,R` | `ED 47`, `ED 4F`, `ED 57`, `ED 5F` | 4 | 9 | UM pp. 94-97: 9 (4,5) | E |
| `RRD`, `RLD` | `ED 67`, `ED 6F` | 4 | 18 | UM p. 240, p. 238: 18 (4,4,3,4,3) | E |
| `LD r,(IX+d)`, `LD (IX+d),r`, `LD (IX+d),n`, ALU `(IX+d)` | `DD 46 d`, `DD 70 d`, `DD 36 d n`, `DD 86 d` | 5 | 19 | UM p. 75, p. 81, p. 86, p. 149: 19 (4,4,3,5,3) | I |
| `INC (IX+d)`, `DEC (IX+d)` | `DD 34 d`, `DD 35 d` | 6 | 23 | UM p. 168, p. 171: 23 (4,4,3,5,4,3) | I |
| `ADD IX,pp` | `DD 09` | 4 | 15 | UM p. 194: 15 (4,4,4,3) | I |
| `LD IX,nn` | `DD 21 nn` | 4 | 14 | UM p. 100: 14 (4,4,3,3) | I |
| `LD (nn),IX`, `LD IX,(nn)` | `DD 22 nn`, `DD 2A nn` | 4 | 20 | UM p. 110, p. 105: 20 (4,4,3,3,3,3) | I |
| `DEC IX` | `DD 2B` | 4 | 10 | UM p. 202: 10 (4,6) | I |
| `BIT b,(IX+d)` | `DD CB d 46` | 9 | 20 | UM p. 247: 20 (4,4,3,5,4) | I |
| Other `DD CB d` operations on `(IX+d)` | `DD CB d 06`, `DD CB d 86`, `DD CB d C6` | 8 | 23 | UM `RLC` p. 217, `RES` p. 260, `SET` p. 255: 23 (4,4,3,5,4,3) | I |
| `DD CB d` register forms (undocumented) | `DD CB d 40` / `DD CB d 80` | 4 / 4 | 20 / 23 | CH: 20 for `BIT`, 23 otherwise | CH comparison only |
| `DD`/`FD` prefix on an instruction that does not use `HL` | `DD 00` / `DD C3 nn` / `DD 10 e`, `B`=2 | 4 / 10 / 3 | 8 / 14 / 17 | UD section 3.2: the instruction "is executed as is"; section 3.7: the prefix behaves as a `NOP` in the instruction stream, and `NOP` is 4 T-states (UM p. 180) | P |
| `IXH`/`IXL` forms (undocumented) | `DD 24` / `DD 26 n` | 4 / 4 | 8 / 11 | CH: `inc ixh` 8, `ld ixh,n` 11 | CH comparison only |

`FD` (`IY`) encodings take the same code path and report the same counts in
each build, for example `FD 09` 4 against 15 and `FD CB d 46` 9 against 20.
UM gives each `IY` form the same figures as its `IX` form, and test **I** runs
both prefixes.

These forms report the same count in both builds and match UM: `RET cc`
11/5 (p. 287), `JP cc,nn` and `CALL cc,nn` taken, `SBC HL,ss` 15 (p. 192),
`NEG` 8 (p. 176), `INC IX` 10 (p. 199), `PUSH IX` 15 (p. 117), `POP IX` 14
(p. 121), `EX (SP),IX` 23 (p. 128), `JP (IX)` 8 (p. 276), `LD SP,IX` 10
(p. 113), `RLC r` 8 (p. 213) and `SET b,r` / `SET b,(HL)` 8 / 15 (pp. 251-253).

### Limits of the evidence

- The references give documented timings. Nothing here was measured on a
  physical Z80, and a host count does not qualify hardware timing.
- `IN r,(C)` and `OUT (C),r` are documented at 12 T-states (UM p. 296,
  p. 307) but the engine does not execute them, so they are excluded from the
  tests and comparisons.
- The two rows marked "CH comparison only" have no regression test.

## Effect on measurements

Code built from Z80-only instructions measures 12-35% higher with documented
timings. The raw banked transfer at `FF00h`, which uses `LD SP,(nn)`,
`ADD IX,DE` and `PUSH`/`POP IY` in its burst loop:

| Read of | DCC copy | Here | Budget in `banked_memory_z80_test.py` |
| ---: | ---: | ---: | ---: |
| 16 bytes | 746 | 938 | 940 |
| 256 bytes | 5,489 | 6,307 | 6,310 |
| 4,096 bytes | 82,064 | 93,112 | 93,120 |

The cycle budgets in `banked_memory_z80_test.py`, `bank_sdk_z80_test.py`,
`banked_allocator_z80_test.py` and `bank1_system_boot_z80_test.py`, and the
tables in the [MEMPERF README](../../../../examples/MEMPERF/README.md), are
stated in documented T-states. `banked_transfer_benchmark.py` and
`banked_sdk_benchmark.py` print the same figures.

## Regression tests

`Z80Timing` in
[emulator_z80_test.py](../../../common/tests/emulator_z80_test.py) asserts the
counts from tables written by instruction group, independent of the engine's
opcode decoding, and fails if a stepped instruction stops the machine:

- every unprefixed opcode, with its condition met and not met;
- all 256 `CB` opcodes;
- the documented `ED` opcodes the engine executes, and each block-repeat form
  while repeating, on its last iteration and, for `CPIR`/`CPDR`, on a match;
- the documented `IX` and `IY` forms, including `DD CB d` and `FD CB d`;
- the extra fetch for an index prefix on other instructions;
- whole-routine totals for `LDIR`, `INIR`, `DJNZ` and a stack-burst sequence.

Run it before any other Z80 test and after every engine change:

```sh
python3.14 src/common/tests/emulator_z80_test.py
```

## Reproducing the evidence

From the repository root, with the emulator library built:

```sh
# Counts that differ from the DCC copy's
python3.14 src/cpm/z80_debug_host/timing/check_timing.py \
  --against src/cpm/z80_debug_host/timing/dcc_copy_counts.json

# Published tables, downloaded from the links under Sources
python3.14 src/cpm/z80_debug_host/timing/check_timing.py \
  --z80info z80time.txt --clrhome table.html
```

To regenerate `dcc_copy_counts.json`, build the library with the DCC copy of
`x80.cxx` in a scratch tree and select it with `Z80SBC_EMULATOR`:

```sh
mkdir /tmp/z80-dcc && git archive HEAD src/cpm src/common | tar -x -C /tmp/z80-dcc
cp ../dcc/src/dcc_debug_host/x80.cxx /tmp/z80-dcc/src/cpm/z80_debug_host/x80.cxx
cmake -S /tmp/z80-dcc/src/cpm/z80_debug_host -B /tmp/z80-dcc/build \
  -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF
cmake --build /tmp/z80-dcc/build --target z80sbc-emulator
Z80SBC_EMULATOR="$(ls /tmp/z80-dcc/build/libz80sbc-emulator.*)" \
  python3.14 src/cpm/z80_debug_host/timing/check_timing.py \
  --dump src/cpm/z80_debug_host/timing/dcc_copy_counts.json
```
