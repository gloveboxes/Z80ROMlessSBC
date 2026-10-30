"""Execute a bank-switching probe in the project's independent Z80 emulator.

The emulator exposes a flat 64 KiB array. The OUT callback swaps that array
with one of two 64 KiB SRAM banks, modeling the proposed A16 pin behavior.
This checks software-visible bank isolation; it does not model bus timing.
"""

from __future__ import annotations

import unittest
from pathlib import Path
import re

import z80
from banked_allocator_v2_z80_test import assemble
from functools import cache


@cache
def shim_code() -> bytes:
    return assemble("src/cpm/bank0_bdos_shim.asm")[0xA00:]


def transfer_code() -> bytes:
    header = (Path(__file__).resolve().parents[1] /
              "include/z80sbc/banked_transfer.h").read_text()
    return bytes(int(value, 16) for value in
                 re.findall(r"0x([0-9A-F]{2})", header.split("= {", 1)[1]))


def initialize_bank() -> bytearray:
    bank = bytearray(65536)
    bank[:8] = b"BKM3BEX1"
    bank[8] = 3  # Optional-stack CALL revision loaded by current firmware
    header = (Path(__file__).resolve().parents[1] /
              "include/z80sbc/banked_allocator.h").read_text()
    allocator = bytes(int(value, 16) for value in
                      re.findall(r"0x([0-9A-F]{2})", header.split("= {", 1)[1]))
    bank[0x500:0x500 + len(allocator)] = allocator
    bank[0x10] = 8
    bank[0x15] = bank[0x108] = 247
    code = transfer_code()
    bank[0xFF00:0xFF00 + len(code)] = code
    bank[0xF800:0xFB40] = shim_code()
    return bank


class BankedMemoryExecution(unittest.TestCase):
    def call_bios(self, entry: int, base: int = 0x2000, bank: int = 0x100,
                  length: int = 16, capability: int = 0xB4) -> z80.Z80Machine:
        machine = z80.Z80Machine()
        machine.memory[:] = initialize_bank()
        machine.memory[0x1000:0x1004] = bytes((0xCD, entry & 255, entry >> 8, 0x76))
        machine.pc, machine.sp = 0x1000, 0xE000
        machine.hl, machine.de, machine.bc = base, bank, length
        machine.ix, machine.iy = 0x4567, 0x89AB
        machine.iff1 = machine.iff2 = True
        machine.set_input_callback(lambda port: capability)
        machine.set_output_callback(lambda port, value: self.fail("invalid call switched banks"))
        for _ in range(10):
            machine.ticks_to_stop = 10000
            machine.run()
            if machine.halted:
                break
        self.assertTrue(machine.halted)
        self.assertEqual((machine.sp, machine.ix, machine.iy), (0xE000, 0x4567, 0x89AB))
        self.assertTrue(machine.iff1 and machine.iff2)
        return machine

    def test_bios_info_and_descriptor(self) -> None:
        bank = initialize_bank()
        self.assertEqual(bytes(bank[0xFF09:0xFF10]), b"BMX2\x02\x06\x10")
        self.assertEqual(bytes(bank[0xFF00:0xFF09:3]), b"\xC3" * 3)
        machine = self.call_bios(0xFF06)
        self.assertEqual((machine.a, machine.hl, machine.de, machine.bc), (0, 0x206, 0xC800, 5632))
        self.assertFalse(machine.f & 1)
        for capability in (0, 0xB0, 0xB2):
            with self.subTest(capability=capability):
                machine = self.call_bios(0xFF06, capability=capability)
                self.assertEqual((machine.a, machine.hl), (1, 0))
                self.assertTrue(machine.f & 1)

    def test_bios_rejects_alignment_overflow_and_resident_page(self) -> None:
        for entry in (0xFF00, 0xFF03):
            for base, bank, length in ((0x2000, 0x100, 1), (0x80, 0x100, 16),
                                       (0xFEF1, 0x100, 16), (0x2000, 0xFEF1, 16),
                                       (0xFFF0, 0x100, 32), (0x2000, 0xFFF0, 32)):
                with self.subTest(entry=entry, base=base, bank=bank, length=length):
                    machine = self.call_bios(entry, base, bank, length)
                    self.assertEqual(machine.a, 2)
                    self.assertTrue(machine.f & 1)

    def test_bios_range_checks_reject_end_overflow_at_multiple_lengths(self) -> None:
        for entry in (0xFF00, 0xFF03):
            for length in (16, 32, 256, 4096, 0xFFF0):
                for start in (0, 0xFF, 0xFF00 - length + 1, 0x10000 - length, 0xFFFF):
                    if not 0 <= start <= 0xFFFF:
                        continue
                    for base, bank in ((start, 0x100), (0x2000, start)):
                        invalid = base < 0x100 or base + length > 0xFF00 or bank + length > 0xFF00
                        if not invalid:
                            continue
                        with self.subTest(entry=entry, base=base, bank=bank, length=length):
                            machine = self.call_bios(entry, base, bank, length)
                            self.assertEqual(machine.a, 2)
                            self.assertTrue(machine.f & 1)

    def test_register_bursts_preserve_stack_and_index_registers(self) -> None:
        for writing in (False, True):
            for enabled in (False, True):
                for length in (0, 16, 32, 48, 240, 256, 272, 512,
                               2032, 2048, 2064, 4080, 4096, 4112, 0x8010, 0xFE00):
                    end_start = 0xFF00 - max(length, 16)
                    ranges = (((0x100, 0x100), (0x100, 0)) if length == 0xFE00 else
                              ((0x2000, 0x100), (0x2001, 0x1235),
                               (0x2000, end_start), (end_start, 0x100),
                               (end_start, end_start), (end_start - 1, end_start - 3)))
                    for base, address in ranges:
                        with self.subTest(writing=writing, enabled=enabled,
                                          base=base, address=address, length=length):
                            m = z80.Z80Machine()
                            banks = [initialize_bank(), initialize_bank()]
                            active = 0
                            switches = []
                            payload = bytes(((i * 73) ^ (i >> 8) ^ (i >> 3)) & 255
                                            for i in range(length))
                            banks[0][base:base + length] = payload if writing else b"\xEE" * length
                            banks[1][address:address + length] = b"\xEE" * length if writing else payload
                            # The largest legal buffer covers 0100h..FEFFh.
                            # Keep the caller and its stack below that range.
                            banks[0][:4] = bytes((0xCD, 3 if writing else 0, 0xFF, 0x76))
                            banks[0][0x80:0x100] = bytes(range(128))
                            m.memory[:] = banks[0]
                            m.pc, m.sp, m.ix, m.iy = 0, 0x80, 0x4567, 0x89AB
                            m.hl, m.de, m.bc = base, address, length
                            m.iff1 = m.iff2 = enabled

                            def output(port: int, value: int) -> None:
                                nonlocal active
                                self.assertEqual(port & 0x17, 7)
                                banks[active][:] = m.memory
                                active = value & 1
                                switches.append(active)
                                m.memory[:] = banks[active]

                            m.set_output_callback(output)
                            for _ in range(500):
                                m.ticks_to_stop = 10000
                                m.run()
                                if m.halted:
                                    break
                            self.assertTrue(m.halted)
                            self.assertEqual(switches, [1, 0] * (length // 16))
                            self.assertEqual((m.sp, m.ix, m.iy), (0x80, 0x4567, 0x89AB))
                            self.assertEqual(m.a, 0)
                            self.assertFalse(m.f & 1)
                            self.assertEqual(bool(m.iff1), enabled)
                            self.assertEqual(bool(m.iff2), enabled)
                            self.assertEqual(bytes(m.memory[0x80:0x100]), bytes(range(128)))
                            actual = (banks[1][address:address + length] if writing else
                                      m.memory[base:base + length])
                            self.assertEqual(bytes(actual), payload)

    def test_raw_copy_cycle_budget(self) -> None:
        from banked_transfer_benchmark import measure
        for length, limit in ((16, 940), (256, 6310), (4096, 93120)):
            for writing in (False, True):
                with self.subTest(length=length, writing=writing):
                    result = measure(transfer_code(), writing, length)
                    self.assertLessEqual(result["tstates"], limit, result)

    def test_two_banks_and_live_z80_execution(self) -> None:
        machine = z80.Z80Machine()
        banks = [bytearray(65536), bytearray(65536)]
        active = 0
        finished: list[int] = []

        # LD A,11h; LD (8000h),A; LD A,1; OUT (07h),A;
        # LD A,22h; LD (8000h),A; LD A,0; OUT (07h),A;
        # LD A,(8000h); CP 11h; JP NZ,fail;
        # LD A,1; OUT (07h),A; LD A,(8000h); CP 22h; JP NZ,fail;
        # LD A,A5h; OUT (06h),A; HALT; fail: LD A,E1h; OUT (06h),A; HALT
        program = bytes((
            0x3E, 0x11, 0x32, 0x00, 0x80, 0x3E, 0x01, 0xD3, 0x07,
            0x3E, 0x22, 0x32, 0x00, 0x80, 0x3E, 0x00, 0xD3, 0x07,
            0x3A, 0x00, 0x80, 0xFE, 0x11, 0xC2, 0x2B, 0x00,
            0x3E, 0x01, 0xD3, 0x07, 0x3A, 0x00, 0x80, 0xFE, 0x22,
            0xC2, 0x2B, 0x00, 0x3E, 0xA5, 0xD3, 0x06, 0x76,
            0x3E, 0xE1, 0xD3, 0x06, 0x76,
        ))
        # Both banks contain the same probe so execution continues across OUT.
        banks[0][:len(program)] = program
        banks[1][:len(program)] = program
        machine.memory[:] = banks[0]

        def output(port: int, value: int) -> None:
            nonlocal active
            decoded = port & 0x17
            if decoded == 0x07:
                requested = value & 1
                banks[active][:] = machine.memory
                active = requested
                machine.memory[:] = banks[active]
            elif decoded == 0x06:
                finished.append(value)
            else:
                self.fail(f"Unexpected OUT {port:04x}, {value:02x}")

        machine.set_output_callback(output)
        machine.set_input_callback(lambda port: self.fail(f"Unexpected IN {port:04x}"))
        for _ in range(100):
            machine.ticks_to_stop = 10000
            machine.run()
            if machine.halted:
                break
        self.assertTrue(machine.halted, "probe did not halt within budget")
        self.assertEqual(finished, [0xA5])
        self.assertEqual(banks[0][0x8000], 0x11)
        self.assertEqual(banks[1][0x8000], 0x22)


if __name__ == "__main__":
    unittest.main(verbosity=2)
