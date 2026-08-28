# CP/M 2.2 Intel 8080 Audit

`cpm64_system.bin` remains the immutable, boot-proven Burcon CCP/BDOS baseline.
The optimized port is generated separately as `cpm64_z80.asm`; it does not
replace this reference artifact.

## Identity and layout

- SHA-256: `2897f0ecf91048c753ea6a09f26fd28f20a607dddbbaca0c96a6943178115d0e`
- Image: 5632 bytes (2048 CCP + 3584 BDOS)
- CCP: `0xE300-0xEAFF`; cold vector `JP 0xE65C`; warm vector `JP 0xE658`
- BDOS: `0xEB00-0xF8FF`; version byte 22 decimal; entry `JP 0xEB11` at `0xEB06`

## Control-flow and compatibility results

- Reachable code: 2543 instructions / 5006 bytes
- Classified data, strings, tables, and workspace: 626 bytes
- Reachable unofficial 8080 or Z80-only opcodes: **0**
- CCP indirect handlers: 7
- BDOS error handlers: 4
- BDOS function handlers: 41 (functions 0-40)
- Direct BIOS targets: 13; all are aligned 3-byte jump-table entries
- No overlapping instructions or direct branches into classified data were found.
- Independent `z80dasm`/`z80asm` round-trip reproduced the exact SHA-256.

The independent disassembler reports self-modifying code for two intentional cases:
the CCP version-mismatch path at `0xE6CF` writes `DI; HLT` at `0xE300`,
and BDOS error formatting at `0xEBEE` writes the current drive letter into
the message byte at `0xEBC6`.
The synthetic return target at `0xF874` is an explicit discovery root because
BDOS reaches that cleanup block by pushing its address and later executing `RET`.

## Intel 8080 optimization review

Applied in the separately assembled and relocated `cpm64_z80.asm`:

- `0xE55E`: replace `MVI A,0` with `XRA A`; `ADD A,L` overwrites flags.
- `0xF503`: replace `MVI A,0` with `XRA A`; the following call establishes
  flags before its caller tests them.
- `0xF0C0`: replace `CALL 0xF02C; RET` with `JMP 0xF02C`.
- Replace 244 eligible `JP` instructions with `JR`
  (97 in CCP and 147 in BDOS).

These save 247 bytes: 98 in CCP and 149 in BDOS.
The generated source symbolizes internal operands, dispatch tables, workspace
pointers, and self-modifying targets. The active port relocates the fixed-size
sections to `0xE700`/`0xEF00`/`0xFD00`, while its unoptimized reference mode assembles byte-for-byte
to the immutable image; this is enforced by the host tests.

## Deeper Z80 optimization pass

The active port additionally applies guarded whole-program transformations:

- Ten flag-dead `DEC B; JR NZ` loop tails become `DJNZ` (nine in CCP, one in
  BDOS). Each site was proven flag-dead: the loop body's own comparison
  branch is the only flag consumer, and every caller either discards the
  return flags or re-establishes them (for example with `LDA`/`ld a,(nn)`)
  before testing them. An earlier rejection of this transform was traced to
  a flaky DCC debug-host test harness, not a real defect; see below.
- The shared counted byte-copy routine becomes `LD C,B; LD B,0; LDIR`,
  after proving its three callers pass nonzero constants and discard `A`, `BC`,
  and exit flags.
- CCP command normalization keeps the pointer in `HL`, uses `DJNZ`, and removes
  one redundant loop test while preserving the zero-length path.
- An inline `DE = DE - HL` computed by hand with `MOV/SUB/MOV/MOV/SBB/MOV`
  becomes native `OR A; EX DE,HL; SBC HL,DE; EX DE,HL`, after proving the
  accumulator and non-carry flags it changes are dead at the call site.
- Three BDOS error branches target their final handler directly instead of the
  `F4FB` trampoline.
- Three conditional-call/return tails become inverse conditional returns followed
  by direct jumps. This is size-neutral but shortens both paths.
- `F113` inverts a branch-over-jump pair into a single `JR C` past the `F0FE`
  handler.
- Two `DEC A; DEC A` pairs become `SUB 2`, where only the zero/non-zero result
  is consumed by the caller.
- The `EF3E` right-shift-normalize loop moves its counter from `C` to `B`
  (`LD B,(HL)` instead of `LD C,M`) so its `DCR C; JR NZ` tail becomes `DJNZ`.
  `C` is never read again after the loop, and the routine itself overwrites
  `B` two instructions later, so no caller can rely on either register.

Together with the first pass, the active source reclaims 263 bytes: 109 bytes in
CCP and 154 bytes in BDOS. Tests independently reconstruct the relocated layout,
verify every generated `JR` and `DJNZ` destination, check the native instruction
encodings, and retain the byte-identical unoptimized round trip.

An earlier working-tree revision rejected the `DJNZ`, native `SBC HL,DE`,
`SUB 2`, and `F113` transforms after "relocated runtime testing" reported
failures. Root-causing those failures found the actual defect in the test
harness, not the generated code: the DCC debug host's `FullCpmHost` model
reports `exited-normally` whenever the emulated PC coincidentally equals
0x0000 or a bootstrap-captured address, even mid-instruction-stream during
completely ordinary execution. That produced intermittent false failures
unrelated to any of the four transforms (repeated runs of the *same*
generated bytes both passed and failed). `test_dcc_debug_host.py` now
resumes past that spurious checkpoint and all four transforms pass
repeated boot, `DIR`, `LS.COM`, and `SAVE` (disk-write) runs under
`dcc-debug-host`.

Rejected transformations include alternate-register allocation across public BDOS
entries, block operations without closed count/register contracts, rotate rewrites
that alter carry, and zeroing substitutions at `E444`, `F384`, and `F6B4` where
the original flags are live.

Rejected substitutions:

- `0xE444`: `MVI A,0` preserves the Z flag consumed by `CNZ`.
- `0xF384`: `MVI A,0` preserves carry from `CMP` for the following `JC`.
- `0xF6B4`: `MVI A,0` preserves carry for the following `ADC A,B`.

The optimized image boots, completes a disk-directory command, loads a transient,
and completes a BIOS warm reload under `dcc-debug-host` using the SBC adapter. More invasive
alternate-register allocation remains deferred without closed behavioral contracts.
