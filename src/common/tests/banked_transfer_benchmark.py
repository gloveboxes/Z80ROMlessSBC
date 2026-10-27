"""Measure BIOS transfer T-states; excludes Pico traps and memory wait states."""
from pathlib import Path
import argparse
import json

import z80
from banked_memory_z80_test import transfer_code


def measure(code: bytes, writing: bool, length: int) -> dict:
    machine = z80.Z80Machine()
    banks = [bytearray(65536), bytearray(65536)]
    payload = bytes(i & 255 for i in range(length))
    base, bank = 0x2000, 0x4000
    for memory in banks:
        memory[0xFF00:0xFF00 + len(code)] = code
    banks[0][base:base + length] = payload if writing else bytes(length)
    banks[1][bank:bank + length] = bytes(length) if writing else payload
    banks[0][0x1000:0x1004] = bytes((0xCD, 3 if writing else 0, 0xFF, 0x76))
    machine.memory[:] = banks[0]
    machine.pc, machine.sp = 0x1000, 0xE000
    machine.hl, machine.de, machine.bc = base, bank, length
    machine.ix, machine.iy = 0x4567, 0x89AB
    active = switches = ticks = 0

    def output(port: int, value: int) -> None:
        nonlocal active, switches
        assert port & 0x17 == 7
        banks[active][:] = machine.memory
        active = value & 1
        machine.memory[:] = banks[active]
        switches += 1

    machine.set_output_callback(output)
    # Stop at the return address, before HALT can consume the remaining budget.
    machine.set_breakpoint(0x1003)
    for _ in range(1000):
        # Keep the budget larger than the measured runs so a ticks-limit
        # stop cannot clamp instruction overshoot and skew the counter.
        machine.ticks_to_stop = 1000000
        events = machine.run()
        ticks += 1000000 - machine.ticks_to_stop
        if events & machine._BREAKPOINT_HIT:
            break
    else:
        raise AssertionError("benchmark exceeded execution budget")
    actual = banks[1][bank:bank + length] if writing else machine.memory[base:base + length]
    assert bytes(actual) == payload
    assert machine.a == 0 and not machine.f & 1
    assert (machine.pc, machine.sp, machine.ix, machine.iy) == (0x1003, 0xE000, 0x4567, 0x89AB)
    assert active == 0 and switches == 2 * (length // 16)
    return {"direction": "write" if writing else "read", "bytes": length,
            "tstates": ticks, "us_at_20mhz_without_traps": ticks / 20,
            "bank_switches": switches}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, help="Earlier assembled BIOS extension binary")
    args = parser.parse_args()
    current = transfer_code()
    report = {"current_code_bytes": len(current), "measurements": []}
    reference = args.reference.read_bytes() if args.reference else None
    if reference is not None:
        report["reference_code_bytes"] = len(reference)
    for length in (16, 256, 4096):
        for writing in (False, True):
            row = measure(current, writing, length)
            if reference is not None:
                before = measure(reference, writing, length)
                row["reference_tstates"] = before["tstates"]
                row["saved_tstates"] = before["tstates"] - row["tstates"]
            report["measurements"].append(row)
    print(json.dumps(report, indent=2))
