# One-Hour 20 MHz Z80 Arithmetic and SRAM Soak Test

This diagnostic makes the **physical Z80 execute and check its own workload
from SRAM**, while the Pico supplies a nominal 20 MHz clock, receives result
reports and supervises a one-hour run. It is not a Pico simulation of the Z80,
an empty clock loop, or a test of only one SRAM address.

The workload combines 16-bit arithmetic and flags, signed and unsigned
comparisons, software multiplication, nested loops, subroutine calls, stack
traffic and repeated write/read/compare sweeps of a 56 KiB work area.

There are two separately built durations. The original one-hour target remains
unchanged in behavior; `z80_stage06_math_soak_14h_20mhz` runs the same resident
workload for **at least 14 hours**, then finishes on a complete four-pattern
cycle. Unless specified otherwise, the examples below describe the one-hour
target.

| Property | Configuration |
| --- | --- |
| Firmware target | `z80_stage06_math_soak_20mhz` |
| Z80 program | Resident assembly starting at `0000h` |
| Z80 clock | Nominal 20,000,000 Hz, 50% duty |
| Pico system clock | 120 MHz |
| Minimum run | 3600 seconds of execution-phase elapsed time |
| Completion boundary | A complete four-pattern cycle |
| Arithmetic corpus | 128 operand pairs, repeated 16 times per phase |
| SRAM work area | `2000h-FFFFh`, 57,344 bytes |
| Result channel | Z80 `OUT (06h),A` |
| Stop policy | First detected mismatch, control fault or progress timeout |
| Operator start | USB serial with DTR, then `f` + Enter |

**A passing soak is functional evidence under the tested conditions, not an
analogue timing qualification or a guaranteed system rating.** The earlier
full-64-KiB frequency screen is a separate test and remains useful.

## Sources and related documentation

The maintained implementation, rather than the explanatory examples here, is
authoritative:

- [Z80 assembly entry: math_soak.asm](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_soak.asm)
- [Shared checked workload: math_workload.asm](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_workload.asm)
- [Pico controller: math_soak_test.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_soak_test.c)
- [Reference answers and memory layout: math_soak_program.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_soak_program.h)
- [Assembly and embedding rules: CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/CMakeLists.txt)
- [Hardware I/O trap: io_trap.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c)
- [Buffered data-bus access: bus.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/bus.c)

See also the [Phase 6 SRAM bring-up specification](../../docs/docs/en/implementation/phase-6-sram.md)
and [frequency-qualification plan](../../docs/docs/en/implementation/frequency-qualification.md).
The full text of this README is also included in the MkDocs soak-test page.

For uninterrupted clock/SRAM captures, use the separate
[no-I/O 20 MHz scope diagnostic](../../docs/docs/en/implementation/20mhz-scope.md).
It selects the same checked assembly workload but stores results in SRAM
instead of using OUT; retrieval occurs only after stopping, with no live or
timed PASS. It does not replace or alter this one-hour soak.

## Hardware, power and clock prerequisites

This is for the current breadboard circuit with all eight active packages:

- Raspberry Pi Pico 2 W.
- Z84C0020PEC Z80.
- AS6C1008-55PCN SRAM; grounded A16 selects the accessible lower 64 KiB.
- SN74HCT32N fixed logic.
- SN74AHCT244N control/clock buffer.
- SN74AHCT245N Pico-to-Z80 data buffer.
- SN74LVC245AN Z80-to-Pico data buffer.
- SN74LVC244AN control/port monitor.

Complete the earlier wiring, isolation, assisted-loading and clock-speed checks
before using this soak. The historical PCB is not equivalent to this circuit.

**Every installed 5 V IC must be powered. Apply external 5 V before USB;
disconnect USB before removing external 5 V. Do not flash a populated board
on USB power alone.** Do not change wiring or insert/remove ICs while powered.

Only this isolated diagnostic chain opts into the 20 MHz limit. Before USB
initialization, with Z80 reset asserted, it configures PLL_SYS with a 1200 MHz
VCO and post-dividers 5 and 2, giving 120 MHz `clk_sys`. PWM uses integer
divider 1 and period count 6, with three counts HIGH and three LOW. USB keeps
its separate 48 MHz PLL. No supervisor-clock retuning occurs during execution.

The controller refuses to start if the system clock is not 120 MHz or the
selected PWM clock is not 20 MHz. Displayed frequencies are **nominal
configured values**, not oscilloscope measurements.

## Loading and memory layout

The Pico first builds an 8192-byte resident image containing assembled Z80
instructions, reference records and initialized stack space. The Z80 itself
writes and reads this image using the existing slow, instruction-injected
loader. Every image byte is verified before free-running execution starts.
The Pico never drives the SRAM address bus or masters SRAM writes.

The Z80 is then reset to start at `0000h`. Its program disables maskable
interrupts with `DI` and sets `SP` to `2000h`. There is no CP/M, BDOS, UART
driver or DCC-generated startup code involved in this diagnostic.

| Address range | Purpose during the soak |
| --- | --- |
| `0000h-0FFFh` | Reserved code region, including the `pattern` and `repeats` state bytes |
| `1000h-19FFh` | 128 reference records, 20 bytes each |
| `1A00h-1FFFh` | Stack reserve, initially filled with `A5h` |
| `2000h-FFFFh` | 56 KiB destructive SRAM work area |

The stack grows downward from `2000h`. The byte at `1A00h` is checked as a
lower guard. The code uses only part of its reserved region and only part of
the stack reserve.

**The work-area sweeps do not overwrite the code, reference table or stack.**
Those regions receive instruction fetches, table reads and stack accesses,
but are not exhaustively swept at 20 MHz. The physical SRAM's inaccessible
upper 64 KiB is not tested.

## Overall execution loop

The Z80 stays resident and repeats this sequence without reloading:

```text
repeat:
    for pattern = 0, 1, 2, 3:
        check nested calls, saved registers, stack pointer and stack guard
        repeat 16 times:
            check all 128 arithmetic/reference records
            check that the reference pointer reached the table end
        check that the outer repetition count was exactly 16
        fill every work-area byte with this pattern
        read every work-area byte and compare with the expected pattern
        report successful completion of this phase through OUT port 06h
```

One **phase** includes all of the above checks for one SRAM pattern. Four
phases form one **cycle**. The Pico, not the Z80 program, measures the hour
and stops execution at a completed-cycle boundary.

## Arithmetic and comparison workload

### Operand corpus and independent expected answers

The first 64 operand pairs are the Cartesian product of these eight values:

```text
0000h  0001h  0FFFh  7FFFh  8000h  8001h  5555h  FFFFh
```

These exercise zero, carries, the bit-11/bit-12 half-carry boundary, signed
limits and alternating data bits. The remaining 64 pairs come from a
deterministic 32-bit xorshift generator seeded with `5A801337h`.

The Pico calculates expected results with wider C arithmetic before loading
the image. The Z80 performs the actual calculations independently and compares
against the stored answers. It does not generate its expected answer by
repeating the same Z80 operation.

The corpus is fixed and repeatable: the test does **not** generate fresh random
operands on every cycle. Each phase executes 128 pairs 16 times, giving
**2048 complete vector checks per phase**.

### Eleven assertions for each operand pair

For each 16-bit pair `a`, `b`, the Z80 checks:

| Check | Z80 operation | Assertions |
| --- | --- | ---: |
| Addition | `ADD HL,DE` | Result and applicable flags |
| Addition with carry-in 1 | `SCF`; `ADC HL,DE` | Result and flags |
| Subtraction with borrow-in 0 | `OR A`; `SBC HL,DE` | Result and flags |
| Subtraction with borrow-in 1 | `SCF`; `SBC HL,DE` | Result and flags |
| Multiplication | 16-step shift/add subroutine | Low 16-bit result |
| Unsigned ordering | Subtract, then branch on zero/carry | Less/equal/greater code |
| Signed ordering | Subtract, then branch on zero/sign/overflow | Less/equal/greater code |

Four result checks plus four flag checks plus multiplication and two ordering
checks make **11 logical assertions**. A checked 16-bit value counts as one
assertion even though its low and high bytes are compared separately.

All arithmetic results wrap modulo 65536. The comparison codes are `FFh` for
less than, `00h` for equal and `01h` for greater than.

### Flag checks

For `ADD HL,DE`, mask `13h` checks half-carry, subtract and carry. It does not
check preservation of sign, zero or parity/overflow for this instruction.

For `ADC HL,DE` and `SBC HL,DE`, mask `D7h` checks:

- **S:** sign, from bit 15.
- **Z:** zero across the entire 16-bit result.
- **H:** carry/borrow across the bit-11/bit-12 boundary.
- **P/V:** signed overflow, not the parity of the result.
- **N:** whether the operation is subtraction.
- **C:** unsigned carry or borrow.

Undocumented flag bits 3 and 5 are deliberately excluded. The assembly saves
`AF` with `PUSH AF`, then uses `POP HL` to recover the saved flag byte in `L`
after checking the arithmetic result.

Examples of cases covered by the corpus:

```text
FFFFh + 0001h       -> 0000h, carry set
7FFFh + 0000h + 1   -> 8000h, signed overflow set
0000h - 0000h - 1   -> FFFFh, borrow set
8000h compared with 7FFFh:
    unsigned: greater
    signed:   less (-32768 versus +32767)
```

Signed ordering uses **sign XOR overflow**, not sign alone. The branch paths
explicitly handle overflow and equality, so a result with its sign bit set
does not automatically mean the original signed operand was smaller.

### Multiplication and loop invariants

There is no native Z80 multiply instruction in this workload. A subroutine
shifts the 16-bit multiplier right using `SRL B` and `RR C`. If the shifted-out
bit is one, it adds the multiplicand to the accumulator. It doubles the
multiplicand with `ADD HL,HL`, exchanging DE/HL as needed, and repeats 16 times.

Only the low 16 product bits are checked; this is not a full 32-bit product
or a multiply-overflow test.

After each 128-record pass, the Z80 checks that IX reached `1A00h`, immediately
after the reference table. After the 16 repetitions, it checks that IY equals
16. These add **17 loop-bound assertions per phase**.

## Subroutine and stack checks

Each phase loads `HL=1357h`, `DE=2468h`, `BC=5FAAh`, then makes **16 recursive
CALL levels**. Each level pushes BC, DE and HL and restores them before RET.
The recursion stops when the low nibble of B becomes zero.

The subsequent assertions check:

1. HL was restored to `1357h`.
2. DE was restored to `2468h`.
3. BC was restored to `5FAAh`.
4. SP equals `1FFCh` inside the stack-test subroutine: two return addresses
   are active at that point, one for the phase and one for the stack test.
5. The lower guard byte at `1A00h` remains `A5h`.

This exercises return-address storage, descending SRAM stack writes, ascending
reads, PUSH/POP order and register preservation. It does not scan every byte
of the stack reserve or exhaustively prove every possible stack fault.

In addition, `pattern_byte` is called for every work-byte write and every
work-byte verification. Stack traffic therefore continues throughout the
SRAM sweeps, not just during the explicit recursive check.

## SRAM write/read/compare patterns

Every phase fills all **57,344 bytes** from `2000h` through `FFFFh` before
starting verification. Verification recomputes the expected byte and reads
the entire area in ascending address order. Address-pointer wrap to zero
ends the sweep, including the last address `FFFFh`.

| Phase | Expected byte at address `ABCDh` | Success marker |
| --- | --- | --- |
| 0: low address byte | `CDh` | `40h` |
| 1: high address byte | `ABh` | `41h` |
| 2: complemented low byte | `32h` | `42h` |
| 3: complemented high byte | `54h` | `43h` |

The complementary patterns exercise both states of each data bit. Combining
low- and high-address patterns helps expose address aliasing. Filling the
whole area first avoids relying only on an immediate write/read to one
location. This is not a complete March algorithm, an exhaustive coupling-fault
test or a separate long-retention test.

## Z80 instructions exercised

The table describes the actual resident assembly, including supporting
instructions. **Being exercised is not the same as having every flag, operand
combination or possible failure mode exhaustively checked.**

| Instruction/forms | Role in this program |
| --- | --- |
| `LD r,r`, `LD r,n` | Operand movement, expected values, counters and report fields |
| `LD BC,nn`, `LD DE,nn`, `LD HL,nn`, `LD SP,nn` | Register-pair and stack initialization |
| `LD IX,nn`, `LD IY,nn` | Reference-table pointer and repetition counter |
| `LD r,(IX+d)` | Indexed reads of operands, reference results and flags; displacements 0-19 |
| `LD (HL),A`, `LD A,(HL)` | Work-area writes and verification reads |
| `LD (nn),A`, `LD A,(nn)` | SRAM-backed loop state and stack-guard access |
| `ADD HL,DE`, `ADC HL,DE`, `SBC HL,DE` | Checked 16-bit arithmetic |
| `ADD HL,HL`, `ADD HL,SP`, `ADD IX,DE` | Multiplicand shifting, SP observation and record-pointer advancement |
| `ADD A,n`, `SUB B` | Progress-marker formation and failure-vector index encoding |
| `INC HL`, `INC IY`, `INC A`, `INC C` | Address progression, repetitions, ordering codes and check stages |
| `DEC A`, `DEC B` | Multiplication/repetition/vector/recursion loops and ordering codes |
| `AND n`, `OR A`, `OR L`, `OR n`, `XOR A` | Flag masks, carry clearing, zero tests and marker/phase setup |
| `CP D`, `CP E` | Byte comparisons for work data and 16-bit expected values |
| `SCF`, `CPL` | Carry/borrow input setup and complemented SRAM patterns |
| `BIT 0,C`, `BIT 1,C` | Select low/high address bytes and complement behavior |
| `SRL B`, `RR C` | Shift the 16-bit multiplier, propagating bits through carry |
| `EX DE,HL` | Swap multiplicand and accumulator during multiplication |
| `PUSH AF/BC/DE/HL/IX/IY` | Save flags, registers, pointers and comparison values |
| `POP BC/DE/HL/IX` | Restore saved values or transfer them through the stack |
| `CALL`, `RET`, `RET Z` | Recursion, arithmetic/check helpers and pattern selection |
| `JR`, `JR Z/NZ/C/NC` | Relative loops and conditional decision paths |
| `JP`, `JP NZ/PE/M` | Cycle/vector loops, sign/overflow decisions and mismatch exits |
| `OUT (n),A` | Success markers and multi-byte failure packets |
| `DI` | Disable maskable interrupts at program startup |
| `HALT` | Failure path only; Pico reset may stop execution before it reaches HALT |

The workload includes ordinary, CB-prefixed, ED-prefixed and DD/FD-prefixed
instructions. For example, CB provides bit/shift operations, ED provides
16-bit ADC/SBC, and DD/FD provide IX/IY operations.

It does **not** exercise `IN`, `DJNZ`, `LDIR`/other block instructions,
`EXX`, alternate-register-bank checking, BCD/`DAA`, division, every
indexed addressing mode, or interrupt-service/acknowledge behavior. `DI` is
executed, but interrupt masking is not verified by injecting interrupts.
Failure-packet instructions and HALT are not part of the normal passing loop.

## How results reach the Pico through OUT port 06h

The Z80 performs the checks. The Pico receives compact completion reports;
it does not reread every SRAM byte or redo every calculation during the soak.

### Success reports and the hardware handshake

After a successful phase, the Z80 executes the equivalent of:

```asm
ld a,040h       ; Phase 0 complete
out (06h),a
```

This is an I/O transaction, not a write to SRAM address `0006h`.

1. The Z80 drives the port address and D0-D7 data, asserting **IORQ#**
   (I/O request) and **WR#** (write). `#` means active LOW.
2. With Pico IO_RELEASE LOW, HCT32 logic asserts **WAIT#** using
   `WAIT# = IORQ# OR IO_RELEASE`. The I/O transaction is held for the Pico.
3. Buffered IORQ# interrupts the Pico. Its handler stops PWM, allows settling,
   samples the port through the LVC244 monitor and validates the controls.
4. The Pico keeps the upward data driver disabled, enables the downward
   LVC245 path and reads the byte safely at 3.3 V logic levels.
5. The receive callback validates the port and marker order. Expected markers
   are `40h`, `41h`, `42h`, `43h`, repeating; each credits one complete phase.
6. The trap raises IO_RELEASE and completes the transaction using controlled
   single clock cycles until IORQ# and WR# release. It isolates both data
   paths, lowers IO_RELEASE and resumes PWM.

Only A0/A1/A2/A4 are decoded (`address & 17h`). `OUT (n),A` also presents A on
the high address byte, but this circuit ignores those bits. Thus sending
`40h` to port `06h` is still decoded as `06h`, not a distinct `4006h` service.

The brief I/O reporting pauses are included in execution-phase elapsed time.
**Memory accesses have no added wait states.** During memory cycles IORQ#
is HIGH, keeping the HCT32 WAIT# output HIGH.

No `IN` is needed. An unexpected I/O read or out-of-order/invalid report is a
protocol fault, not a valid success.

### Failure packet and check-stage identifiers

A detected mismatch sends nine bytes, each with a separate `OUT (06h),A`:

| Byte position | Meaning |
| --- | --- |
| 0 | `E1h`, start of failure packet |
| 1 | Check-stage identifier |
| 2 | Vector index or diagnostic context |
| 3, 4 | Relevant address, low byte then high byte |
| 5, 6 | Actual 16-bit value, low byte then high byte |
| 7, 8 | Expected 16-bit value, low byte then high byte |

| Stage, hexadecimal | Failed check |
| --- | --- |
| `01`, `02` | ADD result, flags |
| `03`, `04` | ADC with carry-in 1 result, flags |
| `05`, `06` | SBC with borrow-in 0 result, flags |
| `07`, `08` | SBC with borrow-in 1 result, flags |
| `09` | Low-16-bit multiplication result |
| `0A`, `0B` | Unsigned, signed ordering code |
| `0C`, `0D`, `0E` | Restored HL, DE, BC |
| `0F` | Stack-pointer recovery |
| `10` | Lower stack guard |
| `11` | Reference-table end pointer |
| `12` | Outer repetition count |
| `80`, `81`, `82`, `83` | SRAM pattern 0, 1, 2, 3 |

Arithmetic records use vector indices 0-127; their address identifies the
20-byte reference record, not a work-area byte. RAM errors use index `FFh`
and report the failing work address; their actual/expected byte occupies
the low byte of each value. Structural/stack indices are context only:
their address field reflects IX and does not necessarily identify a failing
stack location. For example, stack-guard stage `10` identifies the fixed
guard at `1A00h` even though the reported IX points at the reference table.

The assembly contains HALT after the packet. Once the Pico receives the
complete packet, it prints the details, asserts RESET and stops PWM; reset
may preempt HALT. A partial packet also fails on the progress timeout.

## Statistics and what the displayed numbers mean

The Pico generates `PROGRESS` and `STATS` over USB serial every ten seconds.
The host logger adds the UTC timestamp and `SERIAL` prefix. The Z80 does not
format or transmit those text lines, nor does it send one OUT per assertion.

For `p` completed phases:

| Field | Formula |
| --- | --- |
| `completed_math_vectors` | `p * 2048` |
| `sram_byte_checks` | `p * 57344` |
| `math_assertions` | `p * 2048 * 11 = p * 22528` |
| `stack_checks` | `p * 5` |
| `loop_checks` | `p * 17` |
| `successes` | Sum of the four check categories: `p * 79894` |
| `completed_cycles` | Integer division `p / 4` |
| `failures` | 0 until a detected failure; first-failure stop reports 1 |

Counters credit only **completed phases**. Successful operations in a
partially completed phase are not counted. Counts are logical checks, not
CPU instructions, clock edges or unique memory addresses.

For example, this is a progress sample, **not a final one-hour PASS**:

```text
PROGRESS: actual=20000000Hz pwm_ms=340010 phases=365 completed_math_vectors=747520
STATS: successes=29161310 failures=0 sram_byte_checks=20930560 math_assertions=8222720 stack_checks=1825 loop_checks=6205 completed_cycles=91
```

It represents 5 minutes 40.010 seconds, 365 phases, 91 full cycles plus one
phase, and 29,161,310 completed assertions with no detected failure.
Advancing 10 or 11 phases between ten-second reports is normal; reporting
and phase boundaries are independent.

`actual` is nominal selected PWM frequency. `pwm_ms` is elapsed time since
the execution phase began, excluding initial loading/verification but
including the brief trapped-I/O pauses. It is not a count of only the
intervals when PWM was enabled. A timestamp ending in `Z` is UTC.

## Completion, failures and incomplete runs

A valid completed run requires:

- At least `3600000` milliseconds of execution-phase elapsed time.
- A complete four-pattern cycle at the stop boundary.
- No pending failure packet, protocol error, control fault or progress timeout.
- The final `PASS` and zero-failure statistics.

The final line has this shape; elapsed milliseconds and totals vary by run:

```text
PASS: one-hour soak complete (functional, not timing qualification); actual=20000000Hz pwm_ms=<at-least-3600000>; RESET# LOW
STATS: successes=<total> failures=0 sram_byte_checks=<total> math_assertions=<total> stack_checks=<total> loop_checks=<total> completed_cycles=<total>
```

The Pico fails closed on loader/image/clock errors, CPU-reported mismatches,
malformed reports and I/O control faults. A five-second lack of phase
progress, or an incomplete failure packet that times out, also fails.
Some shared trap faults fail closed and reboot; any reboot invalidates the
current run rather than automatically resuming it.

`x` or a console disconnect produces an incomplete STOP, not a PASS.
An operator abort/disconnect does not increment the mismatch failure count;
therefore `failures=0` alone does not prove completion. Reconnecting never
automatically starts or resumes the soak.

After completion, failure or abort, the firmware asserts RESET, stops PWM
and isolates both Pico data paths. A stopped PWM output must not be assumed
to be physically LOW without measurement.

## Build, flash and run

All commands below run from the repository root. Do not flash or reopen
the serial port during an existing soak unless intentionally ending it.

### Fourteen-hour variant

Build in a separate directory to retain the previous one-hour firmware:

```sh
cmake -S . -B build/soak14h -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/soak14h --target z80_stage06_math_soak_14h_20mhz
```

UF2:

```text
build/soak14h/src/stage06_sram_dma/z80_stage06_math_soak_14h_20mhz.uf2
```

Use this UF2 instead of the one-hour image in the flash procedure below.
The banner must say `starts 14 hours`, and RUN must report `minimum=50400s`.
`f` starts a fresh 14-hour run; there is no live duration-change command.
The final PASS requires **at least 50,400,000 ms**, zero failures and a
complete four-pattern cycle. Success text begins `PASS: 14-hour soak complete`.

All arithmetic, SRAM coverage, port reporting, error stops and counters are
identical. Totals use 64-bit arithmetic; successful checks can exceed
4,294,967,295 during this longer run. Keep external power and USB stable
and prevent the host from sleeping for the entire run. On macOS, a logger
can use `caffeinate -i -w <logger-PID>` to prevent idle sleep for its lifetime;
keep a laptop's lid open and its power connected. Closing a read-only log
viewer does not stop the serial owner, but closing the serial connection
invalidates the run.

### Build the isolated target

Use the project's [firmware toolchain setup](../../docs/docs/en/system/firmware-build.md)
with CMake, Ninja, Pico SDK, Arm GNU tools and `z80asm` available:

```sh
cmake -S . -B build/pwm17-20 -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/pwm17-20 --target z80_stage06_math_soak_20mhz
```

Generated firmware:

```text
build/pwm17-20/src/stage06_sram_dma/z80_stage06_math_soak_20mhz.uf2
```

The build assembles the maintained Z80 source and embeds it in the Pico
application. A build-time size check prevents code overlapping the reference
table. The normal firmware's clock limit remains unchanged.

### Flash only with all ICs powered

Close any existing serial console/logger after its run has finished. With
external 5 V applied before USB, identify the intended Pico:

```sh
picotool info -a
```

Normal-mode identification can return a nonzero status while listing the
USB-connected Pico; that is not a flash-verification result. If exactly one
intended Pico is connected, flash, verify and boot:

```sh
picotool load -v -x \
  build/pwm17-20/src/stage06_sram_dma/z80_stage06_math_soak_20mhz.uf2 -f
```

If multiple devices are attached, select the intended device with picotool's
`--bus` and `--address` options from its current identification output; do
not assume USB addresses remain constant. Require verification success.
Booting the firmware does not start the destructive test.

### Start from a serial console

Connect USB serial with **DTR asserted**. A typical macOS port and command are:

```sh
picocom -b 115200 /dev/cu.usbmodem1101
```

Choose the actual device path on your machine. USB CDC does not use the
115200 setting as a physical Z80 timing reference.

Check the banner:

```text
20MHz math/SRAM soak ready; f + Enter starts one hour; x aborts immediately.
Pico clk_sys=120000000Hz; nominal PWM=20000000Hz, not measured.
```

| Command | Action |
| --- | --- |
| `f` + Enter | Load/verify the image, then start a fresh one-hour run |
| `h` + Enter | Show help |
| `s` + Enter | Show state and current statistics |
| `x` immediately | Abort without waiting for Enter |

An abort discards the rest of its input line; press Enter before a subsequent
command. Expect LOAD, then RUN, then progress/statistics roughly every ten
seconds. Keep external power and USB connected throughout.

### View live results without disturbing the serial owner

Only one process should own the serial connection. If a logger already owns
it, do **not** open picocom as a second reader: changing DTR or disconnecting
can invalidate the run.

Instead, open a foreground terminal that follows the logger's actual file:

```sh
tail -n 20 -F /path/to/captured-soak.log
```

Use the real log path printed by the active logger. Ctrl+C in this read-only
viewer stops the viewer only; it does not send `x` or stop the soak. Commands
must go through the process owning the serial connection. Keep the host awake
and retain the final PASS/FAIL and statistics, not just an early sample.

For reproducible evidence, record the firmware SHA-256, board/rail conditions,
configured clocks and complete timestamped serial output. Do not turn a
failed/interrupted attempt into a pass by silently restarting and discarding
its earlier evidence.

## Host validation

The [controller tests](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/tests/math_soak_test.c)
check DTR/manual-start gating, image loading, clock rejection, abort,
disconnect, failure reporting, protocol/control faults and progress timeouts.
Simulated time verifies the exact hour boundary, complete-cycle requirement
and statistics without waiting a physical hour.

The [independent instruction-execution test](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/tests/math_soak_z80_test.py)
uses the pinned host-only `z80` package. It runs the actual generated resident
image through two complete cycles: eight phases, 16,384 vector checks and
all four SRAM patterns twice. It independently checks the final work bytes
after each phase, reference-table preservation and the stack guard.

Fault injection corrupts each arithmetic-result/flag/comparison expectation,
the stack guard, and the first/middle/last work addresses (`2000h`, `8000h`,
`FFFFh`). These cases must produce the correct failure packet without a
phase-success marker.

To enable the independent core, use Python 3.10 or newer; substitute an
available version for `python3.14`:

```sh
python3.14 -m venv build/math-soak-emulator
build/math-soak-emulator/bin/python -m pip install \
  -r src/common/tests/requirements-soak.txt
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DSOAK_EMULATOR_PYTHON="$PWD/build/math-soak-emulator/bin/python"
cmake --build build/host-control --target math_soak_test math_soak_14h_test
ctest --test-dir build/host-control -R '^math_soak_' --output-on-failure
```

Without `SOAK_EMULATOR_PYTHON`, the C controller test can still run when
`z80asm` is present, but independent Z80 execution is explicitly disabled.
These checks do not access or interrupt a physical board.

Host success is not a physical one-hour result. Likewise, a physical soak
does not replace scope measurements, full instruction-set conformance,
interrupt/I/O-read testing, repeated cold starts or testing across voltage
and temperature conditions.
