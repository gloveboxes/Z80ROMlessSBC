"""Unit tests for the C Z80 emulator itself (x80 core plus two-bank SRAM).

Every other Z80 test trusts this emulator. These tests check its documented
flag behavior against an independent Python reference, and its bank model,
control and I/O hooks, before any firmware or CP/M result is believed.
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
