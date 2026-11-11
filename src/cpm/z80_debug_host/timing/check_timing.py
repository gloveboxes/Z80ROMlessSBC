#!/usr/bin/env python3
"""Reproduce the T-state evidence in README.md for the loaded Z80 engine.

Every instruction form is stepped once and its tick count recorded:

    check_timing.py --dump counts.json        save the count of each form
    check_timing.py --against counts.json     list forms that differ from a dump
    check_timing.py --z80info z80time.txt --clrhome table.html
                                              compare with published tables

Select another engine build with Z80SBC_EMULATOR=/path/to/libz80sbc-emulator.
The reference tables are not stored here; README.md gives their URLs.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "common/tests"))

import z80  # noqa: E402

S, Z, PV, C = 0x80, 0x40, 0x04, 0x01
# Flag bytes that meet and fail NZ, Z, NC, C, PO, PE, P, M in opcode order.
CONDITIONS = ((0, Z), (Z, 0), (0, C), (C, 0), (0, PV), (PV, 0), (0, S), (S, 0))
PREFIXES = (0xCB, 0xDD, 0xED, 0xFD)


def ticks(code: tuple[int, ...], taken: bool) -> int | None:
    """Ticks for one step, or None when the engine cannot execute the form.

    taken selects a met condition, a DJNZ that loops, or a block instruction
    that repeats; it has no effect on other instructions.
    """
    indexed = code[0] in (0xDD, 0xFD) and code[1] not in (0xCB, 0xED)
    opcode = code[1 if indexed else 0]
    flags, registers = 0, {"b": 2 if taken else 1}
    if code[0] == 0xED:
        # Copies and compares count in BC; the I/O forms count in B.
        last = 0x0110 if code[1] & 2 else 0x0001
        registers = {"bc": 0x0210 if taken else last}
    elif opcode in (0x20, 0x28, 0x30, 0x38):
        flags = CONDITIONS[(opcode >> 3) & 3][not taken]
    elif opcode >= 0xC0 and opcode & 7 in (0, 2, 4):
        flags = CONDITIONS[(opcode >> 3) & 7][not taken]
    machine = z80.Z80Machine()
    machine.memory[0x100:0x104] = bytes(code).ljust(4, b"\x10")[:4]
    machine.memory[0xF000:0xF002] = b"\x00\x30"
    machine.pc, machine.sp, machine.f = 0x100, 0xF000, flags
    machine.a, machine.bc, machine.de, machine.hl = 0x55, 0x0210, 0x5000, 0x4000
    machine.ix, machine.iy = 0x6000, 0x7000
    for name, value in registers.items():
        setattr(machine, name, value)
    machine.ticks_to_stop = 1000
    machine.step_over_breakpoint()
    if machine.halted and opcode != 0x76:
        return None
    return 1000 - machine.ticks_to_stop


def label(code: tuple[int, ...]) -> str:
    return bytes(code).hex(" ").upper()


def all_forms() -> dict[str, int | None]:
    """Counts keyed by opcode bytes, with the taken and not-taken case of each."""
    codes: list[tuple[int, ...]] = []
    plain = [op for op in range(256) if op not in PREFIXES]
    codes += [(op,) for op in plain]
    codes += [(0xCB, op) for op in range(256)]
    codes += [(0xED, op) for op in range(256)]
    for prefix in (0xDD, 0xFD):
        codes += [(prefix, op) for op in plain]
        codes += [(prefix, 0xCB, 0x10, op) for op in range(256)]
    counts: dict[str, int | None] = {}
    for code in codes:
        counts[label(code) + " taken"] = ticks(code, True)
        counts[label(code) + " not taken"] = ticks(code, False)
    return counts


def z80info_table(path: Path) -> list[tuple[str, tuple[int, ...], int, int]]:
    """Rows of z80time.txt as (mnemonic, opcode bytes, taken, not taken)."""
    rows = []
    for line in path.read_text(errors="replace").splitlines():
        match = re.match(r"^(\S.*?)\s{2,}(\d+)(?:/(\d+))?\s+\d\s+([0-9A-F].*)$", line.rstrip())
        if not match:
            continue
        name, high, low, pattern = match.groups()
        # XX is an operand byte; "88+rb" and "46+8*b" are taken at r = B, b = 0.
        code = tuple(0x10 if token == "XX" else int(token[:2], 16) for token in pattern.split())
        rows.append((name, code, int(high), int(low or high)))
    return rows


def clrhome_table(path: Path) -> list[tuple[str, tuple[int, ...], int, int]]:
    """Cells of the ClrHome opcode table, without its Z180-only instructions."""
    rows = []
    for attributes, name, opcode, cycles in re.findall(
            r"<td([^>]*)>\s*<code>(.*?)</code>\s*<dl>\s*<dt>Opcode</dt>\s*<dd>(.*?)</dd>"
            r"\s*<dt>Bytes</dt>\s*<dd>.*?</dd>\s*<dt>Cycles</dt>\s*<dd>(.*?)</dd>",
            path.read_text(errors="replace"), re.S):
        numbers = [int(number) for number in re.findall(r"\d+", cycles)]
        if "z180" in attributes or not numbers:
            continue
        name = html.unescape(re.sub(r"<.*?>", "", name)).strip()
        if "undocumented" in attributes:
            name += " (undocumented)"
        code = tuple(int(token, 16) if re.fullmatch(r"[0-9A-F]{2}", token) else 0x10
                     for token in re.sub(r"<var>.*?</var>", " operand ", opcode).split())
        rows.append((name, code, max(numbers), min(numbers)))
    return rows


def compare_table(title: str, rows: list[tuple[str, tuple[int, ...], int, int]]) -> bool:
    compared, unexecuted, differing = 0, [], []
    for name, code, high, low in rows:
        measured = ticks(code, True), ticks(code, False)
        if measured[0] is None:
            unexecuted.append(f"{name} [{label(code)}]")
            continue
        compared += 1
        if measured != (high, low):
            differing.append(f"{name} [{label(code)}]: table {high}/{low}, "
                             f"engine {measured[0]}/{measured[1]}")
    print(f"{title}: {len(rows)} entries, {compared} compared, "
          f"{len(unexecuted)} not executed by the engine, {len(differing)} differ")
    for line in differing:
        print("  differs:", line)
    for line in unexecuted:
        print("  not executed:", line)
    return not differing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", type=Path, help="write every form's count as JSON")
    parser.add_argument("--against", type=Path, help="list forms that differ from a dump")
    parser.add_argument("--z80info", type=Path, help="downloaded z80.info z80time.txt")
    parser.add_argument("--clrhome", type=Path, help="downloaded ClrHome opcode table HTML")
    args = parser.parse_args()
    agreed = True
    if args.dump or args.against:
        counts = all_forms()
        if args.dump:
            args.dump.write_text(json.dumps(counts, indent=0, sort_keys=True) + "\n")
            print(f"wrote {len(counts)} counts to {args.dump}")
        if args.against:
            other = json.loads(args.against.read_text())
            changed = [key for key in counts if counts[key] != other.get(key)]
            print(f"{len(counts)} forms, {len(changed)} differ from {args.against.name}")
            for key in changed:
                print(f"  {key}: {other.get(key)} -> {counts[key]}")
    if args.z80info:
        agreed &= compare_table("z80.info z80time.txt", z80info_table(args.z80info))
    if args.clrhome:
        agreed &= compare_table("ClrHome opcode table", clrhome_table(args.clrhome))
    return 0 if agreed else 1


if __name__ == "__main__":
    raise SystemExit(main())
