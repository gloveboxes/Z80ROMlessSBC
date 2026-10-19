"""Execute the actual resident image in an independent Z80 instruction core."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

import z80


class MathSoakExecution(unittest.TestCase):
    image_builder: Path
    image: bytes
    scope_only = False

    @classmethod
    def setUpClass(cls) -> None:
        with tempfile.TemporaryDirectory(prefix="z80-math-soak-") as directory:
            image = Path(directory) / "resident.bin"
            subprocess.run(
                [str(cls.image_builder), "--emit-image", str(image)],
                check=True,
                timeout=15,
            )
            cls.image = image.read_bytes()
        if len(cls.image) != 0x2000:
            raise AssertionError("Resident image must contain exactly 8192 bytes")

    def setUp(self) -> None:
        self.machine = z80.Z80Machine()
        self.machine.memory[:] = b"\xEE" * 65536
        self.machine.memory[:0x2000] = self.image
        self.phases = 0
        self.packet: list[int] = []
        self.phase_base = 0
        self.machine.set_output_callback(self.output)
        self.machine.set_input_callback(self.unexpected_input)

    def unexpected_input(self, port: int) -> int:
        raise AssertionError(f"Unexpected IN from {port:04x}")

    def output(self, port: int, value: int) -> None:
        if self.scope_only:
            self.fail(f"Scope workload executed OUT {port:04x}, {value:02x}")
        self.assertEqual(port & 0x17, 6)
        if self.packet or value == 0xE1:
            self.packet.append(value)
            self.assertLessEqual(len(self.packet), 9)
            return
        self.assertEqual(value, 0x40 + self.phases % 4)
        self.complete_phase()

    def complete_phase(self) -> None:
        pattern = self.phases % 4
        expected = bytes(
            ((address >> 8 if pattern & 1 else address) ^
             (0xFF if pattern & 2 else 0)) & 0xFF
            for address in range(0x2000, 65536)
        )
        self.assertEqual(bytes(self.machine.memory[0x2000:]), expected)
        self.assertEqual(bytes(self.machine.memory[0x1000:0x1A00]),
                         self.image[0x1000:0x1A00])
        self.assertEqual(self.machine.memory[0x1A00], 0xA5)
        self.phases += 1

    def execute(self, phases: int, corrupt_address: int | None = None) -> None:
        injected = False
        if corrupt_address is not None or self.scope_only:
            def write(address: int, value: int) -> None:
                nonlocal injected
                self.machine.memory[address] = value
                if (corrupt_address is not None and address == 0xFFFF
                        and not injected):
                    self.machine.memory[corrupt_address] ^= 1
                    injected = True
                if self.scope_only:
                    mailbox = self.machine.memory[0x0F00:0x0F20]
                    if (0x0F06 <= address <= 0x0F0A or
                            0x0F0C <= address <= 0x0F13):
                        self.assertEqual(mailbox[5], 1)
                    if address == 0x0F05 and value == 0:
                        self.assertEqual(bytes(mailbox[:4]), b"SCP1")
                        if mailbox[4] == 0xE1:
                            self.packet = [0xE1, *mailbox[12:20]]
                        else:
                            self.assertEqual(mailbox[4], 1)
                            count = int.from_bytes(mailbox[6:10], "little")
                            self.assertEqual(count, self.phase_base + self.phases + 1)
                            self.assertEqual(mailbox[10], self.phases % 4)
                            self.complete_phase()
            self.machine.set_write_callback(write)
        for _ in range(4000):
            self.machine.ticks_to_stop = 100000
            self.machine.run()
            if len(self.packet) == 9 or self.phases >= phases:
                if corrupt_address is not None:
                    self.assertTrue(injected)
                if self.scope_only and self.packet:
                    self.assertTrue(self.machine.halted)
                return
            self.assertFalse(self.machine.halted)
        self.fail("Z80 workload exceeded its bounded execution budget")

    def test_two_complete_cycles(self) -> None:
        self.execute(8)
        self.assertEqual(self.packet, [])
        self.assertEqual(self.phases, 8)

    def test_scope_counter_low_word_rollover(self) -> None:
        if not self.scope_only:
            self.skipTest("Scope-only SRAM counter")
        self.phase_base = 65532
        self.machine.memory[0x0F06:0x0F0A] = self.phase_base.to_bytes(4, "little")
        self.machine.memory[0x0F0A] = 3
        self.execute(4)
        self.assertEqual(
            int.from_bytes(self.machine.memory[0x0F06:0x0F0A], "little"), 65536
        )

    def test_every_arithmetic_and_comparison_failure(self) -> None:
        for offset, stage in (
            (4, 1), (6, 2), (7, 3), (9, 4), (10, 5), (12, 6),
            (13, 7), (15, 8), (16, 9), (18, 10), (19, 11),
        ):
            with self.subTest(stage=stage):
                self.setUp()
                self.machine.memory[0x1000 + offset] ^= 1
                self.execute(1)
                self.assertEqual(self.phases, 0)
                self.assertEqual(self.packet[:5], [0xE1, stage, 0, 0, 0x10])
                self.assertNotEqual(self.packet[5:7], self.packet[7:9])

    def test_stack_guard_failure(self) -> None:
        self.machine.memory[0x1A00] ^= 1
        self.execute(1)
        self.assertEqual(self.phases, 0)
        self.assertEqual(self.packet[1], 16)

    def test_first_middle_last_sram_failure(self) -> None:
        for address in (0x2000, 0x8000, 0xFFFF):
            with self.subTest(address=f"{address:04x}"):
                self.setUp()
                self.execute(1, address)
                self.assertEqual(self.phases, 0)
                self.assertEqual(
                    self.packet[:5],
                    [0xE1, 0x80, 0xFF, address & 0xFF, address >> 8],
                )
                self.assertNotEqual(self.packet[5:7], self.packet[7:9])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-builder", required=True, type=Path)
    parser.add_argument("--scope", action="store_true")
    arguments = parser.parse_args()
    MathSoakExecution.image_builder = arguments.image_builder
    MathSoakExecution.scope_only = arguments.scope
    unittest.main(argv=["math_soak_z80_test"], verbosity=2)
