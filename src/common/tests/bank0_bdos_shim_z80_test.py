"""Bank-0 BDOS file calls are forwarded to bank-1 BDOS with DMA copying."""

import unittest

import cpm_boot_z80_test
import bank1_system_boot_z80_test
from banked_allocator_v2_z80_test import assemble


class ShimFileRead(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cpm_boot_z80_test.CpmBootExecution.setUpClass()
        cls.transfer = assemble("src/common/banked_transfer_v2.asm")
        cls.shim = assemble("src/cpm/bank0_bdos_shim.asm")
        cls.payload = assemble("src/common/tests/bank0_bdos_shim_file.asm")

    def test_file_read_into_bank_zero_dma(self) -> None:
        case = cpm_boot_z80_test.CpmBootExecution()
        case.setUp()
        machine = case.machine
        base = bytearray(machine.memory)
        extra = bytearray(65536)
        base[0xEE00:0xEE00 + len(self.shim)] = self.shim
        extra[0xEE00:0xFF00] = case.image[0xEE00:0xFF00]
        extra[5:8] = bytes((0xC3, 0x06, 0xEE))
        extra[0xDE00:0xE600] = bank1_system_boot_z80_test.generated_array("bank1_service_code")
        extra[0xE600:0xEE00] = bank1_system_boot_z80_test.generated_array("bank1_resident_code")
        for bank in (base, extra):
            bank[0xFF00:0xFF00 + len(self.transfer)] = self.transfer
        banks = [base, extra]
        active = 0

        def output(port: int, value: int) -> None:
            nonlocal active
            if port & 255 == 7:
                banks[active][:] = machine.memory
                active = value & 1
                machine.memory[:] = banks[active]
            else:
                case.write_port(port, value)

        machine.set_output_callback(output)
        machine.set_input_callback(lambda port: 0xB4 | active if port & 255 == 7
                                   else case.read_port(port))
        machine.memory[:] = base
        case.until_prompt()
        self.assertEqual(active, 0)
        self.assertIn(b"DUMP", case.command("DIR"))
        machine.memory[0x1000:0x1000 + len(self.payload)] = self.payload
        machine.pc, machine.sp = 0x1000, 0xD000
        machine.set_breakpoint(0x1100)
        machine.ticks_to_stop = 20_000_000
        for _ in range(100):
            if machine.run() & machine._BREAKPOINT_HIT:
                break
        else:
            self.fail("bank-0 BDOS shim did not return")
        self.assertEqual((active, machine.pc, machine.hl), (0, 0x1100, 0))
        self.assertEqual(machine.memory[0x180:0x200], case.autorun[:128])


if __name__ == "__main__":
    unittest.main()
