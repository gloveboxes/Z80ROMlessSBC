"""Measure linked BANKMEM entry points, excluding initialization and Pico waits."""

import argparse
import json
from pathlib import Path
import re

from banked_allocator_v2_z80_test import HighResidentAllocator, ROOT


def measure(image: Path, writing: bool, chunk: int, interrupts: bool = False) -> dict:
    fixture = HighResidentAllocator()
    fixture.setUp()
    machine = fixture.machine
    code = image.read_bytes()
    symbols = dict((name.upper(), int(address, 16)) for address, name in
                   re.findall(r"\b([0-9A-Fa-f]{4})\s+(_\w+)\b",
                              image.with_suffix(".SYM").read_text()))
    machine.memory[0x100:0x100 + len(code)] = code
    machine.clear_breakpoint(0x1003)
    machine.set_breakpoint(0x30)
    switches = 0

    def output(port: int, value: int) -> None:
        nonlocal switches
        switches += 1
        fixture.output(port, value)

    machine.set_output_callback(output)

    def call(name: str, *arguments: int) -> tuple[int, int]:
        machine.pc, machine.sp = symbols[name], 0xD000
        machine.ix, machine.iy = 0x4567, 0x89AB
        machine.iff1 = machine.iff2 = interrupts
        frame = b"\x30\x00" + b"".join(n.to_bytes(2, "little") for n in arguments)
        machine.memory[0xD000:0xD000 + len(frame)] = frame
        ticks = 0
        for _ in range(100):
            machine.ticks_to_stop = 1000000
            events = machine.run()
            ticks += 1000000 - machine.ticks_to_stop
            if events & machine._BREAKPOINT_HIT:
                assert machine.pc == 0x30 and machine.sp == 0xD002
                assert fixture.active == 0
                assert (machine.ix, machine.iy) == (0x4567, 0x89AB)
                assert bool(machine.iff1) == bool(machine.iff2) == interrupts
                return machine.hl, ticks
        raise AssertionError(f"{name} did not return")

    assert call("_BINIT")[0] == 1
    handle = call("_BMALL", 4096)[0]
    assert handle == 0xC800
    payload = bytes(((i * 73) ^ (i >> 8)) & 255 for i in range(4096))
    machine.memory[0x9000:0xA000] = payload if writing else b"\xEE" * 4096
    fixture.banks[1][handle:handle + 4096] = b"\xEE" * 4096 if writing else payload
    switches = ticks = 0
    for offset in range(0, 4096, chunk):
        result, elapsed = call("_BWRIT" if writing else "_BREAD",
                               handle, offset, 0x9000 + offset, chunk)
        assert result == 1
        ticks += elapsed
    actual = (fixture.banks[1][handle:handle + 4096] if writing else
              machine.memory[0x9000:0xA000])
    assert bytes(actual) == payload
    assert int.from_bytes(machine.memory[symbols["_BMRES"]:symbols["_BMRES"] + 2],
                          "little") == 0
    return {"direction": "write" if writing else "read", "chunk_bytes": chunk,
            "bytes": 4096, "tstates": ticks, "bank_switches": switches}


def measure_services() -> list[dict]:
    fixture = HighResidentAllocator()
    fixture.setUp()
    rows = []
    for name, operation, argument, stack, expected in (
            ("allocate", 0, 256, 0, 0xC800),
            ("size", 2, 0xC800, 0, 256),
            ("pinned_size", 2, 0x0100, 0, 0xBF00),
            ("available", 3, 0, 0, 0x1500),
            ("call", 6, 0x0100, 0xC000, 0xBEEF),
            ("free", 1, 0xC800, 0, 1)):
        result = fixture.call(operation, argument, stack)
        assert result == expected
        rows.append({"operation": name, "result": result,
                     "tstates": 1000000 - fixture.machine.ticks_to_stop})
    return rows


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path,
                        default=ROOT / "examples/MEMPERF/build/MEMPERF.COM")
    parser.add_argument("--services", action="store_true",
                        help="Measure resident alloc/size/available/call/free instead")
    args = parser.parse_args()
    HighResidentAllocator.setUpClass()
    print(json.dumps(measure_services() if args.services else
                     [measure(args.image, writing, chunk)
                      for chunk in (4096, 256, 16, 1)
                      for writing in (False, True)], indent=2))
