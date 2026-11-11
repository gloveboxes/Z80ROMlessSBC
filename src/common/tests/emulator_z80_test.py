"""Unit tests for the C Z80 emulator itself (x80 core plus two-bank SRAM).

Every other Z80 test trusts this emulator. These tests check its documented
flag behavior and T-state counts against an independent Python reference, and
its bank model, control and I/O hooks, before any firmware or CP/M result is
believed.
"""

from __future__ import annotations

import itertools
import unittest

import z80

S, Z, H, PV, N, C = 0x80, 0x40, 0x10, 0x04, 0x02, 0x01
DOCUMENTED = S | Z | H | PV | N | C
CODE = 0x0100
EDGE8 = (0x00, 0x01, 0x02, 0x0F, 0x10, 0x7F, 0x80, 0x81, 0x8F, 0xF0, 0xFF)
EDGE16 = (0x0000, 0x0001, 0x07FF, 0x0800, 0x0FFF, 0x1000, 0x7FFF, 0x8000,
          0x8FFF, 0xF000, 0xFFFF)


def run(machine: z80.Z80Machine, code: bytes) -> None:
    machine.memory[CODE:CODE + len(code) + 1] = code + b"\x76"
    machine.halted = False
    machine.pc = CODE
    machine.sp = 0xF000
    machine.ticks_to_stop = 100000
    machine.run()
    assert machine.halted


def add8(a: int, b: int, carry: int) -> tuple[int, int]:
    total = a + b + carry
    result = total & 0xFF
    flags = (result & S) | (Z if not result else 0)
    flags |= H if (a & 15) + (b & 15) + carry > 15 else 0
    flags |= PV if (a ^ result) & (b ^ result) & 0x80 else 0
    return result, flags | (C if total > 0xFF else 0)


def sub8(a: int, b: int, borrow: int) -> tuple[int, int]:
    total = a - b - borrow
    result = total & 0xFF
    flags = (result & S) | (Z if not result else 0) | N
    flags |= H if (a & 15) - (b & 15) - borrow < 0 else 0
    flags |= PV if (a ^ b) & (a ^ result) & 0x80 else 0
    return result, flags | (C if total < 0 else 0)


def adc16(a: int, b: int, carry: int) -> tuple[int, int]:
    total = a + b + carry
    result = total & 0xFFFF
    flags = ((result >> 8) & S) | (Z if not result else 0)
    flags |= H if (a & 0xFFF) + (b & 0xFFF) + carry > 0xFFF else 0
    flags |= PV if (a ^ result) & (b ^ result) & 0x8000 else 0
    return result, flags | (C if total > 0xFFFF else 0)


def sbc16(a: int, b: int, borrow: int) -> tuple[int, int]:
    total = a - b - borrow
    result = total & 0xFFFF
    flags = ((result >> 8) & S) | (Z if not result else 0) | N
    flags |= H if (a & 0xFFF) - (b & 0xFFF) - borrow < 0 else 0
    flags |= PV if (a ^ b) & (a ^ result) & 0x8000 else 0
    return result, flags | (C if total < 0 else 0)


class Z80Flags(unittest.TestCase):
    def setUp(self) -> None:
        self.machine = z80.Z80Machine()

    def check8(self, opcode: int, model, uses_carry: bool, keeps_a: bool = False) -> None:
        for a, b, carry in itertools.product(EDGE8, EDGE8, (0, 1) if uses_carry else (0,)):
            with self.subTest(a=a, b=b, carry=carry):
                self.machine.a, self.machine.b, self.machine.f = a, b, carry
                run(self.machine, bytes([opcode]))
                result, flags = model(a, b, carry)
                self.assertEqual(self.machine.a, a if keeps_a else result)
                self.assertEqual(self.machine.f & DOCUMENTED, flags)

    def test_add_a(self) -> None:
        self.check8(0x80, add8, False)

    def test_adc_a(self) -> None:
        self.check8(0x88, add8, True)

    def test_sub(self) -> None:
        self.check8(0x90, sub8, False)

    def test_sbc_a(self) -> None:
        self.check8(0x98, sub8, True)

    def test_cp(self) -> None:
        self.check8(0xB8, sub8, False, keeps_a=True)

    def check16(self, prefix: bytes, model, uses_carry: bool = True) -> None:
        for a, b, carry in itertools.product(EDGE16, EDGE16, (0, 1)):
            with self.subTest(a=a, b=b, carry=carry):
                self.machine.hl, self.machine.de, self.machine.f = a, b, carry
                run(self.machine, prefix)
                result, flags = model(a, b, carry)
                self.assertEqual(self.machine.hl, result)
                self.assertEqual(self.machine.f & DOCUMENTED, flags)

    def test_adc_hl(self) -> None:
        self.check16(b"\xED\x5A", adc16)

    def test_sbc_hl(self) -> None:
        self.check16(b"\xED\x52", sbc16)

    def test_sbc_hl_half_carry_with_carry_in(self) -> None:
        self.machine.hl, self.machine.de, self.machine.f = 0x0FFF, 0x0FFF, C
        run(self.machine, b"\xED\x52")
        self.assertEqual(self.machine.hl, 0xFFFF)
        self.assertTrue(self.machine.f & H)

    def test_add_hl_changes_only_h_n_c(self) -> None:
        for a, b in itertools.product(EDGE16, EDGE16):
            with self.subTest(a=a, b=b):
                self.machine.hl, self.machine.de, self.machine.f = a, b, S | Z | PV
                run(self.machine, b"\x19")
                total = a + b
                self.assertEqual(self.machine.hl, total & 0xFFFF)
                expected = S | Z | PV
                expected |= H if (a & 0xFFF) + (b & 0xFFF) > 0xFFF else 0
                expected |= C if total > 0xFFFF else 0
                self.assertEqual(self.machine.f & DOCUMENTED, expected)

    def test_inc_dec_preserve_carry_and_set_overflow(self) -> None:
        self.machine.b, self.machine.f = 0x7F, C
        run(self.machine, b"\x04")
        self.assertEqual((self.machine.b, self.machine.f & DOCUMENTED), (0x80, S | H | PV | C))
        self.machine.b, self.machine.f = 0x80, 0
        run(self.machine, b"\x05")
        self.assertEqual((self.machine.b, self.machine.f & DOCUMENTED), (0x7F, H | PV | N))

    def test_daa_after_add_and_sub(self) -> None:
        self.machine.a, self.machine.b, self.machine.f = 0x19, 0x28, 0
        run(self.machine, b"\x80\x27")
        self.assertEqual(self.machine.a, 0x47)
        self.machine.a, self.machine.b, self.machine.f = 0x40, 0x15, 0
        run(self.machine, b"\x90\x27")
        self.assertEqual(self.machine.a, 0x25)

    def test_neg_and_rotate_through_carry(self) -> None:
        self.machine.a, self.machine.f = 0x80, 0
        run(self.machine, b"\xED\x44")
        self.assertEqual((self.machine.a, self.machine.f & DOCUMENTED), (0x80, S | PV | N | C))
        self.machine.a, self.machine.f = 0x80, 0
        run(self.machine, b"\x17")
        self.assertEqual((self.machine.a, self.machine.f & C), (0x00, C))


class Z80Execution(unittest.TestCase):
    def setUp(self) -> None:
        self.machine = z80.Z80Machine()

    def test_block_copy_and_djnz(self) -> None:
        self.machine.memory[0x2000:0x2005] = b"HELLO"
        self.machine.hl, self.machine.de, self.machine.bc = 0x2000, 0x3000, 5
        run(self.machine, b"\xED\xB0")
        self.assertEqual(bytes(self.machine.memory[0x3000:0x3005]), b"HELLO")
        self.assertEqual(self.machine.bc, 0)
        self.machine.b, self.machine.a = 7, 0
        run(self.machine, b"\x3C\x10\xFD")
        self.assertEqual(self.machine.a, 7)

    def test_call_ret_push_pop_and_exchange(self) -> None:
        self.machine.hl, self.machine.de = 0x1111, 0x2222
        run(self.machine, b"\xE5\xEB\xD1\xCD\x0A\x01\x76\x00\x00\x00\x3E\x5A\xC9")
        self.assertEqual((self.machine.hl, self.machine.de), (0x2222, 0x1111))
        self.assertEqual(self.machine.a, 0x5A)

    def test_ixiy_indexed_access(self) -> None:
        self.machine.memory[0x2010] = 0xA5
        self.machine.ix = 0x2000
        run(self.machine, b"\xDD\x7E\x10\xDD\x36\x11\x77")
        self.assertEqual(self.machine.a, 0xA5)
        self.assertEqual(self.machine.memory[0x2011], 0x77)

    def test_in_out_use_the_hooks_with_full_port_address(self) -> None:
        seen = []
        self.machine.set_input_callback(lambda port: 0x40 + (port & 0xFF))
        self.machine.set_output_callback(lambda port, value: seen.append((port & 0xFF, value)))
        run(self.machine, b"\x3E\x99\xD3\x21\xDB\x22")
        self.assertEqual(seen, [(0x21, 0x99)])
        self.assertEqual(self.machine.a, 0x62)

    def test_tick_limit_halt_and_breakpoint(self) -> None:
        self.machine.memory[CODE:CODE + 4] = b"\x00\x00\x00\x76"
        self.machine.pc = CODE
        self.machine.ticks_to_stop = 8
        self.machine.run()
        self.assertFalse(self.machine.halted)
        self.assertGreater(self.machine.pc, CODE)
        self.machine.set_breakpoint(CODE + 3)
        self.machine.pc, self.machine.ticks_to_stop = CODE, 1000
        self.machine.run()
        self.assertEqual((self.machine.pc, self.machine.halted), (CODE + 3, False))
        self.machine.clear_breakpoints()
        self.machine.run()
        self.assertTrue(self.machine.halted)


def documented_main() -> dict[int, tuple[int, int]]:
    """Unprefixed T-states as (condition met, not met), grouped as the Zilog
    Z80 CPU User Manual lists them rather than by opcode bit fields."""
    table: dict[int, tuple[int, int]] = {}

    def put(tstates: int, *opcodes: int, unmet: int | None = None) -> None:
        for opcode in opcodes:
            assert opcode not in table, hex(opcode)
            table[opcode] = (tstates, tstates if unmet is None else unmet)

    registers = (0, 1, 2, 3, 4, 5, 7)                               # B C D E H L A
    put(4, 0x00, 0x08, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37, 0x3F,
        0x76, 0xD9, 0xE9, 0xEB, 0xF3, 0xFB)
    put(4, *(0x40 | d << 3 | s for d in registers for s in registers))    # LD r,r'
    put(7, *(0x46 | d << 3 for d in registers))                     # LD r,(HL)
    put(7, *(0x70 | s for s in registers))                          # LD (HL),r
    put(7, *(0x06 | d << 3 for d in registers))                     # LD r,n
    put(10, 0x36)                                                   # LD (HL),n
    put(7, 0x02, 0x12, 0x0A, 0x1A)                                  # LD (BC/DE),A and back
    put(13, 0x32, 0x3A)                                             # LD (nn),A and back
    put(16, 0x22, 0x2A)                                             # LD (nn),HL and back
    put(10, 0x01, 0x11, 0x21, 0x31)                                 # LD rp,nn
    put(6, 0xF9, 0x03, 0x13, 0x23, 0x33, 0x0B, 0x1B, 0x2B, 0x3B)    # LD SP,HL; INC/DEC rp
    put(11, 0x09, 0x19, 0x29, 0x39)                                 # ADD HL,rp
    put(4, *(0x80 | o << 3 | s for o in range(8) for s in registers))     # ALU r
    put(7, *(0x86 | o << 3 for o in range(8)))                      # ALU (HL)
    put(7, *(0xC6 | o << 3 for o in range(8)))                      # ALU n
    put(4, *(0x04 | r << 3 for r in registers), *(0x05 | r << 3 for r in registers))
    put(11, 0x34, 0x35)                                             # INC/DEC (HL)
    put(11, 0xC5, 0xD5, 0xE5, 0xF5)                                 # PUSH
    put(10, 0xC1, 0xD1, 0xE1, 0xF1)                                 # POP
    put(19, 0xE3)                                                   # EX (SP),HL
    put(10, 0xC3, *(0xC2 | c << 3 for c in range(8)))               # JP, JP cc
    put(12, 0x18)                                                   # JR
    put(12, 0x20, 0x28, 0x30, 0x38, unmet=7)                        # JR cc
    put(13, 0x10, unmet=8)                                          # DJNZ
    put(17, 0xCD)                                                   # CALL
    put(17, *(0xC4 | c << 3 for c in range(8)), unmet=10)           # CALL cc
    put(10, 0xC9)                                                   # RET
    put(11, *(0xC0 | c << 3 for c in range(8)), unmet=5)            # RET cc
    put(11, *(0xC7 | n << 3 for n in range(8)))                     # RST
    put(11, 0xD3, 0xDB)                                             # OUT (n),A; IN A,(n)
    assert sorted(table) == [op for op in range(256) if op not in (0xCB, 0xDD, 0xED, 0xFD)]
    return table


# IN r,(C) and OUT (C),r are absent: the engine does not implement them.
DOCUMENTED_ED = {
    **dict.fromkeys((0x42, 0x52, 0x62, 0x72, 0x4A, 0x5A, 0x6A, 0x7A), 15),  # SBC/ADC HL,rp
    **dict.fromkeys((0x43, 0x53, 0x63, 0x73, 0x4B, 0x5B, 0x6B, 0x7B), 20),  # LD (nn),rp and back
    0x44: 8, 0x45: 14, 0x4D: 14, 0x46: 8, 0x56: 8, 0x5E: 8,         # NEG RETN RETI IM
    0x47: 9, 0x4F: 9, 0x57: 9, 0x5F: 9, 0x67: 18, 0x6F: 18,         # LD I/R; RRD; RLD
    **dict.fromkeys((0xA0, 0xA1, 0xA2, 0xA3, 0xA8, 0xA9, 0xAA, 0xAB), 16),  # LDI CPI INI OUTI ...
}
BLOCK_REPEATS = (0xB0, 0xB1, 0xB2, 0xB3, 0xB8, 0xB9, 0xBA, 0xBB)   # 21 repeating, 16 when done
DOCUMENTED_INDEX = {
    **dict.fromkeys((0x09, 0x19, 0x29, 0x39), 15),                  # ADD IX,rp
    0x21: 14, 0x22: 20, 0x2A: 20, 0x23: 10, 0x2B: 10,               # LD IX,nn/(nn); INC/DEC IX
    0x34: 23, 0x35: 23, 0x36: 19,                                   # INC/DEC (IX+d); LD (IX+d),n
    **dict.fromkeys((0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x7E), 19),  # LD r,(IX+d)
    **dict.fromkeys((0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77), 19),  # LD (IX+d),r
    **dict.fromkeys((0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE), 19),  # ALU (IX+d)
    0xE1: 14, 0xE3: 23, 0xE5: 15, 0xE9: 8, 0xF9: 10,                # POP EX PUSH JP LD SP
}
# Flag bytes that meet and fail NZ, Z, NC, C, PO, PE, P, M in opcode order.
CONDITION_FLAGS = ((0, Z), (Z, 0), (0, C), (C, 0), (0, PV), (PV, 0), (0, S), (S, 0))


class Z80Timing(unittest.TestCase):
    """Tick counts are documented Z80 T-states; cycle budgets depend on them."""

    def setUp(self) -> None:
        self.machine = z80.Z80Machine()

    def step(self, code: bytes, flags: int = 0, **registers: int) -> int:
        machine = self.machine
        machine.memory[CODE:CODE + 4] = code.ljust(4, b"\x10")
        machine.memory[0xF000:0xF002] = b"\x00\x30"
        machine.halted = False
        machine.pc, machine.sp, machine.f = CODE, 0xF000, flags
        machine.a, machine.bc, machine.de, machine.hl = 0x55, 0x0210, 0x5000, 0x4000
        machine.ix, machine.iy = 0x6000, 0x7000
        for name, value in registers.items():
            setattr(machine, name, value)
        machine.ticks_to_stop = 1000
        machine.step_over_breakpoint()
        # A count for an instruction the engine cannot execute proves nothing.
        self.assertEqual(machine.halted, code[:1] == b"\x76", code.hex())
        return 1000 - machine.ticks_to_stop

    def conditions(self, opcode: int) -> tuple[int, int]:
        """Flags that meet and fail the opcode's condition; DJNZ uses B instead."""
        if opcode in (0x20, 0x28, 0x30, 0x38):
            return CONDITION_FLAGS[(opcode >> 3) & 3]
        return CONDITION_FLAGS[(opcode >> 3) & 7]

    def test_unprefixed_instructions(self) -> None:
        for opcode, (met, unmet) in documented_main().items():
            flags = self.conditions(opcode)
            with self.subTest(opcode=f"{opcode:02X}"):
                self.assertEqual(self.step(bytes([opcode]), flags[0], b=2), met)
                self.assertEqual(self.step(bytes([opcode]), flags[1], b=1), unmet)

    def test_bit_instructions(self) -> None:
        for opcode in range(256):
            expected = 8 if opcode & 7 != 6 else 12 if 0x40 <= opcode <= 0x7F else 15
            with self.subTest(opcode=f"CB {opcode:02X}"):
                self.assertEqual(self.step(bytes([0xCB, opcode])), expected)

    def test_extended_instructions(self) -> None:
        for opcode, expected in DOCUMENTED_ED.items():
            with self.subTest(opcode=f"ED {opcode:02X}"):
                self.assertEqual(self.step(bytes([0xED, opcode])), expected)

    def test_block_repeat_instructions(self) -> None:
        for opcode in BLOCK_REPEATS:
            # Copies and compares count in BC; the I/O forms count in B.
            last = 0x0110 if opcode & 2 else 0x0001
            with self.subTest(opcode=f"ED {opcode:02X}"):
                self.assertEqual(self.step(bytes([0xED, opcode]), bc=0x0210), 21)
                self.assertEqual(self.step(bytes([0xED, opcode]), bc=last), 16)
        # CPIR and CPDR also stop on a match while BC is still nonzero.
        self.machine.memory[0x4000] = 0x55
        for opcode in (0xB1, 0xB9):
            self.assertEqual(self.step(bytes([0xED, opcode]), bc=0x0210), 16)
        self.machine.memory[0x4000] = 0

    def test_index_instructions(self) -> None:
        for prefix in (0xDD, 0xFD):
            for opcode, expected in DOCUMENTED_INDEX.items():
                with self.subTest(opcode=f"{prefix:02X} {opcode:02X}"):
                    self.assertEqual(self.step(bytes([prefix, opcode])), expected)
            for opcode in range(6, 256, 8):
                expected = 20 if 0x40 <= opcode <= 0x7F else 23
                with self.subTest(opcode=f"{prefix:02X} CB d {opcode:02X}"):
                    self.assertEqual(self.step(bytes([prefix, 0xCB, 0x10, opcode])), expected)

    def test_index_prefix_adds_one_fetch_to_other_instructions(self) -> None:
        self.assertEqual(self.step(b"\xFD\x15"), 8)                 # DEC D
        self.assertEqual(self.step(b"\xDD\x10", b=2), 17)           # DJNZ taken
        self.assertEqual(self.step(b"\xDD\x10", b=1), 12)
        self.assertEqual(self.step(b"\xDD\xED\xB0", bc=2), 25)      # LDIR repeating

    def test_run_totals_whole_routines(self) -> None:
        machine = self.machine
        for code, registers, expected in (
                (b"\xED\xB0", {"hl": 0x2000, "de": 0x3000, "bc": 128}, 127 * 21 + 16),
                (b"\xED\xB2", {"hl": 0x2000, "bc": 0x8010}, 127 * 21 + 16),
                (b"\x10\xFE", {"b": 128}, 127 * 13 + 8),
                # LD SP,(nn); POP BC; POP IY; LD (nn),SP; ADD IX,DE; LD SP,IX; PUSH IY
                (bytes.fromhex("ED7B0020" "C1" "FDE1" "ED730220" "DD19" "DDF9" "FDE5"),
                 {"ix": 0x6000}, 20 + 10 + 14 + 20 + 15 + 10 + 15)):
            with self.subTest(code=code.hex()):
                machine.memory[0x2000:0x2002] = b"\x00\x50"
                machine.memory[CODE:CODE + len(code) + 1] = code + b"\x76"
                machine.halted = False
                machine.pc, machine.sp = CODE, 0xF000
                for name, value in registers.items():
                    setattr(machine, name, value)
                machine.set_breakpoint(CODE + len(code))
                machine.ticks_to_stop = 100000
                machine.run()
                machine.clear_breakpoints()
                self.assertEqual(machine.pc, CODE + len(code))
                self.assertEqual(100000 - machine.ticks_to_stop, expected)


class Z80Banks(unittest.TestCase):
    def setUp(self) -> None:
        self.machine = z80.Z80Machine()

    def mirror(self, code: bytes) -> None:
        """Both banks hold the program, as the SBC's common code page does."""
        self.machine.bank(1)[CODE:CODE + len(code) + 1] = code + b"\x76"

    def test_out_port_7_selects_bank_and_isolates_memory(self) -> None:
        code = bytes.fromhex(
            "3E01" "D307" "3E55" "32008A" "AF" "D307" "3E11" "32008A")
        self.mirror(code)
        run(self.machine, code)
        self.assertEqual(self.machine.active_bank, 0)
        self.assertEqual(self.machine.bank(0)[0x8A00], 0x11)
        self.assertEqual(self.machine.bank(1)[0x8A00], 0x55)
        self.assertEqual(self.machine.memory[0x8A00], 0x11)

    def test_program_in_the_other_bank_runs_after_a_switch(self) -> None:
        self.machine.bank(1)[CODE + 4:CODE + 7] = b"\x3E\x77\x76"
        run(self.machine, bytes.fromhex("3E01D307"))
        self.assertEqual((self.machine.active_bank, self.machine.a), (1, 0x77))

    def test_select_bank_api_and_parked_view(self) -> None:
        self.machine.memory[0x4000] = 0xAA
        self.machine.select_bank(1)
        self.machine.memory[0x4000] = 0xBB
        self.assertEqual(self.machine.bank(0)[0x4000], 0xAA)
        self.assertEqual(self.machine.bank(1)[0x4000], 0xBB)
        self.machine.select_bank(0)
        self.assertEqual(self.machine.memory[0x4000], 0xAA)


if __name__ == "__main__":
    unittest.main(verbosity=2)
