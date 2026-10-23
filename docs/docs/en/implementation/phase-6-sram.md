# Phase 6 - Z80-Assisted SRAM Loading

<div data-checklist="phase-6" data-checklist-label="Phase 6" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 6 prerequisite passed"> **Prerequisite:** [Phase 5](phase-5-data-bus.md#pass-gate).
**Install:** Z80 U1 and SRAM U2 together, after all socket wiring is checked.
The CPU is now required for RAM access; RESET is released during injection.

## Wiring - SRAM Socket

<input type="checkbox" data-checklist-id="sram-wiring" aria-label="Every SRAM address data and control pin wired and checked"> Complete every [address/data/fixed pin](../hardware/pin-mapping.md), including
CE# U2.22 from U4.12, WE# U2.29 from U4.9, OE# U2.24 from U3.3, CE2 to +5 V,
and A16 to GND.

<input type="checkbox" data-checklist-id="cpu-wiring" aria-label="Every Z80 control and monitor connection checked"> Complete Z80 CLK/RESET/BUSREQ/WAIT and all control monitor taps.
INT/NMI have pulls to +5 V; M1 goes to TP1; HALT/RFSH outputs remain open.
<input type="checkbox" data-checklist-id="cpu-sram-install" aria-label="CPU and SRAM orientation power and bypasses checked before installation"> Verify pin-1 orientation, power pins, and local bypasses before insertion.

## Firmware and Loader Qualification

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Phase 6 firmware built and loaded"> Build `z80_stage06_sram_loader`; UF2:
`build/src/stage06_sram_dma/z80_stage06_sram_loader.uf2`.
[Application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/main.c)
and [loader protocol](../hardware/address-interface.md#loader-sequence).

### Automatic Full 64 KiB Test

The `z80_stage06_step_loader` diagnostic also provides an automatic
whole-range test. It requires all eight active packages, checked wiring,
and loaded power rails. It uses the existing Z80-assisted loader: Pico
injects instructions and supplies clocks; **the Z80 alone addresses and
writes SRAM**. This is destructive and does not load or run CP/M.

```sh
cmake --build build --target z80_stage06_step_loader
```

Flash `build/src/stage06_sram_dma/z80_stage06_step_loader.uf2`, then open
the Pico USB serial console at 115200 baud. Apply external 5 V before USB;
disconnect USB before external 5 V. Never flash/run with USB alone.
Before the serial console connects, the application holds RESET# LOW,
CLK LOW, SRAM inhibited and both data paths disabled, without issuing
clocks or accessing memory. It waits for the USB CDC console connection
(DTR), not merely a plugged-in USB cable. Connecting alone only prints
the banner: it still waits for your explicit start command.
The startup banner is concise; **type `f`, then press Enter once** to run
the complete test automatically. No further stepping commands are needed.
It can also replace an in-progress manual session, starting a fresh loader.

The inclusive range is **0000-FFFF**, exactly **65,536 byte addresses**.
An SRAM byte cannot hold 256 or larger, so values wrap:

1. **Low-byte pass:** fill the entire range with `address & 0xFF`
   (`00,01,...,FF,00,01,...`), then read the entire range back.
2. **High-byte pass:** fill the entire range with `address >> 8`
   (256 zeros, 256 ones, and so on), then read the entire range back.

Both passes fill all memory **before** verification. The second pass is
important because the low-byte pattern alone cannot distinguish locations
256 bytes apart. Together they check both halves of the address, but they
are not a substitute for March tests, analogue margins or timing evidence.
After success, SRAM contains the high-byte pattern from the second pass.

Writing has one short phase banner, not a claimed per-byte success.
Verification prints exactly one result line per address, all numbers
hexadecimal, using the actual sampled SRAM value:

```text
address 0000, value 00, expected 00 - SUCCESS
address 0001, value 01, expected 01 - SUCCESS
address 0002, value 02, expected 02 - SUCCESS
```

Pass banners distinguish the two patterns. A successful run has 65,536
result lines per pass, 131,072 total, followed by a short success summary.
Every completed, failed or aborted run also prints decimal totals:

```text
STATS: successes=131072 failures=0
```

Successes count matching SRAM read-back checks across both passes, not
unverified writes: a complete error-free run has 131,072 successes over
65,536 distinct addresses. Failures count a data mismatch, a read/write
API error, or loader initialization failure. The test still stops at the
first failure, so the failure count is at most one. An operator abort or
console disconnection does not count as a failure; its totals are partial.
Counters reset on each new `f` run.

A mismatch prints the actual value and `FAIL`, then stops at the first
error. An API read/write error explicitly prints `value ERROR` rather than
inventing a read-back value. Loader initialization errors are reported
separately and cannot produce a success summary.

**Press `x` to abort**, without Enter. The automatic loop checks input and
USB connection between byte operations, not in the middle of a clock pulse.
It performs at most one byte write/read per iteration. On abort, failure,
completion or console disconnection, both data paths are isolated, reset
is asserted and the clock is stopped. While automatic mode is running,
other commands are rejected; they do not switch or restart the test.
After `x`, press Enter to clear the input line before `f` + Enter to restart.

Automatic mode uses the shared loader's **1 us half-period clocks**, not
the manual harness's 1 ms half-period clocks. It does not start PWM or
execute from SRAM. Actual elapsed time depends on firmware overhead and
USB output; these slow-cycle tests do not qualify normal operating speed.

[Automatic test implementation](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/full_memory_test.c).
Host tests verify exact 0000/FFFF bounds, both fill/read passes, all output
rows, wrap at 256, detection of 256-byte address aliases, first-error stops,
abort boundaries and console-disconnect isolation. Host tests are not
physical RAM-test evidence.

### PWM Speed Screening After the Slow Test

After the slow whole-memory test passes, the separate
`z80_stage06_pwm_memory` application can screen **actual Z80 execution from
SRAM** at increasing PWM clock rates. It does not require CP/M, provision
flash disks, or start networking. All eight active packages and the complete
WAIT/I/O wiring must be fitted. The existing slow diagnostic is unchanged.

```sh
cmake --build build --target z80_stage06_pwm_memory
```

Flash `build/src/stage06_sram_dma/z80_stage06_pwm_memory.uf2`.
Keep external 5 V applied before USB and power every fitted IC; USB-only
flashing/running is unsupported. Open USB serial at 115200 baud with DTR.
Startup holds reset and does not start clocks or memory accesses until
**`f` + Enter**. Use **`x` without Enter** to abort; `h` + Enter displays
help and `s` + Enter displays status. After aborting, press Enter before
starting another run. Disconnecting the console stops the test safely;
reconnecting does not resume it.

The test requests 1-8 MHz in 500 kHz steps, using the existing integer,
50%-duty PWM implementation. It prints requested and nominal actual
frequencies; these are not measured frequencies. The 6.5 MHz request
duplicates the actual 6 MHz waveform and is skipped after 6 MHz passes.
There are **14 distinct actual rates** at the configured 144 MHz system
clock. Actual rates above 6 MHz are explicitly marked experimental.
See the [frequency table and timing requirements](frequency-qualification.md).

At each speed:

1. Load and read back a small test program using slow assisted clocks.
2. Execute that program from the lower 32 KiB using PWM. Fill the entire
   upper 32 KiB, then verify it, for each of four patterns: address low
   byte, address high byte, complemented low byte, complemented high byte.
3. Reload the program into the upper 32 KiB, with a temporary `JP 8000`
   at reset address zero. Once execution reaches the upper half, repeat
   the same tests across **every lower-half address, including 0000**.
   The program needs no stack and disables maskable interrupts.
4. Repeat both halves until at least **60 seconds of execution-phase
   wall time** have accumulated and a complete two-half cycle has finished.
   Assisted loading/verification time is excluded. Execution time includes
   the brief stepped I/O reports, not exclusively uninterrupted PWM pulses.

One complete cycle verifies 65,536 addresses against four patterns:
**262,144 successful comparisons**. Program fetches, branches, reads and
writes run at the selected PWM speed. Only short progress/error `OUT 06`
reports use the existing WAIT trap and slow I/O completion.
The main loop must observe a correctly ordered completed-pattern marker
within five seconds; a hung CPU cannot become a timed PASS. The longest
normal fill/read block is under four million T-states (about four seconds
at 1 MHz), leaving a bounded progress margin.

Output is one RUN line, progress about every ten seconds, and one PASS
line per speed, rather than one USB line per byte. For example:

```text
RUN: requested=1000000Hz actual=1000000Hz minimum=60s
PASS: requested=1000000Hz actual=1000000Hz pwm_ms=... cycles=... successes=... failures=0
```

The test stops at the first mismatch, missing/incomplete progress,
unexpected I/O, loader error, or trap-counter change. A CPU-detected
mismatch includes address, actual byte and expected byte. Final statistics
count **completed 32 KiB verification blocks only**; successful comparisons
in an incomplete block are not counted. Abort/disconnection does not count
as a failure. Reset is asserted, PWM stopped, SRAM inhibited, and both
Pico data paths isolated on completion, failure or abort. A watchdog or
power reboot during a run invalidates that run; no new run starts
automatically and no unobserved interval can count toward a PASS.

This is a **functional screening result**, not frequency qualification.
It is not a March/coupling-fault test. The two-half approach also does not
establish whole-memory retention across simultaneous patterns in both
halves; retain the slow full-range test as complementary alias evidence.
Analogue setup/hold and write-pulse margins, clock quality, WAIT handover,
and repeated cold starts still need the
[scope and logic-analyzer qualification procedure](frequency-qualification.md).

[Screening implementation](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/pwm_memory_test.c).
Host tests independently execute the generated Z80 instructions through
both halves, check exact fill/read bounds and patterns, inject last-address
corruption and 256-byte aliases, and exercise duration, sweep, USB gating,
abort, loader failures, protocol errors and timeouts. These tests do not
prove physical timing.

#### Experimental 8-12 MHz Screening

Only after the 1-8 MHz screening passes, the explicitly separate
`z80_stage06_pwm_memory_experimental` target extends the same **self-validating
write/read/compare** test to requests from 8 through 12 MHz:

```sh
cmake --build build --target z80_stage06_pwm_memory_experimental
```

Flash
`build/src/stage06_sram_dma/z80_stage06_pwm_memory_experimental.uf2`.
Connect the serial console and send `f` + Enter. The same four patterns,
complete 64 KiB coverage, minimum 60-second execution phases, progress
timeouts, immediate `x` abort, and first-failure stop apply. The Z80
performs the comparisons while executing from SRAM at the selected rate;
these are not clock-output-only tests or slow-loader readback results.

The target compiles its clock, CPU, SRAM and I/O-trap chain separately with
`Z80_EXPERIMENTAL_12MHZ`. Normal phases and the original PWM diagnostic
retain their **8 MHz maximum**. The Pico system clock remains 144 MHz,
with no fractional PWM divider or PLL retuning.

| Requested MHz | Actual MHz (rounded) | PWM period count | Action after prior rate passes |
| ---: | ---: | ---: | --- |
| 8.0 | 8.000000 | 18 | Test |
| 8.5 | 8.000000 | 18 | Skip duplicate |
| 9.0 | 9.000000 | 16 | Test |
| 9.5 | 9.000000 | 16 | Skip duplicate |
| 10.0 | 9.000000 | 16 | Skip duplicate |
| 10.5 | 10.285714 | 14 | Test |
| 11.0 | 10.285714 | 14 | Skip duplicate |
| 11.5 | 10.285714 | 14 | Skip duplicate |
| 12.0 | 12.000000 | 12 | Test |

There are **four distinct actual rates**, not nine independent speed tests.
All are experimental, and the actual-rate gaps grow near the upper end.
A functional PASS does not establish safe analogue margins or extend the
normal system rating. Do not continue to a higher rate after any failure.

#### Exact 14 MHz Experimental Screening

At the usual 144 MHz Pico system clock, stable integer-divider, even-period
PWM jumps from 12 to 14.4 MHz. A request capped at 14 MHz still produces
12 MHz. To test **nominally exact 14 MHz without exceeding the request**,
use the separately compiled `z80_stage06_pwm_memory_14mhz` application:

```sh
cmake -S . -B build/pwm14 -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/pwm14 --target z80_stage06_pwm_memory_14mhz
```

UF2:
`build/pwm14/src/stage06_sram_dma/z80_stage06_pwm_memory_14mhz.uf2`.
Only this target opts into `Z80_EXPERIMENTAL_14MHZ`. After safe GPIO startup,
with Z80 RESET asserted and before USB initialization or any Z80 clocks,
it sets PLL_SYS to an 840 MHz VCO with post-dividers 3 and 2, giving
**140 MHz clk_sys**. PLL_USB remains independently at 48 MHz. The system
clock is not changed during an active test. Normal firmware and the
8-12 MHz experimental application retain their 144 MHz system clock and
respective 8/12 MHz Z80 limits. A separate build directory keeps the
14 MHz artifact distinct from the earlier firmware.

The banner reports the configured Pico system clock. A new run refuses
to start if it does not match the application's expected clock.
Serial DTR and `f` + Enter are still required; `x`, disconnect, timeouts
and detected errors stop safely as before.

| Requested MHz | Actual MHz (rounded) | PWM period count | Action |
| ---: | ---: | ---: | --- |
| 12.0 | 11.666667 | 12 | Test baseline at new system clock |
| 12.5 | 11.666667 | 12 | Skip duplicate |
| 13.0 | 11.666667 | 12 | Skip duplicate |
| 13.5 | 11.666667 | 12 | Skip duplicate |
| 14.0 | 14.000000 | 10 | Test |

Both distinct rates run the same **full 64 KiB, four-pattern
write/read/compare workload** for at least 60 seconds of execution-phase
time, finishing a complete two-half cycle. Stop at the first failure.
These rates are nominal configured values, not scope measurements; the
changed supervisor clock must be recorded with the result. A successful
run is functional screening evidence only, not a new normal system rating.

#### Exact 15 MHz Experimental Screening

After the 14 MHz screen passes, build the separate
`z80_stage06_pwm_memory_15mhz` target:

```sh
cmake -S . -B build/pwm15 -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/pwm15 --target z80_stage06_pwm_memory_15mhz
```

UF2:
`build/pwm15/src/stage06_sram_dma/z80_stage06_pwm_memory_15mhz.uf2`.
Only this target opts into `Z80_EXPERIMENTAL_15MHZ`. With the Z80 held in
reset, before USB initialization or Z80 execution, it selects a 1500 MHz
PLL_SYS VCO with post-dividers 5 and 2, giving **150 MHz clk_sys**.
The independent USB PLL remains 48 MHz. No system-clock change occurs
during testing; normal and earlier experimental targets keep their
existing clocks and limits.

| Requested MHz | Actual MHz | PWM period count | Action |
| ---: | ---: | ---: | --- |
| 14.0 | 12.5 | 12 | Test baseline at new system clock |
| 14.5 | 12.5 | 12 | Skip duplicate |
| 15.0 | 15.0 | 10 | Test |

Connect serial with DTR and send `f` + Enter. Both distinct rates run the
same four-pattern write/read/compare test across all 64 KiB for at least
60 seconds of execution-phase time, ending on a complete cycle. Memory
accesses have no added wait states; only I/O progress/error reports use
the existing WAIT trap and stepped completion. All failures, timeouts,
disconnects and the immediate `x` command stop safely. The displayed
clock is nominal, not scope-measured; this remains functional screening,
not timing qualification or a long-term reliability rating.

#### Exact 16 MHz Experimental Screening

After the 15 MHz screen passes, use the isolated
`z80_stage06_pwm_memory_16mhz` target:

```sh
cmake -S . -B build/pwm16 -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/pwm16 --target z80_stage06_pwm_memory_16mhz
```

UF2:
`build/pwm16/src/stage06_sram_dma/z80_stage06_pwm_memory_16mhz.uf2`.
Its `Z80_EXPERIMENTAL_16MHZ` opt-in selects **128 MHz clk_sys** from a
1536 MHz PLL_SYS VCO and post-dividers 6 and 2. This happens with Z80
RESET asserted, before USB initialization or Z80 clocks. USB retains its
independent 48 MHz PLL; no system-clock retuning occurs during the test.
All prior targets retain their clocks and limits.

| Requested MHz | Actual MHz | PWM period count | Action |
| ---: | ---: | ---: | --- |
| 15.0 | 12.8 | 10 | Test baseline at new system clock |
| 15.5 | 12.8 | 10 | Skip duplicate |
| 16.0 | 16.0 | 8 | Test |

Send `f` + Enter after connecting USB serial with DTR. Each distinct
rate must complete at least 60 seconds of the same four-pattern,
full-64-KiB write/read/compare workload and end on a complete two-half
cycle. There are no added SRAM wait states; only progress/error I/O
reports use the existing WAIT trap. First-error stops, timeouts,
disconnect protection and immediate `x` abort remain unchanged.
Record the 128 MHz supervisor clock with these nominal PWM rates.
A pass is functional screening evidence, not timing qualification.

#### Exact 17, 18, 19 and 20 MHz Screening

After 16 MHz passes, test the next rates **in order, stopping at the first
failure**. Each of these isolated targets tests one nominally exact rate;
there is no automatic advance inside the firmware:

```sh
cmake -S . -B build/pwm17-20 -G Ninja -DPICO_BOARD=pico2_w
cmake --build build/pwm17-20 --target \
  z80_stage06_pwm_memory_17mhz z80_stage06_pwm_memory_18mhz \
  z80_stage06_pwm_memory_19mhz z80_stage06_pwm_memory_20mhz
```

UF2s are under `build/pwm17-20/src/stage06_sram_dma/`, named after their
targets. Each target uses its own `Z80_EXPERIMENTAL_17MHZ`,
`Z80_EXPERIMENTAL_18MHZ`, `Z80_EXPERIMENTAL_19MHZ` or
`Z80_EXPERIMENTAL_20MHZ` clock-limit opt-in. Normal firmware and previous
experimental targets remain unchanged.

| Z80 MHz | Pico clk_sys MHz | PLL_SYS VCO MHz | Post-dividers | PWM period count |
| ---: | ---: | ---: | --- | ---: |
| 17 | 136 | 816 | 3, 2 | 8 |
| 18 | 144 | 1440 | 5, 2 (SDK startup unchanged) | 8 |
| 19 | 114 | 1140 | 5, 2 | 6 |
| 20 | 120 | 1200 | 5, 2 | 6 |

All use integer PWM division and 50% duty. The supervisor clock is
selected before USB initialization or Z80 execution, with RESET asserted;
USB retains its independent 48 MHz PLL. No supervisor clock change occurs
during an active test. These are nominal configured frequencies, not
scope-measured values, and the changed supervisor clock must be recorded.

For each rate, keep external 5 V on with every IC powered, flash/verify
the matching image, connect USB serial with DTR, check the clock banner,
and send `f` + Enter. Require a complete PASS and zero failures before
flashing the next rate. Each point must complete at least 60 seconds of
the same full-64-KiB, four-pattern write/read/compare workload, finishing
on a complete two-half cycle. SRAM accesses have no added wait states;
only short I/O reports use the existing WAIT trap.

A mismatch, timeout, unexpected control, loader error, aborted run,
disconnect or watchdog reboot invalidates the point: **do not proceed
to the next rate**. Immediate `x` abort and fail-closed reset/isolation
remain available. The CPU's 20 MHz grade is not a complete-system rating;
even four passing points are functional screening evidence, not analogue
timing qualification or a long-term reliability guarantee.

#### One-Hour 20 MHz Arithmetic and SRAM Soak

For an overnight run, the separate `z80_stage06_math_soak_14h_20mhz` target
uses the identical workload for at least **14 hours (50,400 seconds)**,
stopping at the first failure and finishing only on a complete cycle.
The original one-hour target remains available. See the
[14-hour build and operating notes](20mhz-soak.md#fourteen-hour-variant).

See the [complete soak-test guide](20mhz-soak.md) for the instruction inventory,
arithmetic/flag assertions, result-port protocol, statistics and run procedure.
That page includes the maintained diagnostic README directly.

`z80_stage06_math_soak_20mhz` is a separate destructive diagnostic, not a
replacement for the full-64-KiB frequency screens. It loads one resident
Z80 assembly program and independently calculated reference vectors, then
executes continuously without reloading/resetting between cycles.

```sh
cmake --build build/pwm17-20 --target z80_stage06_math_soak_20mhz
```

UF2: `build/pwm17-20/src/stage06_sram_dma/z80_stage06_math_soak_20mhz.uf2`.
The existing build directory must first be configured as above. `z80asm`
is required; the build assembles `math_soak.asm` and embeds its exact bytes.
The isolated 20 MHz clock/CPU/trap chain selects **120 MHz Pico clk_sys**
before USB initialization with Z80 reset asserted, using the same PLL and
50%-duty PWM configuration as the 20 MHz frequency screen. Normal targets
remain unchanged.

The resident memory layout is:

| SRAM addresses | Purpose |
| --- | --- |
| `0000-0FFF` | Z80 instructions and two loop-state bytes |
| `1000-19FF` | 128 reference records, generated by the Pico using wider C arithmetic |
| `1A00-1FFF` | Guarded stack; SP starts at `2000` and grows downward |
| `2000-FFFF` | 56 KiB write/read/compare work area |

Each phase performs:

1. Nested CALL/RET with 16 recursive levels, PUSH/POP register preservation,
   exact stack-pointer recovery and a lower stack guard.
2. Sixteen repetitions of 128 operand pairs: 64 boundary-value combinations
   and 64 deterministic pseudo-random pairs. Each pair checks ADD HL,
   ADC HL with carry, subtraction via SBC HL with carry clear, SBC HL with
   borrow, low-16-bit shift/add multiplication, and signed/unsigned ordering.
   Arithmetic results and documented carry, half-carry, sign, zero, overflow
   and subtract flags are checked where applicable; undocumented flag bits
   are masked out. Signed comparison accounts for sign XOR overflow.
3. A complete fill of `2000-FFFF`, followed by complete read/compare.
   Successive phases use low address, high address, complemented low
   address and complemented high address bytes. Four phases make a cycle.

The code, reference and stack regions are **not** swept as general data.
Instruction fetches, table reads and stack accesses exercise them, but this
does not claim full-64-KiB destructive coverage; retain the preceding screen.
No CP/M or BDOS is installed or required.

Keep external 5 V applied with every fitted IC powered before flashing or
connecting USB. Connect serial with DTR, confirm the 120 MHz supervisor
banner, then send **`f` + Enter**. A connection alone never starts the test.
`h` gives help, `s` gives status/statistics, and immediate **`x`** aborts.
Completion requires at least **3600 seconds of execution-phase elapsed
time**, excluding initial image loading/verification but including brief I/O
report pauses, and a completed four-phase cycle. The program is not reset
between phases. There are no added memory wait states.

##### How the Z80 Reports Results to the Pico

**The Z80 performs the calculations and comparisons; the Pico supervises
the run and receives its reports.** Results travel through Z80 I/O port
`06h`, not through a UART, CP/M/BDOS call or an SRAM mailbox. This is an
I/O-space address, not SRAM address `0006h`. No `IN` instruction is needed;
an unexpected I/O read is treated as a protocol fault.

After all arithmetic, stack, loop and SRAM checks in a phase succeed, the
Z80 executes the equivalent of:

```asm
ld a,040h       ; Phase 0 completed successfully
out (06h),a    ; Send the accumulator byte to the Pico
```

The actual program forms the marker from the current phase number.
Successive phases send `40h`, `41h`, `42h`, `43h`, then repeat in that
order. These bytes identify a **complete successful phase**, not a single
arithmetic result or memory comparison.

Each `OUT (06h),A` is captured as follows:

1. The Z80 drives its port address and the accumulator value onto the
   address and D0-D7 data buses, asserting **IORQ#** (I/O request) and
   **WR#** (write). The `#` suffix means active LOW.
2. With Pico **IO_RELEASE** LOW, the HCT32 equation
   `WAIT# = IORQ# OR IO_RELEASE` asserts **WAIT#**, holding the I/O
   transaction while the Pico responds.
3. The buffered IORQ# falling edge interrupts the Pico. The handler stops
   PWM, allows settling, samples the port through the LVC244 monitor and
   confirms that this is a write. Only A0/A1/A2/A4 are decoded
   (`address & 0x17`), so the port is seen as `06h`; unused address bits,
   including the high byte presented by `OUT (n),A`, are ignored.
4. The Pico keeps its upward data driver disabled, enables the downward
   **SN74LVC245AN** path and samples the eight data bits at 3.3 V.
   No raw 5 V Z80 data signal is connected directly to a Pico input.
5. The soak's receive callback checks the decoded port and byte. An
   in-order success marker increments the completed-phase counter; an
   unexpected marker or port flags a protocol error.
6. The trap raises IO_RELEASE and uses controlled single clock cycles
   until IORQ# and WR# release. It then isolates both data paths, lowers
   IO_RELEASE and resumes PWM. The next Z80 instruction follows normally;
   this reporting transaction does not reset or reload the program.

Only the short I/O reports use this hardware WAIT/stepped-completion
mechanism. SRAM instructions execute at the configured PWM rate without
added memory wait states.

On a failed check, the Z80 instead sends **nine bytes**, each using a
separate `OUT (06h),A`, then executes HALT:

| Byte position | Contents |
| --- | --- |
| 0 | `E1h`: failure-packet marker |
| 1 | Check stage |
| 2 | Operand-vector index or diagnostic context |
| 3, 4 | Relevant address, low byte then high byte |
| 5, 6 | Actual 16-bit value, low byte then high byte |
| 7, 8 | Expected 16-bit value, low byte then high byte |

The Pico assembles the packet, prints the mismatch details and fails
closed with RESET asserted and PWM stopped. An incomplete packet also
fails on timeout. Stages `01-0B` identify arithmetic result/flag,
multiplication and comparison checks; `0C-10` identify stack checks;
`11-12` check vector/repetition loop bounds; `80-83` identify RAM patterns.
For arithmetic failures the address identifies the reference record;
for RAM failures it identifies the work address. Structural/stack vector
indices are diagnostic context, not necessarily a valid operand index.

Maintained sources:
[Z80 report instructions](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_soak.asm),
[Pico report parser and statistics](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/math_soak_test.c),
and [I/O trap and stepped completion](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c).

The Pico reports progress every ten seconds and fails closed on the first
CPU mismatch, malformed/out-of-order report, loader/control error or
five-second progress timeout. Disconnect, reboot or abort invalidates the
run, not a PASS. A completed PASS holds RESET LOW, stops PWM and isolates
both Pico data paths. There is no automatic restart.

The Pico generates the USB serial `PROGRESS` and `STATS` text from the
received phase markers and its own elapsed-time measurement. The host
logger adds the UTC timestamp and `SERIAL` prefix shown in the live
terminal. Millions of successful checks therefore do **not** require
millions of `OUT` transactions: the Pico credits the known workload only
after receiving each complete-phase success marker. It does not reread
every work byte or independently recompute each arithmetic result during
the soak.

`STATS` reports successful checks and failures, with separate SRAM-byte,
arithmetic, stack and loop totals. Each completed phase credits:

- 57,344 SRAM-byte comparisons;
- 22,528 arithmetic assertions (2048 complete vectors, 11 assertions each);
- five stack assertions and 17 loop-bound assertions;
- **79,894 total successful assertions**.

These are different kinds of assertions, not an instruction count. Partial
phases are deliberately not credited. A first failure counts as one;
abort/disconnect is not a failure count but is still an incomplete run.
Progress statistics alone are not a final PASS. A one-hour pass remains
functional soak evidence, not measured analogue timing qualification.

##### Host Validation

Host controller tests simulate the exact hour/cycle boundary, counters,
serial gating and fault stops. Optional independent instruction execution
requires Python 3.10+ and the pinned host-only emulator:

```sh
python3.14 -m venv build/math-soak-emulator
build/math-soak-emulator/bin/python -m pip install \
  -r src/common/tests/requirements-soak.txt
cmake -S src/common/tests -B build/host-control -G Ninja \
  -DSOAK_EMULATOR_PYTHON="$PWD/build/math-soak-emulator/bin/python"
cmake --build build/host-control --target math_soak_test math_soak_14h_test
ctest --test-dir build/host-control -R '^math_soak_' --output-on-failure
```

Substitute an available Python 3.10+ executable for `python3.14`. The
independent core runs the actual image through two cycles and checks every
work byte/reference-table boundary, plus injected arithmetic, flag,
comparison, stack-guard and first/middle/last SRAM failures. This is host
execution evidence, not an hour-long physical run.

#### Continuous 20 MHz Scope Diagnostic (No Z80 I/O)

For uninterrupted scope captures, use the separate
[`z80_stage06_scope_20mhz` guide](20mhz-scope.md). It shares the arithmetic,
stack and 56 KiB SRAM workload, but replaces all Z80 port reports with a
reserved SRAM mailbox. No I/O trap, periodic result reads or bank reloads
pause PWM during RUN. A detected mismatch commits SRAM details and HALTs
the CPU while PWM continues.

`x` stops execution and retrieves the mailbox with slow assisted reads.
There is no live progress supervision or automatic timed PASS. A clock
that keeps switching does not prove the CPU remains healthy.
See the [20 MHz DHO814 setup](../hardware/oscilloscope.md#20-mhz-no-io-clock-measurement)
for probe safety, measurement settings and the expected 50 ns period.
Build only until another running test has completed; deployment is a separate
explicit operation.

### Console-Stepped Miniature Loader

For controlled bring-up with **all eight active packages installed**, build
the separate `z80_stage06_step_loader` target:

```sh
cmake --build build --target z80_stage06_step_loader
```

UF2: `build/src/stage06_sram_dma/z80_stage06_step_loader.uf2`.
[Maintained C harness](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/step_loader.c).
This exercises real hardware using the same injected instructions and bus
helpers as the loader, rather than simulating SRAM in Pico RAM. It mimics the
mechanism used to load CP/M but **does not load or execute a CP/M image**.
The existing automatic `p`/`c`/`m` application remains unchanged.

This test destructively writes **00 at 0000, 01 at 0001, and 02 at 0002**
(all hexadecimal). There are no scratch writes. It is not a full-memory
or complete address-bus test.
The C transfer table describes each instruction and its expected effect;
the serial console separates **EXPECTED** behavior from **OBSERVED**
monitor inputs and actual data-bus samples.

Before flashing, check every buffer's supply, direction and enables, and
the loaded rails. Apply external 5 V before USB; remove USB before external
5 V. Do not use USB alone. Use the
[firmware flashing procedure](../system/firmware-build.md) and open the Pico
USB serial console at 115200 baud. The application waits for the console,
starts in reset and does not clock the CPU until instructed. Disconnecting
the console isolates both data paths, asserts reset and stops the session;
reconnection requires an explicit restart.

#### Commands and first steps

Type **one lower-case letter then Enter**. CR, LF and CRLF work. Empty
Enter does nothing; malformed/multiple-letter lines do not advance.
Do not paste command sequences or hold down a key.

| Command | Action |
| --- | --- |
| `h` + Enter | Print help, physical probe pin map and current expected behavior; no clocks or ownership changes |
| `f` + Enter | Start the automatic full 64 KiB low/high-byte test described above |
| `b` + Enter | Begin/restart with RESET# LOW, CLK LOW, SRAM inhibited and data paths isolated; no clocks |
| `n` + Enter | Perform exactly one next action: either one complete clock cycle **or** a labelled no-clock ownership/reset action |
| `r` + Enter | Repeat current instructions and observations without changing signals or clocking |
| `x` | Abort immediately without Enter: isolate data paths, assert reset and inhibit SRAM output |

After `x`, press Enter to clear the discarded line before entering `b`.
On any reported mismatch the session stops in reset; `n` cannot continue
from the fault. Record the first error before changing anything.

1. Read `h`, then type `b` + Enter.
2. Issue `n` + Enter six times, one reset clock at a time.
3. The next `n` checks idle controls and releases reset with CLK stopped LOW;
   it does not generate a clock.
4. Issue two further `n` commands for the existing loader's reset-exit
   alignment clocks. Physical alignment must still be qualified.
5. Follow **NEXT n** for every subsequent action. Setup and injection actions
   do not clock; T1/T2/T3/T4 actions each clock once and finish LOW.

Every pulse has 1 ms LOW/HIGH half-periods and the shared clock helper's LOW
settling delay. The application never starts PWM. A multimeter sees the
held LOW clock between commands; it cannot prove pulse amplitude, edge
quality, exact timing, or absence of short glitches.

#### Expected instruction and bus sequence

For each target address `nn` and test byte `n`, the Z80 executes:

```text
LD HL,nn       ; 21 low high: set target, using injected opcode/operands
LD (HL),n      ; 36 byte: Z80 drives address/data and writes SRAM
LD A,00        ; 3E 00: clear accumulator without changing SRAM
LD A,(HL)      ; 7E: SRAM supplies the byte; Pico samples through U10
```

`LD A,00` clears the accumulator, **not a persistently driven physical
data bus**. The zero operand is injected during its own fetch; afterward
the upward path is isolated. No extra bus-clear operation or scratch write
is performed. The Pico cannot directly observe A; it observes the SRAM
data bus during the subsequent read, and the Z80 is expected to latch that
byte at read completion.

| Held point | Expected address/data ownership and controls |
| --- | --- |
| Injected opcode/operand after T2 | Address is the injected instruction PC, not necessarily HL; U9 supplies the printed byte, U10 off, SRAM OE# HIGH |
| Z80 write after T2 | Address is HL; Z80 supplies the test byte; CE# LOW, WE# LOW, OE# HIGH; U9 off and U10 samples the write data |
| SRAM read after T2 | Address is HL; SRAM supplies the stored byte; CE# LOW, WE# HIGH, OE# LOW; U9 off and U10 samples the read data |
| Opcode T4 | Refresh can put a different address on the bus; do not mistake it for a target-address failure |

At the held T2 points, use `r` to reread instructions without changing bus
ownership. The console prints full expected address and byte in hexadecimal
and as `A15..A0` / `D7..D0` bits, and `h` gives exact SRAM pin numbers.
For example, the write/read of address 0002 expects address bits
`0000000000000010` and data bits `00000010`.

Use DC volts with the black probe on common circuit GND and insulated tips
on the listed pins; never bridge neighboring contacts. Power off before
moving clips. HIGH/LOW listings are logic expectations, not measured
voltages: compare physical readings against the exact receiver's input
limits. Raw Z80 outputs are TTL; a digital HIGH indication does not establish
SRAM CMOS margin. Use the scope/analyzer for timing qualification.

The console compares the U10 data sample with the expected byte during both
write T2 and read T2. A write-data match is **not yet proof of a committed
write**; the following T3 finishes it. A read match is counted only after
its T3 completes. The next action never hides another clock.

After all three write/immediate-read pairs, a final console-stepped
**read-only retention pass** reloads each HL address, clears A again and
reads the stored byte without rewriting it. This can expose overwrites
caused by aliases among these three addresses. A complete sequence has
182 full clock cycles, including six reset and two reset-exit clocks,
and six verified read-backs. Completion asserts reset, isolates both
data paths and leaves SRAM contents intact.

#### What the harness can observe

The Pico checks BUSACK#, IORQ#, RD# and WR# via U7, and only the address
bits A0/A1/A2/A4 (mask 17). It checks those address bits at stable fetch,
write and read points, not during refresh. It cannot read all sixteen
address bits, MREQ#, actual SRAM CE#/OE#/WE#, or actual U9 OE#.
Reported enable/inhibit values are Pico-side commands, not measurements
of the downstream hardware.

Host tests decode the injected instructions into a simulated Z80/SRAM and
check manual pulse counts, the exact three writes, six accumulator clears,
immediate/final read-back, ownership isolation, fault stops and abort at
every action. Those tests establish software sequencing, **not physical
reset-exit alignment, analogue levels, timing or full SRAM qualification**.

### Full-Memory Loader Qualification

First capture slow reset exit and injected `21/36/7E` sequences with the
analyzer, alongside scope measurements of CLK and SRAM controls. Confirm:

- <input type="checkbox" data-checklist-id="reset-exit-capture" aria-label="Reset and first opcode alignment captured"> Six reset clocks and two reset-exit clocks align the first opcode fetch.
- <input type="checkbox" data-checklist-id="opcode-operand-capture" aria-label="Opcode and operand T-state counts captured"> Opcode fetches have four T-states; operand reads have three.
- <input type="checkbox" data-checklist-id="slow-clock-data-hold" aria-label="Slow clock settling and injected data hold measured"> Each slow clock leaves CLK LOW for at least 1 us before returning. Confirm
  injected data remains valid through RD# release before firmware isolates it.
- <input type="checkbox" data-checklist-id="boot-inhibit-capture" aria-label="SRAM output inhibit captured throughout injection"> SRAM OE stays HIGH throughout injected bytes.
- <input type="checkbox" data-checklist-id="write-isolation-capture" aria-label="Upward drive disabled before CPU writes"> Upward data is disabled before the CPU's memory-write cycle.
- <input type="checkbox" data-checklist-id="readback-isolation-capture" aria-label="Exclusive SRAM readback drive captured"> Readback enables SRAM only after the Pico stops driving.
- <input type="checkbox" data-checklist-id="translator-settling-capture" aria-label="Translator high impedance and settling measured"> After disabling both translators, allow at least 1 us before changing Pico
  data pins to outputs; verify U10 is actually high-impedance at that point.
- <input type="checkbox" data-checklist-id="sram-timing-capture" aria-label="SRAM write and read timings meet exact datasheets"> SRAM write pulse, address/data setup/hold, and read sample position meet
  the exact Z80 and SRAM datasheets. Host tests are not this evidence.

`p` runs the full 64 KiB address-derived pattern and readback; `c` the
complement; `m` a full March test. Each command initializes a fresh injected
session and finishes with RESET asserted and the clock stopped. Save the
first mismatch address/expected/actual; do not advance after an error.
Start with the scope-connected current-limited supply and check loaded rail
voltage and clock thresholds before long tests.

| Done | Command | Required result |
| --- | --- | --- |
| <input type="checkbox" data-checklist-id="loaded-power-clock" aria-label="Loaded rails and clock thresholds measured before long RAM tests"> | Before long tests | Loaded rail voltage and clock thresholds checked |
| <input type="checkbox" data-checklist-id="ram-pattern" aria-label="Full 64 KiB address-derived pattern passed repeatedly"> | `p` | Full 64 KiB address-derived pattern and readback pass repeatedly |
| <input type="checkbox" data-checklist-id="ram-complement" aria-label="Full 64 KiB complement pattern passed repeatedly"> | `c` | Complement pattern and readback pass repeatedly |
| <input type="checkbox" data-checklist-id="ram-march" aria-label="Full 64 KiB March test passed repeatedly"> | `m` | Full March test passes repeatedly |

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-loader" aria-label="Phase 6 loader phase verified on real hardware"> Loader phase verified on real hardware.
- <input type="checkbox" data-checklist-id="gate-ram" aria-label="Phase 6 all three RAM tests and boundary addresses passed"> All three RAM tests pass repeatedly, including addresses 0000/FFFF.
- <input type="checkbox" data-checklist-id="gate-exclusive-drive" aria-label="Phase 6 no overlapping data drive measured"> No overlapping data drive occurs.
- <input type="checkbox" data-checklist-id="gate-cpu-bus-grants" aria-label="Phase 6 reset fetch and physical bus-grant checks passed"> Complete the [CPU reset, fetch and bus-grant checks](phase-6-bus-grants.md#pass-gate), including receiving-pin captures and safe grant/release fault behavior.
- <input type="checkbox" data-checklist-id="gate-evidence" aria-label="Phase 6 captures and cold-start results saved"> Save captures and cold-start results.

No normal-run MHz claim follows from slow injected tests alone.

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Phase 6 application - src/stage06_sram_dma/main.c"

    ```c
    {% include "../../../../src/stage06_sram_dma/main.c" %}
    ```

??? example "Shared assisted loader and RAM tests - src/common/sram.c"

    ```c
    {% include "../../../../src/common/sram.c" %}
    ```

??? example "Console-stepped harness - src/stage06_sram_dma/step_loader.c"

    ```c
    {% include "../../../../src/stage06_sram_dma/step_loader.c" %}
    ```

??? example "Automatic full-memory test - src/stage06_sram_dma/full_memory_test.c"

    ```c
    {% include "../../../../src/stage06_sram_dma/full_memory_test.c" %}
    ```
