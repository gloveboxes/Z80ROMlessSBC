"""Proposed high-resident allocator and pinned library Z80 execution."""

from pathlib import Path
import random
import subprocess
import tempfile
import unittest

import z80


ROOT = Path(__file__).resolve().parents[3]


def assemble(source: str) -> bytes:
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "code.bin"
        subprocess.run(["z80asm", "-o", str(output), str(ROOT / source)],
                       check=True)
        return output.read_bytes()


class HighResidentAllocator(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.transfer = assemble("src/common/banked_transfer_v2.asm")
        cls.entry = assemble("src/common/banked_service_entry.asm")
        cls.allocator = assemble("src/common/banked_allocator_v2.asm")
        cls.context = assemble("src/common/banked_context.asm")
        cls.extensions = assemble("src/common/banked_extensions.asm")
        cls.descriptor = assemble("src/common/banked_descriptor.asm")
        cls.capabilities = assemble("src/common/banked_capabilities.asm")
        cls.shim = assemble("src/cpm/bank0_bdos_shim.asm")

    def setUp(self) -> None:
        self.machine = z80.Z80Machine()
        self.banks = [bytearray(65536), bytearray(65536)]
        for bank in self.banks:
            bank[0xFF00:0xFF00 + len(self.transfer)] = self.transfer
        self.banks[0][0xF800:0xFB40] = self.shim[0xA00:]
        bank = self.banks[1]
        bank[0xE600:0xE609] = b"BKM3BEX1\x06"
        bank[0xE640:0xE640 + len(self.entry)] = self.entry
        bank[0xE800:0xE800 + len(self.allocator)] = self.allocator
        bank[0xE780:0xE780 + len(self.context)] = self.context
        bank[0xEC00:0xEC00 + len(self.extensions)] = self.extensions
        bank[0xE390:0xE390 + len(self.descriptor)] = self.descriptor
        bank[0xE360:0xE360 + len(self.capabilities)] = self.capabilities
        bank[0xDF80] = 0x80
        bank[0xFC24] = 0xC9  # SETDMA stub; this fixture has no CP/M BIOS.
        bank[0x0100:0x0104] = bytes((0x21, 0xEF, 0xBE, 0xC9))
        bank[0xDE9F] = 1
        self.active = 0
        self.machine.set_output_callback(self.output)
        self.machine.set_input_callback(lambda port: 0xB4 if (port & 255) == 7 else 0)
        self.machine.set_breakpoint(0x1003)
        self.call(4)

    def output(self, port: int, value: int) -> None:
        self.assertEqual(port & 255, 7)
        self.banks[self.active][:] = self.machine.memory
        self.active = value & 1
        self.machine.memory[:] = self.banks[self.active]

    def call(self, operation: int, hl: int = 0, bc: int = 0,
             ix: int = 0, iy: int = 0) -> int:
        m = self.machine
        self.banks[0][0x1000:0x1003] = bytes((0xCD, 0x10, 0xFF))
        m.memory[:] = self.banks[0]
        m.pc, m.sp, m.hl, m.e, m.bc, m.ix, m.iy = (
            0x1000, 0xD000, hl, operation, bc, ix, iy)
        m.iff1 = m.iff2 = False
        m.ticks_to_stop = 1000000
        for _ in range(10):
            if m.run() & m._BREAKPOINT_HIT:
                break
        else:
            self.fail("allocator did not return")
        self.assertEqual((self.active, m.sp, m.pc), (0, 0xD000, 0x1003))
        self.banks[0][:] = m.memory
        return m.hl

    def test_pinned_library_and_stack(self) -> None:
        self.assertEqual(self.call(3), 0x1600)
        self.assertEqual(self.call(2, 0x0100), 0xBF00)
        self.assertEqual(self.call(2, 0xC000), 0x0800)
        self.assertEqual(self.call(1, 0x0100), 0)
        self.assertEqual(self.call(1, 0xC000), 0)
        self.assertEqual(self.call(6, 0x0100, 0xC000), 0xBEEF)
        self.assertEqual(self.call(3), 0x1600)

    def test_general_pool_and_reset(self) -> None:
        self.assertEqual(self.call(0, 1), 0xC800)
        self.assertEqual(self.call(0, 0x1500), 0xC900)
        self.assertEqual(self.call(3), 0)
        self.assertEqual(self.call(0, 1), 0)
        self.assertEqual(self.call(1, 0xC800), 1)
        self.assertEqual(self.call(1, 0xC900), 1)
        self.assertEqual(self.call(3), 0x1600)
        self.assertEqual(self.call(4), 1)
        self.assertEqual(self.call(2, 0x0100), 0xBF00)
        self.assertEqual(self.call(2, 0xC000), 0x0800)

    def test_fragmentation_matches_address_ordered_first_fit(self) -> None:
        rng = random.Random(0xB105)
        free = [(0xC8, 22)]
        live = {}
        for _ in range(400):
            if live and rng.randrange(3) == 0:
                start = rng.choice(tuple(live))
                length = live.pop(start)
                self.assertEqual(self.call(1, start << 8), 1)
                free.append((start, length))
                free.sort()
                merged = []
                for block, size in free:
                    if merged and merged[-1][0] + merged[-1][1] == block:
                        merged[-1] = (merged[-1][0], merged[-1][1] + size)
                    else:
                        merged.append((block, size))
                free = merged
            else:
                requested = rng.randrange(1, 7 * 256 + 1)
                need = (requested + 255) // 256
                fit = next((i for i, (_, size) in enumerate(free)
                            if size >= need), None)
                expected = 0
                if fit is not None:
                    start, size = free.pop(fit)
                    expected = start << 8
                    live[start] = need
                    if size > need:
                        free.insert(fit, (start + need, size - need))
                self.assertEqual(self.call(0, requested), expected)
                if expected:
                    self.assertEqual(self.call(2, expected), need << 8)
            self.assertEqual(self.call(3), sum(size for _, size in free) << 8)
        for start in tuple(live):
            self.assertEqual(self.call(1, start << 8), 1)
        self.assertEqual(self.call(3), 0x1600)

    def test_capability_record_and_distinct_failure_status(self) -> None:
        self.assertEqual(self.call(9), 0xE360)
        record = self.banks[1][0xE360:0xE390]
        self.assertEqual(int.from_bytes(record[:2], "little"), 48)
        self.assertEqual(int.from_bytes(record[4:6], "little"), 0x0206)
        self.assertEqual(self.call(0, 0), 0)
        self.assertEqual(self.call(11), 2)
        self.assertEqual(self.call(1, 0xC900), 0)
        self.assertEqual(self.call(11), 3)
        self.assertEqual(self.call(0, 0x1600), 0xC800)
        self.assertEqual(self.call(0, 1), 0)
        self.assertEqual(self.call(11), 4)
        self.banks[1][0xE608] = 5
        self.assertEqual(self.call(9), 0)
        self.assertEqual(self.call(11), 1)

    def control(self, action: int, fields: dict[int, int] | None = None,
                *, address: int = 0xDF20) -> int:
        request = bytearray(32)
        for offset, value in {0: 32, 2: 1, 4: action, **(fields or {})}.items():
            request[offset:offset + 2] = value.to_bytes(2, "little")
        self.banks[1][0xDF20:0xDF40] = request
        status = self.call(10, address)
        self.assertEqual(self.call(11), status)
        self.assertEqual(int.from_bytes(self.banks[1][0xDF26:0xDF28], "little"), status)
        return status

    def test_control_validation_load_errors_and_generation_exhaustion(self) -> None:
        self.assertEqual(self.control(2, address=0xDF21), 2)
        for fields in ({0: 31}, {2: 2}, {4: 256}, {4: 4}):
            self.assertEqual(self.control(2, fields), 2)
        self.assertEqual(self.control(0, {12: 0xC800}), 3)
        handle = self.call(0, 272)
        for loader_status, expected in ((2, 7), (3, 8), (1, 9), (0, 0)):
            self.banks[1][0xE000:0xE004] = bytes((0x21, loader_status, 0, 0xC9))
            self.assertEqual(self.control(0, {12: handle}), expected)
        self.assertEqual(int.from_bytes(self.banks[1][0xDFA0:0xDFA2], "little"), 4)
        self.assertEqual(self.call(8), 0)
        self.assertEqual(int.from_bytes(self.banks[1][0xDFA0:0xDFA2], "little"), 5)
        self.banks[1][0xDFA0:0xDFA2] = b"\xff\xff"
        self.assertEqual(self.control(0, {12: handle}), 11)
        self.assertEqual(self.control(1), 0)
        self.assertEqual(self.banks[1][0xDE9F], 0)
        self.assertEqual(self.banks[1][0xDFA0:0xDFA2], b"\xff\xff")

    def test_control_rejects_stale_epochs_and_preserves_function_results(self) -> None:
        handle = self.call(0, 272)
        bank = self.banks[1]
        bank[0xDFA0] = 1
        bank[0xE4D2:0xE4D6] = b"\x01\x0c\x20\x00"
        bank[handle:handle + 2] = b"\x02\x00"
        fields = {8: 1, 12: handle, 14: 37, 30: 1}
        bank[0x100:0x104] = b"\x21\x01\x00\xc9"
        self.assertEqual(self.control(3, fields), 0)
        self.assertEqual(bank[0xE4D6:0xE4D8], b"\x25\x00")
        bank[0x100:0x104] = b"\x21\x00\x00\xc9"
        self.assertEqual(self.control(3, fields), 6)
        self.assertEqual(self.call(6, 0x100, 0xC000), 0)
        self.assertEqual(self.call(11), 0)
        self.assertEqual(self.control(3, {**fields, 8: 2}), 10)
        self.assertEqual(self.control(3, {**fields, 10: 1}), 2)
        self.assertEqual(self.control(3, {**fields, 12: handle + 1}), 3)
        for length in (0, 257, 65535):
            bank[handle:handle + 2] = length.to_bytes(2, "little")
            self.assertEqual(self.control(3, fields), 2)
        self.assertEqual(self.call(4), 1)
        self.assertEqual(self.control(3, fields), 10)
        bank[0xDFA6:0xDFA8] = b"\xff\xff"
        self.assertEqual(self.call(4), 0)
        self.assertEqual(self.call(11), 11)
        self.assertEqual(bank[0xDFA6:0xDFA8], b"\xff\xff")

    def test_resident_code_budgets(self) -> None:
        for code, budget in ((self.transfer, 240), (self.entry, 100),
                             (self.context, 128), (self.allocator, 1024),
                             (self.extensions, 512), (self.descriptor, 112)):
            self.assertLessEqual(len(code), budget)
        self.assertEqual(len(self.capabilities), 48)
        self.assertEqual(len(self.shim), 0xD40)

    def test_library_dma_and_search_are_restored_between_calls(self) -> None:
        bank = self.banks[1]
        bank[0xFAF1:0xFAF3] = b"\x40\x03"
        host_search = bytes(range(28))
        bank[0xFB14:0xFB30] = host_search
        program = bytes((0x21, 0x23, 1, 0x22, 0xF1, 0xFA,
                         0x21, 0xAA, 0xBB, 0x22, 0x14, 0xFB,
                         0x21, 0xDD, 0xCC, 0x22, 0x18, 0xFB,
                         0x3E, 0xEE, 0x32, 0x1A, 0xFB,
                         0x21, 0x22, 0x11, 0x22, 0x29, 0xFB,
                         0x3E, 0x33, 0x32, 0x2B, 0xFB,
                         0x21, 1, 0, 0xC9))
        bank[0x100:0x100 + len(program)] = program
        self.assertEqual(self.call(6, 0x100, 0xC000), 1)
        self.assertEqual(bank[0xFAF1:0xFAF3], b"\x40\x03")
        expected = bytearray(host_search)
        expected[1] = 0xBB  # Non-search scratch is shared, not restored.
        self.assertEqual(bank[0xFB14:0xFB30], expected)
        self.assertEqual(bank[0xDF80:0xDF82], b"\x23\x01")
        self.assertEqual(bank[0xDF82:0xDF89],
                         b"\xAA\xDD\xCC\xEE\x22\x11\x33")
        bank[0x100:0x104] = bytes((0x2A, 0xF1, 0xFA, 0xC9))
        self.assertEqual(self.call(6, 0x100, 0xC000), 0x0123)
        self.assertEqual(bank[0xFB14:0xFB30], expected)

    def test_resident_hot_path_cycle_budgets(self) -> None:
        for operation, argument, stack, expected, limit in (
                (0, 256, 0, 0xC800, 1600),
                (2, 0xC800, 0, 256, 570),
                (2, 0x0100, 0, 0xBF00, 520),
                (3, 0, 0, 0x1500, 380),
                (6, 0x0100, 0xC000, 0xBEEF, 2950),
                (1, 0xC800, 0, 1, 1800)):
            with self.subTest(operation=operation, argument=argument):
                self.assertEqual(self.call(operation, argument, stack), expected)
                self.assertLessEqual(1000000 - self.machine.ticks_to_stop, limit)

    def test_size_checks_all_pages_and_extent_boundaries(self) -> None:
        self.assertEqual(self.call(0, 512), 0xC800)
        expected = {0x0100: 0xBF00, 0xC000: 0x0800, 0xC800: 512}
        for page in range(256):
            handle = page << 8
            with self.subTest(handle=handle):
                self.assertEqual(self.call(2, handle), expected.get(handle, 0))
                self.assertEqual(self.call(11), 0 if handle in expected else 3)
        for handle in expected:
            for offset in (1, 15, 127, 255):
                with self.subTest(handle=handle, offset=offset):
                    self.assertEqual(self.call(2, handle + offset), 0)
                    self.assertEqual(self.call(11), 3)
        self.assertEqual(self.call(1, 0xC800), 1)
        self.assertEqual(self.call(2, 0xC800), 0)
        self.assertEqual(self.call(11), 3)

    def copy_bytes(self, writing: bool, base: int, bank: int, length: int,
                   interrupts: bool = False) -> None:
        machine = self.machine
        address = 0xF803 if writing else 0xF800
        self.banks[0][0x1000:0x1003] = bytes((0xCD, address & 255, address >> 8))
        machine.memory[:] = self.banks[0]
        machine.pc, machine.sp = 0x1000, 0xD000
        machine.hl, machine.de, machine.bc = base, bank, length
        machine.ix, machine.iy = 0x1234, 0x5678
        machine.iff1 = machine.iff2 = interrupts
        machine.ticks_to_stop = 1000000
        for _ in range(10):
            if machine.run() & machine._BREAKPOINT_HIT:
                break
        else:
            self.fail(f"byte transfer did not return: PC={machine.pc:04x}")
        self.assertEqual((self.active, machine.pc, machine.sp), (0, 0x1003, 0xD000))
        self.assertEqual((machine.ix, machine.iy), (0x1234, 0x5678))
        self.assertEqual(bool(machine.iff1), interrupts)
        self.assertEqual(bool(machine.iff2), interrupts)
        self.banks[0][:] = machine.memory

    def test_byte_transfers_preserve_guards_and_interrupt_state(self) -> None:
        for length in (0, 1, 15, 16, 17, 127, 255, 256, 511):
            with self.subTest(length=length):
                payload = bytes(i & 255 for i in range(length))
                self.banks[0][0x4000:0x4000 + length] = payload
                self.banks[1][0xC800:0xCA02] = b"\xA5" * 514
                self.copy_bytes(True, 0x4000, 0xC801, length, True)
                self.assertEqual(self.machine.a, 0)
                self.assertEqual(self.banks[1][0xC801:0xC801 + length], payload)
                self.assertEqual(self.banks[1][0xC800], 0xA5)
                self.assertEqual(self.banks[1][0xC801 + length], 0xA5)
                self.banks[0][0x5000:0x5000 + length + 2] = b"\xCC" * (length + 2)
                self.copy_bytes(False, 0x5001, 0xC801, length)
                self.assertEqual(self.machine.a, 0)
                self.assertEqual(self.banks[0][0x5001:0x5001 + length], payload)
                self.assertEqual(self.banks[0][0x5000], 0xCC)
                self.assertEqual(self.banks[0][0x5001 + length], 0xCC)

    def test_byte_transfer_upper_boundary_and_invalid_ranges(self) -> None:
        self.banks[0][0x4000] = 0x69
        gateway = bytes(self.banks[1][0xFF00:0xFFF0])
        workspace = bytes(self.banks[1][0xFFF2:])
        self.copy_bytes(True, 0x4000, 0xFEFF, 1)
        self.assertEqual(self.machine.a, 0)
        self.assertEqual(self.banks[1][0xFEFF], 0x69)
        self.assertEqual(bytes(self.banks[1][0xFF00:0xFFF0]), gateway)
        self.assertEqual(bytes(self.banks[1][0xFFF2:]), workspace)
        self.copy_bytes(False, 0xFEFF, 0xFEFF, 1)
        self.assertEqual(self.machine.a, 0)
        self.assertEqual(self.banks[0][0xFEFF], 0x69)
        for base, bank, count in ((0xFF, 0xC800, 1), (0xFEFF, 0xC800, 2),
                                  (0x4000, 0xFEFF, 2), (0x4000, 0xFFFF, 2)):
            with self.subTest(base=base, bank=bank, count=count):
                before = bytes(self.banks[1])
                self.copy_bytes(True, base, bank, count)
                self.assertEqual(self.machine.a, 2)
                self.assertEqual(bytes(self.banks[1]), before)
        self.copy_bytes(False, 0, 0xFFFF, 0)
        self.assertEqual(self.machine.a, 0)


if __name__ == "__main__":
    unittest.main()
