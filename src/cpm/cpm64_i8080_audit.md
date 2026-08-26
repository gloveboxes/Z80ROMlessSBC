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

- Reachable code: 2531 instructions / 4977 bytes
- Classified data, strings, tables, and workspace: 655 bytes
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

## Intel 8080 optimization review

Applied in the separately assembled and relocated `cpm64_z80.asm`:

- `0xE55E`: replace `MVI A,0` with `XRA A`; `ADD A,L` overwrites flags.
- `0xF503`: replace `MVI A,0` with `XRA A`; the following call establishes
  flags before its caller tests them.
- `0xF0C0`: replace `CALL 0xF02C; RET` with `JMP 0xF02C`.
- Replace 242 eligible `JP` instructions with `JR`
  (97 in CCP and 145 in BDOS).

These save 245 bytes: 98 in CCP and 147 in BDOS.
The generated source symbolizes internal operands, dispatch tables, workspace
pointers, and self-modifying targets, then pads each section to retain the fixed
`0xE300`/`0xEB00`/`0xF900` ABI. Its unoptimized mode assembles byte-for-byte
to the immutable image; this is enforced by the host tests.

Rejected substitutions:

- `0xE444`: `MVI A,0` preserves the Z flag consumed by `CNZ`.
- `0xF384`: `MVI A,0` preserves carry from `CMP` for the following `JC`.
- `0xF6B4`: `MVI A,0` preserves carry for the following `ADC A,B`.

No correctness defect was found in the reachable 8080 CCP/BDOS code. More
invasive Z80 rewrites such as loop conversion, block operations, or alternate
register use are deliberately deferred until they have behavioral tests.
