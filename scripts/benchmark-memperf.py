"""Measure actual Z80 cycles in MEMPERF timer intervals, excluding Pico waits."""
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/common/tests"))
import z80
from banked_heap_z80_test import BankedHeapExecution


def measure(program):
    factory = z80.Z80Machine
    machines = []

    def instrumented_machine():
        machine = factory()
        run_cpu, set_output = machine.run, machine.set_output_callback
        state = {"ticks": 0, "budget": 0, "start": None, "intervals": []}

        def run():
            state["budget"] = machine.ticks_to_stop
            events = run_cpu()
            state["ticks"] += state["budget"] - machine.ticks_to_stop
            return events

        def install_output(callback):
            def output(port, value):
                if port & 0x17 == 0x17:
                    ticks = state["ticks"] + state["budget"] - machine.ticks_to_stop
                    if value == 0x40:
                        state["start"] = ticks
                    elif value == 0x60 and state["start"] is not None:
                        state["intervals"].append(ticks - state["start"])
                        state["start"] = None
                callback(port, value)
            set_output(output)

        machine.run = run
        machine.set_output_callback = install_output
        machines.append(state)
        return machine

    z80.Z80Machine = instrumented_machine
    try:
        case = BankedHeapExecution()
        case.program = program
        output, _, _, _ = case.execute_program(
            True, stopwatch_ms=(64, 65, 34, 35, 512, 513, 36, 37))
        assert b"PASS: MEMPERF complete" in output
        intervals = machines[-1]["intervals"]
        assert len(intervals) == 8
    finally:
        z80.Z80Machine = factory
    labels = ("bank_bulk_write", "bank_bulk_read", "base_bulk_write", "base_bulk_read",
              "bank_256_write", "bank_256_read", "base_256_write", "base_256_read")
    return {"program": str(program), "measurements": {
        label: {"tstates": ticks, "ms_at_20mhz_without_pico_waits": round(ticks / 20000, 4)}
        for label, ticks in zip(labels, intervals)}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("programs", nargs="*", type=Path,
                        default=[ROOT / "examples/MEMPERF/build/MEMPERF.COM"])
    args = parser.parse_args()
    BankedHeapExecution.setUpClass()
    try:
        print(json.dumps([measure(program) for program in args.programs], indent=2))
    finally:
        BankedHeapExecution.tearDownClass()
