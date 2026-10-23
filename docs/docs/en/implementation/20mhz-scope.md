# Continuous 20 MHz CPU/SRAM Scope Test

`z80_stage06_scope_20mhz` is an isolated diagnostic for observing the clock,
instruction fetches and SRAM activity **without Z80 I/O reports or clock
pauses**. It is separate from both the full-memory frequency screens and the
[one-hour arithmetic soak](20mhz-soak.md).

**Building this target does not deploy it. Do not flash, disconnect serial,
or attach/move probes during another running test.** Finish and retain that
test's results first.

## Purpose and limits

After slow loading and verification, the Pico generates continuous nominal
20 MHz PWM while the Z80 repeatedly checks arithmetic, flags, comparisons,
loops, stack behavior and SRAM data. No Z80 `IN`, `OUT`, block-I/O instruction,
or Pico I/O-trap handler participates in the running workload.

The Pico USB console still accepts commands. This is **no Z80 port I/O**, not
an absence of USB communication with the Pico. `s` and `h` do not pause PWM
or inspect live SRAM. There are no periodic result reads, bank reloads,
BUSREQ transactions or automatic one-hour stops.

| Property | Scope diagnostic |
| --- | --- |
| Target | `z80_stage06_scope_20mhz` |
| Supervisor clock | 120 MHz, selected before USB and Z80 execution |
| Z80 clock | Nominal 20 MHz, PWM divider 1, period count 6, three HIGH/three LOW |
| Runtime | Continuous until explicit stop, disconnect, or power loss |
| SRAM work area | `2000h-FFFFh`, 56 KiB |
| Live CPU result reports | None |
| Results | Reserved SRAM mailbox, retrieved only after stopping |
| Detected Z80 mismatch | Save first failure, enter HALT; PWM keeps running |
| Operator stop | `x` immediately stops PWM, then reads the mailbox with assisted clocks |
| Console disconnect | Fail closed; reconnect and explicitly request result retrieval |
| Automatic PASS | Never |

The configured clock is not a measured frequency. A continuously switching
CLK does **not** prove that arithmetic or memory checks are still advancing:
the CPU might have halted, stalled or branched incorrectly. This target
intentionally gives up the soak's live progress timeout and result supervision
to provide an uninterrupted measurement interval.

## Maintained sources

- [scope_test.c: Pico controller](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/scope_test.c)
- [scope_test.asm: no-I/O assembly selection](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/scope_test.asm)
- [math_workload.asm: shared checked workload](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_workload.asm)
- [scope_test_program.h: image/mailbox definitions and validation](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/scope_test_program.h)
- [math_soak_program.h: independent C reference answers](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_soak_program.h)
- [CMakeLists.txt: isolated firmware target](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/CMakeLists.txt)

The original soak selects the same assembly workload with port reports
enabled; the scope image selects SRAM reports instead. The original soak's
assembled bytes are preserved by this separation.

## What the Z80 tests

The [soak guide's arithmetic and instruction descriptions](20mhz-soak.md#arithmetic-and-comparison-workload)
also describe the shared scope workload:

1. Check 16 nested CALL levels, HL/DE/BC preservation, SP recovery and the
   lower stack guard.
2. Perform 16 passes through 128 fixed operand pairs:
    - 16-bit ADD, ADC with carry, and SBC with/without borrow.
    - Result and documented arithmetic-flag assertions.
    - Shift/add multiplication, checking the low 16 product bits.
    - Signed and unsigned less/equal/greater decisions.

3. Check every vector-table end pointer and the outer repetition count.
4. Fill the entire 56 KiB work area, then read and compare the entire area.
5. Commit a completed-phase count in SRAM and advance to the next pattern.

Patterns are low address byte, high address byte, complemented low byte and
complemented high byte. Four phases make a cycle. Each complete phase credits
57,344 SRAM-byte checks, 22,528 arithmetic assertions, five stack checks and
17 loop checks: **79,894 logical checks**.

Additional scope-reporting instructions include `LD HL,(nn)`,
`LD (nn),HL`, `LD (nn),IX`, and `LD (nn),DE`. They update memory only.
The normal/failure `OUT` blocks are excluded by assembly selection.

### Memory map

| Address | Purpose |
| --- | --- |
| `0000h-0EFFh` | Code and local loop state; assembler rejects overlap with mailbox |
| `0F00h-0F1Fh` | 32-byte scope result mailbox |
| `0F20h-0FFFh` | Unused resident reserve |
| `1000h-19FFh` | 128 independent expected-answer records |
| `1A00h-1FFFh` | Stack reserve; lower guard at `1A00h` |
| `2000h-FFFFh` | Destructive SRAM work area |

The Pico builds, loads and verifies the complete lower 8 KiB image once.
Only the Z80 addresses and accesses SRAM, including assisted loading and
post-stop reads. Neither the mailbox nor code/table/stack regions receive
the destructive work-area sweeps; this is not full-64-KiB pattern coverage.

## Build without deploying

From the repository root, with the
[firmware toolchain](../system/firmware-build.md) and `z80asm` available:

```sh
export PATH="$HOME/.local/share/arm-gnu-toolchain-15.3.rel1/bin:$PATH"
cmake -S . -B build/scope20 -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/scope20 --target z80_stage06_scope_20mhz
```

Output:

```text
build/scope20/src/stage06_sram_dma/z80_stage06_scope_20mhz.uf2
```

This separate build directory leaves the earlier soak/screen UF2s intact.
Normal firmware retains its existing clock limit. The scope target uses
the isolated 20 MHz clock/CPU chain and does not install an I/O trap.

The VS Code **Flash Pico - Phase 6 20 MHz scope diagnostic** task force-flashes
the UF2 without a Phase 9 stop/flush handshake. Use it only when interrupting
the current firmware is safe and the populated board has external +5 V applied
before USB.

## Later deployment and console operation

**The following is a later manual procedure, not part of building.**

1. Finish the current test and save its final results. Close its serial
   owner; never open competing USB serial readers.
2. For the current circuit, all eight active packages must be installed and
   powered. Apply external 5 V before USB; disconnect USB before removing
   external 5 V. Never program the populated board with only USB power.
3. Attach the scope probes with the board powered off, following the
   [20 MHz clock measurement procedure](../hardware/oscilloscope.md#20-mhz-no-io-clock-measurement).
4. Identify the intended Pico with `picotool info -a`. With exactly one
   intended Pico attached, flash and verify:

    ```sh
    picotool load -v -x \
      build/scope20/src/stage06_sram_dma/z80_stage06_scope_20mhz.uf2 -f
    ```

    If multiple devices are attached, select the current `--bus` and
    `--address` explicitly. Require flash verification success.

5. Open the Pico USB serial connection with DTR asserted. For example:

    ```sh
    picocom -b 115200 /dev/cu.usbmodem1101
    ```

    Use the actual port on your host. Check the no-I/O diagnostic banner
    and `clk_sys=120000000Hz`; boot/reconnect never starts the test.

6. Send `f` + Enter. Wait for `RUN: nominal=20000000Hz` before measuring.
   Loading, verification and startup reset clocks are not the continuous
   scope interval.

| Command | Behavior |
| --- | --- |
| `f` + Enter | Start a fresh load, verification and continuous run; stopped state only |
| `s` + Enter | Report controller state, not live CPU progress |
| `h` + Enter | Show help without disturbing the running clock |
| `x` immediately | During RUN, stop and retrieve results; during assisted operations, abort |
| `r` + Enter | Read the stopped mailbox after a previously started run, including after reconnect |

`x` discards the remainder of its input line. Press Enter before another
command. A serial disconnect stops safely without retrieving results or
restarting. After reconnect, use `r` + Enter; SRAM must have remained powered.
Results survive reset, not loss of SRAM power, and a fresh `f` replaces them.

## Scope captures

Use the [DHO814 20 MHz setup](../hardware/oscilloscope.md#20-mhz-no-io-clock-measurement)
to measure at **Z80 CLK pin 6**, not just Pico GP2.

Expected nominal values are **20 MHz, 50 ns period, about 25 ns HIGH and
25 ns LOW**. Capture only after RUN and before `x`/disconnect. There should
be no deliberate reporting-related clock gaps during that interval.

For SRAM work, use the existing
[read/write capture groups](../hardware/oscilloscope.md#four-channel-connections-and-expected-results).
The first part of each phase performs arithmetic and stack traffic, followed
by a full work-area fill and verification. Not every observed write is a
work-area write: stack and mailbox updates are also legitimate memory cycles.
Repeat captures with the logic analyzer as needed to identify addresses/data.

On a detected mismatch, HALT leaves the Pico PWM running; Z80 HALT can still
produce fetch/refresh activity. Do not interpret either as continuing useful
work. Stop and retrieve the mailbox after your captures.

## Interpreting stopped results

`x` stops PWM and asserts RESET before starting slow Z80-assisted reads.
These readback clocks are outside the measurement interval. Retrieval
finishes with RESET asserted, SRAM inhibited and Pico data paths isolated.
Do not assume a stopped PWM signal is physically LOW.

| Mailbox offset | Meaning |
| --- | --- |
| `00h-03h` | ASCII `SCP1`, format identifier |
| `04h` | `00h` prepared/not started, `01h` started, `E1h` committed mismatch |
| `05h` | Busy flag: `01h` while committing progress/failure, `00h` when stable |
| `06h-09h` | Completed phases, unsigned 32-bit little-endian |
| `0Ah` | Last completed pattern, 0-3; `FFh` before any completion |
| `0Bh` | Reserved |
| `0Ch`, `0Dh` | Failure stage and vector/context |
| `0Eh-0Fh` | Failure address, little-endian |
| `10h-11h` | Actual value, little-endian |
| `12h-13h` | Expected value, little-endian |
| `14h-1Fh` | Reserved |

The busy flag is written first and cleared last for both progress and
failure commits. A stop during an update yields **INCONCLUSIVE**, rather
than crediting a potentially torn count or failure record. An invalid magic,
state, last-pattern/count relationship or failure record is also inconclusive.
The 32-bit phase counter must not be allowed to wrap; counts after wrap
are not meaningful.

Example of eight completed phases:

```text
SNAPSHOT: phases=8 completed_cycles=2 sram_byte_checks=458752 math_vectors=16384 completed_phase_checks=639152
RESULT: no mismatch recorded in retrieved mailbox; not proof of live progress, not a timed PASS or timing qualification
```

Only completed phases are credited. A stable started mailbox with zero phases
is inconclusive. A positive count proves that those completions were recorded,
not that the CPU was still advancing at the moment you stopped it.

Failure stages and field meanings match the
[soak failure identifiers](20mhz-soak.md#failure-packet-and-check-stage-identifiers),
but this target stores the fields in SRAM instead of emitting a packet.
The Z80 commits failure data before setting the failure state and clearing
busy, then HALTs. HALT itself is not monitored by a Pico input.

Loader, clock, verification and readback faults print explicit FAIL messages.
An incomplete/invalid mailbox never becomes a PASS. There is no automated
duration criterion or live first-fault notification in this scope target.

## Host validation

With the optional pinned emulator environment configured as in the
[soak host-validation guide](20mhz-soak.md#host-validation):

```sh
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DSOAK_EMULATOR_PYTHON="$PWD/build/math-soak-emulator/bin/python"
cmake --build build/host-control --target scope_test math_soak_test math_soak_14h_test
ctest --test-dir build/host-control -R '^(scope_|math_soak_)' --output-on-failure
```

Controller tests cover DTR/manual gating, verification, quiet RUN behavior,
post-stop readback, disconnect/reconnect, invalid/torn mailboxes and failures.
The independent Z80 core executes the actual scope image through two cycles
and rejects **any IN or OUT**, including failure paths. It checks mailbox
commit ordering, a 16-bit counter carry into the high word, all arithmetic
failure stages, the stack guard and first/middle/last SRAM corruption.

The original soak controller and instruction tests remain regression checks.
Host validation and an ARM build are not physical scope measurements or a
bench pass for this newly built target.
