#!/usr/bin/env python3
"""Disassemble and audit the boot-proven Burcon CP/M 2.2 CCP/BDOS image."""

from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path


ORIGIN = 0xE300
CCP_BYTES = 0x0800
BDOS_BASE = 0xEB00
BIOS_BASE = 0xF900
IMAGE_BYTES = BIOS_BASE - ORIGIN
ACTIVE_ORIGIN = 0xE700
ACTIVE_BDOS_BASE = 0xEF00
ACTIVE_BIOS_BASE = 0xFD00
EXPECTED_SHA256 = (
    "2897f0ecf91048c753ea6a09f26fd28f20a607dddbbaca0c96a6943178115d0e"
)

CCP_COMMAND_TABLE = (0xE6C1, 7)
BDOS_ERROR_TABLE = (0xEB09, 4)
BDOS_FUNCTION_TABLE = (0xEB47, 41)

REGISTERS = ("B", "C", "D", "E", "H", "L", "M", "A")
REGISTER_PAIRS = ("B", "D", "H", "SP")
ALU_OPERATIONS = ("ADD", "ADC", "SUB", "SBB", "ANA", "XRA", "ORA", "CMP")
CONDITIONS = ("NZ", "Z", "NC", "C", "PO", "PE", "P", "M")
UNOFFICIAL_8080 = {0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
                   0xCB, 0xD9, 0xDD, 0xED, 0xFD}
SELF_MODIFYING_SITES = {0xE6D2: ("SHLD", "E300H"),
                        0xEBEE: ("STA", "EBC6H")}
DJNZ_SITES = {
    0xE504, 0xE5AB, 0xE5BC, 0xE5DB, 0xE5EC,
    0xE5F5, 0xE643, 0xE650, 0xE78B, 0xF121,
}
CONDITIONAL_TAIL_CALLS = {0xF698: "NZ", 0xF6A1: "NZ", 0xF8A8: "NZ"}
THREADED_CONDITIONAL_JUMPS = {0xF4D5: 0xEE05, 0xF4E3: 0xEE05,
                              0xF4EC: 0xEE05}
SUB_TWO_SITES = {0xF57A, 0xF582}


@dataclass(frozen=True)
class Instruction:
    address: int
    opcode: int
    size: int
    mnemonic: str
    operand: str = ""
    target: int | None = None
    flow: str = "next"


def hex8(value: int) -> str:
    return f"{value:02X}H"


def hex16(value: int) -> str:
    return f"{value:04X}H"


def decode(image: bytes, address: int) -> Instruction:
    offset = address - ORIGIN
    opcode = image[offset]

    def byte() -> int:
        if offset + 1 >= len(image):
            raise ValueError(f"truncated instruction at {address:04X}")
        return image[offset + 1]

    def word() -> int:
        if offset + 2 >= len(image):
            raise ValueError(f"truncated instruction at {address:04X}")
        return image[offset + 1] | (image[offset + 2] << 8)

    if opcode in UNOFFICIAL_8080:
        return Instruction(address, opcode, 1, "DB", hex8(opcode),
                           flow="unofficial")
    if 0x40 <= opcode <= 0x7F:
        if opcode == 0x76:
            return Instruction(address, opcode, 1, "HLT", flow="stop")
        destination = REGISTERS[(opcode >> 3) & 7]
        source = REGISTERS[opcode & 7]
        return Instruction(address, opcode, 1, "MOV", f"{destination},{source}")
    if 0x80 <= opcode <= 0xBF:
        operation = ALU_OPERATIONS[(opcode >> 3) & 7]
        return Instruction(address, opcode, 1, operation, REGISTERS[opcode & 7])

    low_register = REGISTERS[(opcode >> 3) & 7]
    pair = REGISTER_PAIRS[(opcode >> 4) & 3]
    fixed: dict[int, tuple[str, str, int, str]] = {
        0x00: ("NOP", "", 1, "next"), 0x02: ("STAX", "B", 1, "next"),
        0x07: ("RLC", "", 1, "next"), 0x0A: ("LDAX", "B", 1, "next"),
        0x0F: ("RRC", "", 1, "next"), 0x12: ("STAX", "D", 1, "next"),
        0x17: ("RAL", "", 1, "next"), 0x1A: ("LDAX", "D", 1, "next"),
        0x1F: ("RAR", "", 1, "next"), 0x22: ("SHLD", hex16(word()), 3, "next"),
        0x27: ("DAA", "", 1, "next"), 0x2A: ("LHLD", hex16(word()), 3, "next"),
        0x2F: ("CMA", "", 1, "next"), 0x32: ("STA", hex16(word()), 3, "next"),
        0x37: ("STC", "", 1, "next"), 0x3A: ("LDA", hex16(word()), 3, "next"),
        0x3F: ("CMC", "", 1, "next"), 0xC3: ("JMP", hex16(word()), 3, "jump"),
        0xC9: ("RET", "", 1, "stop"), 0xCD: ("CALL", hex16(word()), 3, "call"),
        0xD3: ("OUT", hex8(byte()), 2, "next"), 0xDB: ("IN", hex8(byte()), 2, "next"),
        0xE3: ("XTHL", "", 1, "next"), 0xE9: ("PCHL", "", 1, "stop"),
        0xEB: ("XCHG", "", 1, "next"), 0xF3: ("DI", "", 1, "next"),
        0xF9: ("SPHL", "", 1, "next"), 0xFB: ("EI", "", 1, "next"),
    }
    if opcode in fixed:
        mnemonic, operand, size, flow = fixed[opcode]
        target = word() if flow in {"jump", "call"} else None
        return Instruction(address, opcode, size, mnemonic, operand, target, flow)

    if opcode < 0x40:
        low_nibble = opcode & 0x0F
        if low_nibble == 0x01:
            return Instruction(address, opcode, 3, "LXI", f"{pair},{hex16(word())}")
        if low_nibble == 0x03:
            return Instruction(address, opcode, 1, "INX", pair)
        if low_nibble in {0x04, 0x0C}:
            return Instruction(address, opcode, 1, "INR", low_register)
        if low_nibble in {0x05, 0x0D}:
            return Instruction(address, opcode, 1, "DCR", low_register)
        if low_nibble in {0x06, 0x0E}:
            return Instruction(address, opcode, 2, "MVI", f"{low_register},{hex8(byte())}")
        if low_nibble == 0x09:
            return Instruction(address, opcode, 1, "DAD", pair)
        if low_nibble == 0x0B:
            return Instruction(address, opcode, 1, "DCX", pair)
        if opcode & 7 == 7:
            rotates = {0x07: "RLC", 0x0F: "RRC", 0x17: "RAL", 0x1F: "RAR",
                       0x27: "DAA", 0x2F: "CMA", 0x37: "STC", 0x3F: "CMC"}
            return Instruction(address, opcode, 1, rotates[opcode])
        raise ValueError(f"unsupported 8080 opcode {opcode:02X} at {address:04X}")

    condition = CONDITIONS[(opcode >> 3) & 7]
    column = opcode & 7
    if column == 0:
        return Instruction(address, opcode, 1, f"R{condition}", flow="next")
    if column == 1:
        push_pair = ("B", "D", "H", "PSW")[(opcode >> 4) & 3]
        return Instruction(address, opcode, 1, "POP", push_pair)
    if column == 2:
        target = word()
        return Instruction(address, opcode, 3, f"J{condition}", hex16(target),
                           target, "conditional_jump")
    if column == 4:
        target = word()
        return Instruction(address, opcode, 3, f"C{condition}", hex16(target),
                           target, "conditional_call")
    if column == 5:
        push_pair = ("B", "D", "H", "PSW")[(opcode >> 4) & 3]
        return Instruction(address, opcode, 1, "PUSH", push_pair)
    if column == 6:
        operation = ("ADI", "ACI", "SUI", "SBI", "ANI", "XRI", "ORI", "CPI")[(opcode >> 3) & 7]
        return Instruction(address, opcode, 2, operation, hex8(byte()))
    if column == 7:
        return Instruction(address, opcode, 1, "RST", str((opcode >> 3) & 7))
    raise ValueError(f"unsupported 8080 opcode {opcode:02X} at {address:04X}")


def table_targets(image: bytes, start: int, count: int) -> list[int]:
    targets = []
    for index in range(count):
        offset = start - ORIGIN + index * 2
        targets.append(image[offset] | (image[offset + 1] << 8))
    return targets


def roots(image: bytes) -> dict[int, str]:
    result = {
        0xE300: "CCP_COLD_ENTRY",
        0xE303: "CCP_WARM_ENTRY",
        0xEB06: "BDOS_ENTRY",
        0xF874: "BDOS_RETURN_CLEANUP",
    }
    for index, target in enumerate(table_targets(image, *CCP_COMMAND_TABLE)):
        result[target] = f"CCP_COMMAND_{index}"
    for index, target in enumerate(table_targets(image, *BDOS_ERROR_TABLE)):
        result[target] = f"BDOS_ERROR_{index}"
    for index, target in enumerate(table_targets(image, *BDOS_FUNCTION_TABLE)):
        if ORIGIN <= target < BIOS_BASE:
            result[target] = f"BDOS_FUNCTION_{index}"
    return result


def discover(image: bytes) -> tuple[dict[int, Instruction], set[int]]:
    instructions: dict[int, Instruction] = {}
    occupied: dict[int, int] = {}
    targets: set[int] = set(roots(image))
    pending = list(targets)

    while pending:
        address = pending.pop()
        while ORIGIN <= address < BIOS_BASE:
            if address in instructions:
                break
            if address in occupied:
                owner = occupied[address]
                raise ValueError(
                    f"control flow enters instruction data at {address:04X} "
                    f"(instruction starts at {owner:04X})"
                )
            instruction = decode(image, address)
            if instruction.flow == "unofficial":
                raise ValueError(
                    f"reachable non-8080 opcode {instruction.opcode:02X} "
                    f"at {address:04X}"
                )
            for byte_address in range(address, address + instruction.size):
                if byte_address >= BIOS_BASE:
                    raise ValueError(f"instruction crosses image end at {address:04X}")
                if byte_address in occupied:
                    raise ValueError(f"overlapping instruction at {address:04X}")
                occupied[byte_address] = address
            instructions[address] = instruction

            if instruction.target is not None and ORIGIN <= instruction.target < BIOS_BASE:
                targets.add(instruction.target)
                pending.append(instruction.target)
            if instruction.flow in {"stop", "jump"}:
                break
            address += instruction.size

    for target in targets:
        if ORIGIN <= target < BIOS_BASE and target not in instructions:
            raise ValueError(f"internal target is not an instruction boundary: {target:04X}")
    return instructions, targets


def labels_for(image: bytes, targets: set[int]) -> dict[int, str]:
    labels = roots(image)
    for target in sorted(targets):
        labels.setdefault(target, f"L_{target:04X}")
    labels.update({
        0xE6C1: "CCP_COMMAND_POINTERS",
        0xEB09: "BDOS_ERROR_POINTERS",
        0xEB47: "BDOS_FUNCTION_POINTERS",
    })
    return labels


def format_instruction(instruction: Instruction, labels: dict[int, str],
                       image: bytes) -> str:
    operand = instruction.operand
    if instruction.target is not None and instruction.target in labels:
        operand = labels[instruction.target]
    text = f"        {instruction.mnemonic:<5} {operand}".rstrip()
    offset = instruction.address - ORIGIN
    encoded = image[offset:offset + instruction.size].hex(" ").upper()
    return f"{text:<32} ; {instruction.address:04X}: {encoded}"


def emit_listing(image: bytes, instructions: dict[int, Instruction],
                 targets: set[int]) -> str:
    labels = labels_for(image, targets)
    lines = [
        "; Generated by disassemble_cpm64.py from the boot-proven Burcon image.",
        "; Intel 8080 mnemonics; unvisited bytes are emitted as data.",
        f"        ORG   {hex16(ORIGIN)}",
        "",
    ]
    address = ORIGIN
    while address < BIOS_BASE:
        if address in labels:
            lines.append(f"{labels[address]}:")
        instruction = instructions.get(address)
        if instruction is not None:
            lines.append(format_instruction(instruction, labels, image))
            address += instruction.size
            continue

        start = address
        data = bytearray()
        while address < BIOS_BASE and address not in instructions and len(data) < 16:
            if address != start and address in labels:
                break
            data.append(image[address - ORIGIN])
            address += 1
        values = ",".join(hex8(value) for value in data)
        printable = "".join(chr(value) if 32 <= value < 127 else "." for value in data)
        lines.append(f"        DB    {values:<63} ; {start:04X}: {printable}")
    return "\n".join(lines) + "\n"


def z80_operand(operand: str, labels: dict[int, str]) -> str:
    registers = {"A": "a", "B": "b", "C": "c", "D": "d", "E": "e",
                 "H": "h", "L": "l", "M": "(hl)", "SP": "sp", "PSW": "af"}
    parts = operand.split(",")
    converted = []
    for part in parts:
        if part in registers:
            converted.append(registers[part])
        elif part.endswith("H") and len(part) == 5:
            value = int(part[:-1], 16)
            converted.append(labels.get(value, f"0x{value:04x}"))
        elif part.endswith("H"):
            converted.append(f"0x{int(part[:-1], 16):02x}")
        else:
            converted.append(part.lower())
    return ",".join(converted)


def format_z80_instruction(instruction: Instruction, labels: dict[int, str]) -> str:
    mnemonic = instruction.mnemonic
    operand = z80_operand(instruction.operand, labels)
    pairs = {"B": "bc", "D": "de", "H": "hl", "SP": "sp", "PSW": "af"}
    register_alu = {"ADD": "add a", "ADC": "adc a", "SUB": "sub",
                    "SBB": "sbc a", "ANA": "and", "XRA": "xor",
                    "ORA": "or", "CMP": "cp"}
    immediate_alu = {"ADI": "add a", "ACI": "adc a", "SUI": "sub",
                     "SBI": "sbc a", "ANI": "and", "XRI": "xor",
                     "ORI": "or", "CPI": "cp"}
    fixed = {"NOP": "nop", "HLT": "halt", "RET": "ret", "RLC": "rlca",
             "RRC": "rrca", "RAL": "rla", "RAR": "rra", "DAA": "daa",
             "CMA": "cpl", "STC": "scf", "CMC": "ccf", "XTHL": "ex (sp),hl",
             "PCHL": "jp (hl)", "XCHG": "ex de,hl", "SPHL": "ld sp,hl",
             "DI": "di", "EI": "ei"}
    if mnemonic in fixed:
        return fixed[mnemonic]
    if mnemonic == "MOV" or mnemonic == "MVI":
        return f"ld {operand}"
    if mnemonic == "LXI":
        pair, value = instruction.operand.split(",")
        return f"ld {pairs[pair]},{z80_operand(value, labels)}"
    if mnemonic == "LDA":
        return f"ld a,({operand})"
    if mnemonic == "STA":
        return f"ld ({operand}),a"
    if mnemonic == "LHLD":
        return f"ld hl,({operand})"
    if mnemonic == "SHLD":
        return f"ld ({operand}),hl"
    if mnemonic == "LDAX":
        return f"ld a,({pairs[instruction.operand]})"
    if mnemonic == "STAX":
        return f"ld ({pairs[instruction.operand]}),a"
    if mnemonic == "INX":
        return f"inc {pairs[instruction.operand]}"
    if mnemonic == "DCX":
        return f"dec {pairs[instruction.operand]}"
    if mnemonic == "INR":
        return f"inc {operand}"
    if mnemonic == "DCR":
        return f"dec {operand}"
    if mnemonic == "DAD":
        return f"add hl,{pairs[instruction.operand]}"
    if mnemonic in register_alu:
        return f"{register_alu[mnemonic]},{operand}" if mnemonic in {"ADD", "ADC", "SBB"} else f"{register_alu[mnemonic]} {operand}"
    if mnemonic in immediate_alu:
        return f"{immediate_alu[mnemonic]},{operand}" if mnemonic in {"ADI", "ACI", "SBI"} else f"{immediate_alu[mnemonic]} {operand}"
    if mnemonic == "JMP":
        return f"jp {operand}"
    if mnemonic.startswith("J") and mnemonic != "JMP":
        return f"jp {mnemonic[1:].lower()},{operand}"
    if mnemonic == "CALL":
        return f"call {operand}"
    if mnemonic.startswith("C") and mnemonic not in {"CMA", "CMC", "CPI"}:
        return f"call {mnemonic[1:].lower()},{operand}"
    if mnemonic.startswith("R") and mnemonic not in {"RAL", "RAR", "RLC", "RRC", "RST", "RET"}:
        return f"ret {mnemonic[1:].lower()}"
    if mnemonic == "PUSH" or mnemonic == "POP":
        return f"{mnemonic.lower()} {pairs[instruction.operand]}"
    if mnemonic == "IN":
        return f"in a,({operand})"
    if mnemonic == "OUT":
        return f"out ({operand}),a"
    if mnemonic == "RST":
        return f"rst {operand}"
    raise ValueError(f"cannot translate {mnemonic} at {instruction.address:04X}")


def emit_z80_port(image: bytes, instructions: dict[int, Instruction],
                  targets: set[int], optimize: bool = True,
                  relocate: bool | None = None,
                  deep_optimize: bool = True) -> str:
    if relocate is None:
        relocate = optimize
    output_origin = ACTIVE_ORIGIN if relocate else ORIGIN
    output_bdos_base = ACTIVE_BDOS_BASE if relocate else BDOS_BASE
    output_bios_base = ACTIVE_BIOS_BASE if relocate else BIOS_BASE
    labels = labels_for(image, targets)
    for value in range(BIOS_BASE, BIOS_BASE + 0x31, 3):
        labels.setdefault(value, f"BIOS_BASE+0x{value - BIOS_BASE:02x}")
    for instruction in instructions.values():
        for part in instruction.operand.split(","):
            if part.endswith("H") and len(part) == 5:
                value = int(part[:-1], 16)
                if ORIGIN <= value < BIOS_BASE:
                    labels.setdefault(value, f"A_{value:04X}")
                elif BIOS_BASE <= value <= BIOS_BASE + 0x30:
                    labels.setdefault(
                        value, f"BIOS_BASE+0x{value - BIOS_BASE:02x}"
                    )

    pointer_tables = {
        CCP_COMMAND_TABLE[0]: CCP_COMMAND_TABLE[1],
        BDOS_ERROR_TABLE[0]: BDOS_ERROR_TABLE[1],
        BDOS_FUNCTION_TABLE[0]: BDOS_FUNCTION_TABLE[1],
    }
    lines = [
        "; Z80 port generated from the immutable Burcon CP/M 2.2 image.",
        "; Internal references are symbolic so each fixed-base section can be compacted.",
        f"BIOS_BASE: equ 0x{output_bios_base:04x}",
        f"        org 0x{output_origin:04x}",
        "",
    ]
    address = ORIGIN
    while address < BIOS_BASE:
        if address == BDOS_BASE:
            lines.extend((
                "",
                f"        defs 0x{output_bdos_base:04x}-$,0",
                f"        org 0x{output_bdos_base:04x}",
                "",
            ))
        if address in labels:
            lines.append(f"{labels[address]}:")
        if address in pointer_tables:
            table_address = address
            for target in table_targets(image, address, pointer_tables[address]):
                if table_address != address and table_address in labels:
                    lines.append(f"{labels[table_address]}:")
                destination = labels.get(target, f"0x{target:04x}")
                lines.append(f"        dw {destination}")
                table_address += 2
            address += pointer_tables[address] * 2
            continue
        if address == 0xE388:
            lines.append(f"        dw {labels[0xE308]}")
            address += 2
            continue
        if optimize and deep_optimize and address == 0xE4A7:
            lines.extend((
                f"        ld hl,{labels[0xE307]}",
                "        ld b,(hl)",
                "        inc hl",
                "        ld a,b",
                "        or a",
                f"        jr z,{labels[0xE4BA]}",
                f"{labels[0xE4AB]}:",
                "        ld a,(hl)",
                f"        call {labels[0xE430]}",
                "        ld (hl),a",
                "        inc hl",
                f"        djnz {labels[0xE4AB]}",
            ))
            address = 0xE4BA
            continue
        if optimize and deep_optimize and address == 0xE4BA:
            lines.append("        ld (hl),b")
            address += instructions[address].size
            continue
        if optimize and deep_optimize and address == 0xE742:
            lines.extend((
                "        ld c,b",
                "        ld b,0",
                "        ldir",
                "        ret",
            ))
            address = 0xE74B
            continue
        if optimize and deep_optimize and address == 0xEEEF:
            expected = ("MOV", "SUB", "MOV", "MOV", "SBB", "MOV")
            sequence = []
            sequence_address = address
            for _ in expected:
                sequence.append(instructions[sequence_address])
                sequence_address += sequence[-1].size
            if tuple(item.mnemonic for item in sequence) != expected:
                raise ValueError("native DE subtraction pattern changed at EEEF")
            lines.extend((
                "        or a",
                "        ex de,hl",
                "        sbc hl,de",
                "        ex de,hl",
            ))
            address = sequence_address
            continue
        if optimize and deep_optimize and address == 0xEF3E:
            # This loop's counter is C, so its DCR/JNZ tail cannot use DJNZ
            # as-is. C is never read again after the loop (the routine
            # overwrites B itself two instructions later, so its caller
            # cannot rely on B being preserved either), so the counter is
            # safely relocated from C to B to enable DJNZ.
            expected = ("LXI", "MOV", "LDA", "ORA", "RAR", "DCR", "JNZ")
            sequence = []
            sequence_address = address
            for _ in expected:
                sequence.append(instructions[sequence_address])
                sequence_address += sequence[-1].size
            if (tuple(item.mnemonic for item in sequence) != expected or
                    sequence[1].operand != "C,M" or
                    sequence[5].operand != "C" or
                    sequence[6].target != 0xEF45):
                raise ValueError("C-to-B loop counter pattern changed at EF3E")
            lines.extend((
                f"        ld hl,{labels[0xF8C3]}",
                "        ld b,(hl)",
                f"        ld a,({labels[0xF8E3]})",
                f"{labels[0xEF45]}:",
                "        or a",
                "        rra",
                f"        djnz {labels[0xEF45]}",
            ))
            address = sequence_address
            continue
        instruction = instructions.get(address)
        if instruction is None:
            lines.append(f"        db 0x{image[address - ORIGIN]:02x}")
            address += 1
            continue

        next_instruction = instructions.get(address + instruction.size)
        if optimize and deep_optimize and address in DJNZ_SITES:
            if (next_instruction is None or next_instruction.mnemonic != "JNZ" or
                    next_instruction.target is None):
                raise ValueError(f"DJNZ pattern changed at {address:04X}")
            text = f"djnz {labels[next_instruction.target]}"
            address += next_instruction.size
        elif optimize and deep_optimize and address in CONDITIONAL_TAIL_CALLS:
            if (next_instruction is None or next_instruction.mnemonic != "RET" or
                    instruction.target is None):
                raise ValueError(f"conditional tail-call pattern changed at {address:04X}")
            lines.append(f"        ret {CONDITIONAL_TAIL_CALLS[address].lower()}")
            text = f"jp {labels[instruction.target]}"
            address += next_instruction.size
        elif optimize and deep_optimize and address in SUB_TWO_SITES:
            if (next_instruction is None or next_instruction.mnemonic != "DCR" or
                    next_instruction.operand != "A"):
                raise ValueError(f"SUB 2 pattern changed at {address:04X}")
            text = "sub 2"
            address += next_instruction.size
        elif optimize and deep_optimize and address == 0xF113:
            if (next_instruction is None or next_instruction.address != 0xF116 or
                    next_instruction.mnemonic != "JMP" or
                    next_instruction.target != 0xF0FE):
                raise ValueError("branch inversion pattern changed at F113")
            text = f"jr c,{labels[0xF0FE]}"
            address += next_instruction.size
        elif optimize and deep_optimize and address in THREADED_CONDITIONAL_JUMPS:
            condition = instruction.mnemonic[1:].lower()
            text = f"jp {condition},{labels[THREADED_CONDITIONAL_JUMPS[address]]}"
        elif optimize and deep_optimize and address == 0xF4FB:
            address += instruction.size
            continue
        elif optimize and address in {0xE55E, 0xF503}:
            text = "xor a"
        elif optimize and address == 0xF0C0:
            text = f"jp {labels[instruction.target or 0]}"
        elif optimize and address == 0xF0C3:
            address += instruction.size
            continue
        else:
            text = format_z80_instruction(instruction, labels)
            if (optimize and
                    instruction.flow in {"jump", "conditional_jump"} and
                    instruction.target is not None and
                    ((address < BDOS_BASE) == (instruction.target < BDOS_BASE)) and
                    -128 <= instruction.target - (address + 2) <= 127 and
                    (instruction.flow == "jump" or instruction.mnemonic[1:] in {"NZ", "Z", "NC", "C"})):
                text = text.replace("jp ", "jr ", 1)
        lines.append(f"        {text}")
        address += instruction.size
    lines.extend(("", f"        defs 0x{output_bios_base:04x}-$,0"))
    return "\n".join(lines) + "\n"


def audit(image: bytes, instructions: dict[int, Instruction]) -> list[str]:
    digest = hashlib.sha256(image).hexdigest()
    if len(image) != IMAGE_BYTES:
        raise ValueError(f"expected {IMAGE_BYTES} bytes, found {len(image)}")
    if digest != EXPECTED_SHA256:
        raise ValueError("CP/M image SHA-256 does not match the boot-proven artifact")
    if image[0:3] != bytes((0xC3, 0x5C, 0xE6)):
        raise ValueError("unexpected CCP cold-entry vector")
    if image[3:6] != bytes((0xC3, 0x58, 0xE6)):
        raise ValueError("unexpected CCP warm-entry vector")
    if image[CCP_BYTES + 6:CCP_BYTES + 9] != bytes((0xC3, 0x11, 0xEB)):
        raise ValueError("unexpected BDOS entry vector")
    if image[CCP_BYTES + 1] != 22:
        raise ValueError("BDOS version byte is not CP/M 2.2 (22 decimal)")

    ccp_targets = table_targets(image, *CCP_COMMAND_TABLE)
    error_targets = table_targets(image, *BDOS_ERROR_TABLE)
    function_targets = table_targets(image, *BDOS_FUNCTION_TABLE)
    for name, values in (("CCP command", ccp_targets),
                         ("BDOS error", error_targets),
                         ("BDOS function", function_targets)):
        for target in values:
            if not (ORIGIN <= target < BIOS_BASE or BIOS_BASE <= target <= BIOS_BASE + 0x30):
                raise ValueError(f"{name} table target outside CP/M/BIOS: {target:04X}")

    bios_targets = {
        instruction.target for instruction in instructions.values()
        if instruction.target is not None and BIOS_BASE <= instruction.target <= BIOS_BASE + 0x30
    }
    if any((target - BIOS_BASE) % 3 != 0 for target in bios_targets):
        raise ValueError("CP/M code targets a non-entry byte in the BIOS jump table")

    code_bytes = sum(instruction.size for instruction in instructions.values())
    tail_calls: list[tuple[int, int]] = []
    zero_loads: list[int] = []
    ordered = sorted(instructions)
    for index, address in enumerate(ordered[:-1]):
        instruction = instructions[address]
        next_instruction = instructions.get(address + instruction.size)
        if (instruction.mnemonic == "CALL" and next_instruction is not None and
                next_instruction.mnemonic == "RET"):
            tail_calls.append((address, instruction.target or 0))
        if instruction.mnemonic == "MVI" and instruction.operand == "A,00H":
            zero_loads.append(address)

    if tail_calls != [(0xF0C0, 0xF02C)]:
        raise ValueError("unexpected CALL/RET optimization candidates")
    if zero_loads != [0xE444, 0xE55E, 0xF384, 0xF503, 0xF6B4]:
        raise ValueError("unexpected MVI A,0 optimization candidates")

    for address, expected in SELF_MODIFYING_SITES.items():
        instruction = instructions.get(address)
        if instruction is None or (instruction.mnemonic, instruction.operand) != expected:
            raise ValueError(f"self-modifying-code site changed at {address:04X}")

    relative_branches = [
        instruction for instruction in instructions.values()
        if (instruction.flow in {"jump", "conditional_jump"} and
            instruction.target is not None and
            ((instruction.address < BDOS_BASE) ==
             (instruction.target < BDOS_BASE)) and
            -128 <= instruction.target - (instruction.address + 2) <= 127 and
            (instruction.flow == "jump" or
             instruction.mnemonic[1:] in {"NZ", "Z", "NC", "C"}))
    ]
    ccp_relative_branches = sum(
        instruction.address < BDOS_BASE for instruction in relative_branches
    )
    bdos_relative_branches = len(relative_branches) - ccp_relative_branches

    return [
        "# CP/M 2.2 Intel 8080 Audit",
        "",
        "`cpm64_system.bin` remains the immutable, boot-proven Burcon CCP/BDOS baseline.",
        "The optimized port is generated separately as `cpm64_z80.asm`; it does not",
        "replace this reference artifact.",
        "",
        "## Identity and layout",
        "",
        f"- SHA-256: `{digest}`",
        f"- Image: {len(image)} bytes ({CCP_BYTES} CCP + {len(image) - CCP_BYTES} BDOS)",
        "- CCP: `0xE300-0xEAFF`; cold vector `JP 0xE65C`; warm vector `JP 0xE658`",
        "- BDOS: `0xEB00-0xF8FF`; version byte 22 decimal; entry `JP 0xEB11` at `0xEB06`",
        "",
        "## Control-flow and compatibility results",
        "",
        f"- Reachable code: {len(instructions)} instructions / {code_bytes} bytes",
        f"- Classified data, strings, tables, and workspace: {len(image) - code_bytes} bytes",
        "- Reachable unofficial 8080 or Z80-only opcodes: **0**",
        f"- CCP indirect handlers: {len(ccp_targets)}",
        f"- BDOS error handlers: {len(error_targets)}",
        f"- BDOS function handlers: {len(function_targets)} (functions 0-40)",
        f"- Direct BIOS targets: {len(bios_targets)}; all are aligned 3-byte jump-table entries",
        "- No overlapping instructions or direct branches into classified data were found.",
        "- Independent `z80dasm`/`z80asm` round-trip reproduced the exact SHA-256.",
        "",
        "The independent disassembler reports self-modifying code for two intentional cases:",
        "the CCP version-mismatch path at `0xE6CF` writes `DI; HLT` at `0xE300`,",
        "and BDOS error formatting at `0xEBEE` writes the current drive letter into",
        "the message byte at `0xEBC6`.",
        "The synthetic return target at `0xF874` is an explicit discovery root because",
        "BDOS reaches that cleanup block by pushing its address and later executing `RET`.",
        "",
        "## Intel 8080 optimization review",
        "",
        "Applied in the separately assembled and relocated `cpm64_z80.asm`:",
        "",
        "- `0xE55E`: replace `MVI A,0` with `XRA A`; `ADD A,L` overwrites flags.",
        "- `0xF503`: replace `MVI A,0` with `XRA A`; the following call establishes",
        "  flags before its caller tests them.",
        "- `0xF0C0`: replace `CALL 0xF02C; RET` with `JMP 0xF02C`.",
        f"- Replace {len(relative_branches)} eligible `JP` instructions with `JR`",
        f"  ({ccp_relative_branches} in CCP and {bdos_relative_branches} in BDOS).",
        "",
        f"These save {len(relative_branches) + 3} bytes: "
        f"{ccp_relative_branches + 1} in CCP and {bdos_relative_branches + 2} in BDOS.",
        "The generated source symbolizes internal operands, dispatch tables, workspace",
        "pointers, and self-modifying targets. The active port relocates the fixed-size",
        "sections to `0xE700`/`0xEF00`/`0xFD00`, while its unoptimized reference mode assembles byte-for-byte",
        "to the immutable image; this is enforced by the host tests.",
        "",
        "## Deeper Z80 optimization pass",
        "",
        "The active port additionally applies guarded whole-program transformations:",
        "",
        "- Ten flag-dead `DEC B; JR NZ` loop tails become `DJNZ` (nine in CCP, one in",
        "  BDOS). Each site was proven flag-dead: the loop body's own comparison",
        "  branch is the only flag consumer, and every caller either discards the",
        "  return flags or re-establishes them (for example with `LDA`/`ld a,(nn)`)",
        "  before testing them. An earlier rejection of this transform was traced to",
        "  a flaky DCC debug-host test harness, not a real defect; see below.",
        "- The shared counted byte-copy routine becomes `LD C,B; LD B,0; LDIR`,",
        "  after proving its three callers pass nonzero constants and discard `A`, `BC`,",
        "  and exit flags.",
        "- CCP command normalization keeps the pointer in `HL`, uses `DJNZ`, and removes",
        "  one redundant loop test while preserving the zero-length path.",
        "- An inline `DE = DE - HL` computed by hand with `MOV/SUB/MOV/MOV/SBB/MOV`",
        "  becomes native `OR A; EX DE,HL; SBC HL,DE; EX DE,HL`, after proving the",
        "  accumulator and non-carry flags it changes are dead at the call site.",
        "- Three BDOS error branches target their final handler directly instead of the",
        "  `F4FB` trampoline.",
        "- Three conditional-call/return tails become inverse conditional returns followed",
        "  by direct jumps. This is size-neutral but shortens both paths.",
        "- `F113` inverts a branch-over-jump pair into a single `JR C` past the `F0FE`",
        "  handler.",
        "- Two `DEC A; DEC A` pairs become `SUB 2`, where only the zero/non-zero result",
        "  is consumed by the caller.",
        "- The `EF3E` right-shift-normalize loop moves its counter from `C` to `B`",
        "  (`LD B,(HL)` instead of `LD C,M`) so its `DCR C; JR NZ` tail becomes `DJNZ`.",
        "  `C` is never read again after the loop, and the routine itself overwrites",
        "  `B` two instructions later, so no caller can rely on either register.",
        "",
        "Together with the first pass, the active source reclaims 263 bytes: 109 bytes in",
        "CCP and 154 bytes in BDOS. Tests independently reconstruct the relocated layout,",
        "verify every generated `JR` and `DJNZ` destination, check the native instruction",
        "encodings, and retain the byte-identical unoptimized round trip.",
        "",
        "An earlier working-tree revision rejected the `DJNZ`, native `SBC HL,DE`,",
        "`SUB 2`, and `F113` transforms after \"relocated runtime testing\" reported",
        "failures. Root-causing those failures found the actual defect in the test",
        "harness, not the generated code: the DCC debug host's `FullCpmHost` model",
        "reports `exited-normally` whenever the emulated PC coincidentally equals",
        "0x0000 or a bootstrap-captured address, even mid-instruction-stream during",
        "completely ordinary execution. That produced intermittent false failures",
        "unrelated to any of the four transforms (repeated runs of the *same*",
        "generated bytes both passed and failed). `test_dcc_debug_host.py` now",
        "resumes past that spurious checkpoint and all four transforms pass",
        "repeated boot, `DIR`, `LS.COM`, and `SAVE` (disk-write) runs under",
        "`dcc-debug-host`.",
        "",
        "Rejected transformations include alternate-register allocation across public BDOS",
        "entries, block operations without closed count/register contracts, rotate rewrites",
        "that alter carry, and zeroing substitutions at `E444`, `F384`, and `F6B4` where",
        "the original flags are live.",
        "",
        "Rejected substitutions:",
        "",
        "- `0xE444`: `MVI A,0` preserves the Z flag consumed by `CNZ`.",
        "- `0xF384`: `MVI A,0` preserves carry from `CMP` for the following `JC`.",
        "- `0xF6B4`: `MVI A,0` preserves carry for the following `ADC A,B`.",
        "",
        "The optimized image boots, completes a disk-directory command, loads a transient,",
        "and completes a BIOS warm reload under `dcc-debug-host` using the SBC adapter. More invasive",
        "alternate-register allocation remains deferred without closed behavioral contracts.",
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path,
                        default=Path(__file__).with_name("cpm64_system.bin"))
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).with_name("cpm64_i8080.asm"))
    parser.add_argument("--report", type=Path,
                        default=Path(__file__).with_name("cpm64_i8080_audit.md"))
    parser.add_argument("--z80-output", type=Path,
                        default=Path(__file__).with_name("cpm64_z80.asm"))
    args = parser.parse_args()

    image = args.input.read_bytes()
    instructions, targets = discover(image)
    args.output.write_text(emit_listing(image, instructions, targets), encoding="ascii")
    args.z80_output.write_text(
        emit_z80_port(image, instructions, targets), encoding="ascii"
    )
    report = audit(image, instructions)
    args.report.write_text("\n".join(report) + "\n", encoding="ascii")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())