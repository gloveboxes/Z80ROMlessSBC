# 8.12 Frequency Qualification

**Prerequisite:** The [Phase 10 pass gate](phase-10-websocket.md#pass-gate) must pass at
1 MHz before testing any higher rate.

**Qualification is measurement, not overclocking until CP/M looks stable.**
A clean terminal session can miss rare bus errors. Keep a passing 1 MHz
baseline and change only the clock setting between tests, not the wiring,
power arrangement, and probe layout at the same time.

**Setup time** is how long data must be valid before the CPU samples it;
**hold time** is how long it must remain valid afterward. Measure at the
receiving pin relative to the specified sampling edge. A waveform that
eventually reaches HIGH can still arrive too late. Use the scope for these
small timing margins, not just a logic-analyzer byte decode.

Begin only after CP/M has flushed all disks. Use USB `+`/`-` to select
each frequency under the firmware's BUSREQ#/BUSACK# and core-1
flash-quiescence interlocks.

- **Step sequence:** Test 2 MHz, then increase in 500 kHz steps to
  6 MHz. If and only if 6 MHz passes with margin, continue
  experimentally in 500 kHz steps to 8 MHz.
- **Functional checks at each step:** Run `a` for CPU SRAM
  readback/address activity and `h` for the one-hour self-checking
  memory loop plus continuous terminal IN/OUT while measuring stop
  latency.
- **Address-pattern captures at 1, 2, 3, 4, 5, and 6 MHz, plus every
  experimental step:** Apply address patterns 0x0000, 0xFFFF, 0x5555,
  0xAAAA, walking one, and walking zero while capturing A0, A7, A8, and
  A15 at the SRAM pins. Capture CLK, MREQ#, RD#/WR#, SRAM CE#/OE#/WE#,
  and D0-D7 as well; require valid read data before the Z80 setup
  deadline and every SRAM write pulse to meet the 45 ns minimum after
  propagation through the GAL and AHCT244.
- **WAIT# handshake, every I/O cycle:** Require WAIT# LOW before the
  Z80 sampling edge, WAIT# HIGH only after DATA_ENABLE and data
  direction are valid, and no WAIT# reassertion until IORQ# and
  RD#/WR# are inactive.
- **Oscilloscope evidence:** Use the DHO814 groups in the
  [oscilloscope capture plan](../hardware/oscilloscope.md#four-channel-connections-and-expected-results)
  and repeat the listed alternatives for each analogue signal.
- **Logic-analyzer evidence:** At every tested rate, also save the
  DSLogic Plus
  [Group A, B, C-IN, C-OUT, and D captures](../hardware/logic-analyzer.md)
  using 16-channel, 100 MHz Buffer Mode. Do not infer whole-bus
  ordering from four analogue channels, and do not imply that the
  16-channel analyzer captured the complete address bus, complete data
  bus, and controls simultaneously.

The qualified frequency is the highest error-free step at or below
6 MHz for which the DSLogic capture set proves digital ordering and the
DHO814 proves memory margin and the complete WAIT/clock-stop handshake.
Report 6.5-8 MHz separately as experimental even if they pass; do not
claim any rate without equivalent timing evidence and repeated
cold/runtime tests.

Record the requested and reported clock, firmware revision, rail voltages,
functional error counts, worst measured timing margins, and capture filenames
for each step. On any failed criterion, stop increasing the clock and return
to the last fully passing rate. After changing wiring, repeat qualification;
the old result describes the old physical build.
