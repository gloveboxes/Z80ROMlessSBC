"""Real Z80 extent allocator versus an independent interval model."""
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

import z80
from banked_memory_z80_test import initialize_bank
from extent_model import ExtentModel


class ResidentAllocator(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'heap.bin'
            subprocess.run(['z80asm', '-o', str(binary), str(root / 'src/common/banked_allocator.asm')], check=True)
            cls.code = binary.read_bytes()
        assert len(cls.code) <= 0x2C0
        assert initialize_bank()[0x500:0x500 + len(cls.code)] == cls.code

    def setUp(self):
        self.m = z80.Z80Machine()
        self.banks = [initialize_bank(), initialize_bank()]
        self.banks[1][0x800:0xFF00] = bytes([0xA5]) * 63232
        self.model = ExtentModel()
        self.active = 0
        self.m.set_output_callback(self.output)
        self.m.set_breakpoint(0x1003)

    def output(self, port, value):
        self.assertEqual(port & 255, 7)
        self.assertFalse(self.m.iff1)
        self.banks[self.active][:] = self.m.memory
        self.active = value & 1
        self.switches.append(self.active)
        self.m.memory[:] = self.banks[self.active]

    def call(self, op, value=0, enabled=True, expected_result=None,
             stack_handle=0, allow_stack_changes=False,
             entry_offset=0x4567, argument=0x89AB):
        expected = self.model.call(op, value)
        if expected_result is not None:
            expected = expected_result
        m = self.m
        self.banks[0][0x1000:0x1003] = bytes((0xCD, 0x10, 0xFF))
        m.memory[:] = self.banks[0]
        m.pc, m.sp, m.hl, m.e, m.bc = 0x1000, 0xE000, value, op, stack_handle
        m.ix, m.iy = entry_offset, argument
        m.iff1 = m.iff2 = enabled
        self.switches = []
        base_before = bytes(m.memory)
        payload_before = bytes(self.banks[1][0x800:0xFF00])
        m.ticks_to_stop = 1000000
        for _ in range(100):
            if m.run() & m._BREAKPOINT_HIT:
                break
        else:
            self.fail('heap exceeded instruction budget')
        self.last_ticks = 1000000 - m.ticks_to_stop
        self.assertEqual(m.hl, expected, (op, value, self.model.extents))
        self.assertEqual(self.switches, [1, 0])
        self.assertEqual((self.active, m.sp, m.ix, m.iy),
                         (0, 0xE000, entry_offset, argument))
        self.assertEqual((bool(m.iff1), bool(m.iff2)), (enabled, enabled))
        self.assertEqual(bytes(m.memory[:0xDFFC]), base_before[:0xDFFC])
        self.assertEqual(bytes(m.memory[0xE000:]), base_before[0xE000:])
        self.assertEqual(bytes(self.banks[1][:8]), b"BKM3BEX1")
        self.banks[0][:] = m.memory
        payload_after = bytes(self.banks[1][0x800:0xFF00])
        if allow_stack_changes:
            start = stack_handle - 0x800
            end = start + self.model.call(2, stack_handle)
            self.assertEqual(payload_after[:start], payload_before[:start])
            self.assertEqual(payload_after[end:], payload_before[end:])
        else:
            self.assertEqual(payload_after, payload_before)
        self.assertEqual(self.banks[1][0x500:0x500 + len(self.code)], self.code)
        self.invariants()
        return expected

    def invariants(self):
        bank = self.banks[1]
        expected = bytearray(65536)
        self.model.write_tables(expected)
        self.assertEqual(bank[0x10], expected[0x10])
        self.assertEqual(bank[0x14:0x16], expected[0x14:0x16])
        starts = set()
        for start, _, free in self.model.extents:
            starts.add(start)
            for table in (0x100, 0x200, 0x300):
                self.assertEqual(bank[table + start], expected[table + start], (hex(table), start))
            if free:
                self.assertEqual(bank[0x400 + start], expected[0x400 + start])
        for index in range(8, 255):
            if index not in starts:
                self.assertEqual(bank[0x100 + index], 0, index)

    def test_boundaries_exhaustion_and_reset(self):
        for value in (0, 1, 255, 256, 257, 63231, 63232, 63233, 65535):
            for enabled in (False, True):
                self.call(4)
                handle = self.call(0, value, enabled)
                if handle:
                    self.call(2, handle)
                    self.call(1, handle, enabled)
        for _ in range(247):
            self.call(0, 1)
        self.call(0, 1)
        self.call(3)
        self.call(4)
        self.call(0, 63232)
        self.call(4)

    def test_coalescing_and_invalid_handles(self):
        self.call(6, 0x800)
        self.call(255, 0x800)
        a, b, c, d = [self.call(0, 4096) for _ in range(4)]
        for value in (1, 0x100, a + 1, a + 256, 0xFF00, 0xFFFF):
            self.call(1, value)
            self.call(2, value)
            self.call(5, value)
        self.call(1, 0)
        self.call(1, b)
        self.call(1, d)
        self.call(1, c)  # merge both neighbors
        self.call(1, c)  # stale handle into merged extent
        self.call(1, a)
        self.assertEqual(self.model.extents, [(8, 247, True)])

    def test_call_uses_private_stack_and_restores_index_registers(self):
        handle = self.call(0, 256)
        code = bytes((0xDD, 0x21, 0x34, 0x12,  # LD IX,1234h
                      0xFD, 0x21, 0x78, 0x56,  # LD IY,5678h
                      0x21, 0xEF, 0xBE,        # LD HL,BEEFh
                      0xC9))                    # RET
        self.banks[1][handle:handle + len(code)] = code
        self.call(5, handle, expected_result=0xBEEF)
        self.assertEqual(self.banks[1][0x12:0x14], bytes((0xFC, 0xDF)))

    def test_separate_stack_call_rejects_invalid_or_shared_handles(self):
        code = self.call(0, 256)
        self.call(5, code, stack_handle=code)
        self.call(5, code, stack_handle=code + 256)
        self.call(5, code + 1, stack_handle=code)
        self.call(1, code)

    def test_separate_stack_can_end_at_resident_page(self):
        code = self.call(0, 256)
        prefix = self.call(0, 245 * 256)
        stack = self.call(0, 256)
        self.assertEqual((code, prefix, stack), (0x0800, 0x0900, 0xFE00))
        self.banks[1][code:code + 4] = bytes((0x21, 0xEF, 0xBE, 0xC9))
        self.call(5, code, stack_handle=stack, expected_result=0xBEEF,
                  allow_stack_changes=True)
        self.assertNotEqual(bytes(self.banks[1][0xFEFE:0xFF00]), bytes((0xA5, 0xA5)))
        self.call(1, stack)
        self.call(1, prefix)
        self.call(1, code)

    def test_direct_offset_uses_dcc_argument_and_rejects_out_of_range_entry(self):
        code = self.call(0, 256)
        stack = self.call(0, 256)
        context = self.call(0, 256)
        # DCC prologue: PUSH IX; IX=SP; first pointer argument at IX+4.
        function = bytes((0xDD, 0xE5, 0xDD, 0x21, 0, 0, 0xDD, 0x39,
                          0xDD, 0x6E, 4, 0xDD, 0x66, 5, 0x7E,
                          0x6F, 0x26, 0, 0xDD, 0xE1, 0xC9))
        self.banks[1][code:code + len(function)] = function
        self.banks[1][code + 0x20:code + 0x20 + len(function) + 1] = (
            function[:14] + bytes((0x23,)) + function[14:])  # INC HL before LD A,(HL)
        self.banks[1][context:context + 2] = bytes((42, 73))
        self.call(6, code, stack_handle=stack, entry_offset=0,
                  argument=context, expected_result=42, allow_stack_changes=True)
        self.call(6, code, stack_handle=stack, entry_offset=0x20,
                  argument=context, expected_result=73, allow_stack_changes=True)
        for offset, bad_stack in ((0, 0), (0, code), (256, stack),
                                  (65535, stack)):
            self.call(6, code, stack_handle=bad_stack, entry_offset=offset,
                      argument=context)
        self.call(1, context)
        self.call(1, stack)
        self.call(1, code)

    def test_maximum_fragmentation(self):
        handles = [self.call(0, 256) for _ in range(247)]
        for handle in handles[::2]:
            self.call(1, handle)
        self.assertEqual(self.call(0, 512), 0)
        self.call(1, handles[1])
        merged = self.call(0, 768)
        self.assertEqual(merged, 0x800)
        self.call(1, merged)
        for handle in handles[3::2]:
            self.call(1, handle)
        self.assertEqual(self.model.extents, [(8, 247, True)])

    def test_random_fragmentation(self):
        rng = random.Random(8080)
        for _ in range(5000):
            live = [start * 256 for start, _, free in self.model.extents if not free]
            if live and rng.randrange(3) == 0:
                self.call(1, rng.choice(live), bool(rng.randrange(2)))
            else:
                self.call(0, rng.choice((1, 256, 257, 1024, 4096, 32768)), bool(rng.randrange(2)))
            if rng.randrange(20) == 0:
                self.call(3)
                self.call(2, rng.choice(live) if live else 0)
        for start, _, free in list(self.model.extents):
            if not free:
                self.call(1, start * 256)
        self.assertEqual(self.model.extents, [(8, 247, True)])

    def test_large_operations_do_not_walk_payload_blocks(self):
        ticks = []
        for size in (256, 32768):
            self.call(4)
            prefix = self.call(0, 16384)
            handle = self.call(0, size)
            alloc_ticks = self.last_ticks
            self.call(1, handle)
            ticks.append((alloc_ticks, self.last_ticks))
            self.call(1, prefix)
        self.assertEqual(ticks[0], ticks[1])
        print('Z80 cycles for alloc/free with occupied prefix:', ticks)


if __name__ == '__main__':
    unittest.main(verbosity=2)
