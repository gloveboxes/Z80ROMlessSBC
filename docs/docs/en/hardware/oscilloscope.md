# RIGOL DHO814 Capture Plan

## Purpose

The DHO814 validates actual voltage, edge shape, ringing, pulse width,
and timing margin at the device pins. Its four channels allow one stable
trigger and three related nodes to be captured in the same acquisition.
It does not replace the
[DSLogic Plus](logic-analyzer.md): four analog channels cannot prove
the state or ordering of the complete A0-A15 and D0-D7 buses. Conversely,
the logic analyzer does not prove analogue voltage or signal integrity.

## Reading your first capture

For opt-in automated evidence collection, see [MCP acceptance runner](#mcp-acceptance-runner). Automation does not replace the electrical and digital pass gates below.

The horizontal axis is time; the vertical axis is voltage relative to the
probe's GND. `200 ns/div` means each large horizontal division spans 200 ns.
A **trigger** tells the scope which event to align on; it does not generate
that event. Arm a Single capture before issuing the diagnostic command.
If the scope keeps waiting, check the trigger source, edge, level, and whether
the test actually ran before concluding that the circuit failed.

A multimeter reports steady or averaged voltage; a running 0-5 V clock may
therefore read around 2.5 V on a meter without being faulty. Use the scope
to distinguish a switching clock from a line stuck at an invalid mid-level.

## Probe and instrument preparation

1. Before connecting any probe or other input/output lead to the DHO814,
   use its supplied ground cable to bond the oscilloscope chassis to
   protective earth. The Type-C power connection does not ground this
   non-isolated oscilloscope. Its BNC shells, probe grounds, chassis, and
   digital-interface grounds are common; never use an ordinary passive probe
   for a floating measurement.
2. Connect each probe to its channel, set the physical probe switch to
   **10X**, and open that channel's **Vertical > Probe > Probe Ratio** menu.
   Set the scope ratio to **10X** as well. A mismatched ratio makes every
   displayed voltage incorrect.
3. Before each bring-up session, connect each probe in turn to the front
   panel compensation output and its adjacent ground terminal. Adjust the
   probe until the square wave has flat tops and square corners, matching
   the manual's "Perfectly compensated" example. Repeat for CH1-CH4.
4. Prefer a ground spring on every probe. If a wire ground must be used,
   keep it shorter than 20 mm. Connect all probe grounds only to nearby points
   on the circuit's common GND, and never to a node with voltage relative to
   earth, including +3.3 V, +5 V, or any signal node.
5. Recall the default setup, then configure every active channel as
   **1 MOhm input, DC coupling, 10X probe, Invert OFF, Delay 0 s**, and
   **Bandwidth Limit OFF**. Use the full 100 MHz bandwidth for timing,
   edge, overshoot, and ringing captures. A bandwidth limit may be used
   only for a separately labelled supply-noise capture.
6. Set **Horizontal > Acquisition = Normal**. The DHO800 manual identifies
   Normal as the best mode for most waveforms and confirms that sampling is
   real-time only. Do not use Average to qualify glitches or transitions;
   averaging can hide non-repetitive faults. Peak mode may be used as a
   second search capture, but the pass/fail evidence must include a Normal
   acquisition.
7. Start with **Memory Depth = Auto** for repetitive clock checks. For
   single-shot RESET#, BUSREQ#/BUSACK#, DMA, I/O-trap, or intermittent-fault
   captures, select the largest depth available for the number of enabled
   channels: **25 Mpts** with one channel, **10 Mpts** with two channels, or
   **5 Mpts** with three or four channels. The corresponding DHO814 maximum
   real-time sample rates are **1.25 GSa/s**, **625 MSa/s**, and
   **312.5 MSa/s**. The four-channel maximum gives about 39 samples per 8 MHz
   clock period and is suitable for the listed multichannel timing captures.
   For rise/fall time, overshoot, or ringing, disable unneeded channels and
   shorten the captured time span to obtain the highest displayed sample
   rate. Place the trigger at about 40% of the screen so both pre-trigger
   cause and post-trigger response are visible, and record the displayed
   sample rate and memory depth with the saved evidence.
8. For 3.3 V nodes start at **1 V/div**; for 5 V nodes start at
   **2 V/div**. Position ground markers near the bottom of each lane without
   overlapping traces. Adjust one step finer when useful, but keep ground
   and both logic levels visible. Start the horizontal scale at 200 us/div
   for 1 kHz, 2 us/div for 100 kHz, 200 ns/div for 1 MHz, and 50 ns/div for
   2-8 MHz. Tighten the scale around an edge when measuring rise time or
   ringing.
9. Open **Trigger**, choose **Type = Edge**, **Source = CH1**,
   **Coupling = DC**, and choose the edge stated in the table below. Set the
   level to **1.65 V** when CH1 is a 3.3 V node or **2.5 V** when it is a
   5 V node. Use **Sweep = Normal** for repetitive captures and
   **Sweep = Single** for ownership, reset, trap, and other one-off events;
   arm Single before issuing the firmware command. Do not use Auto sweep as
   pass evidence because it can force an acquisition without the requested
   event.
10. Add automatic measurements appropriate to the capture: **Frequency,
    Period, +Duty, -Duty, Rise Time, Fall Time, +Width, -Width, Vmax, Vmin,
    Vpp, Overshoot**, and the applicable **Delay** measurement between CH1
    and another channel. Check timing with cursors as well; automatic
    measurements are invalid if the relevant edges or levels are not fully
    visible.
11. Stop acquisition and power off the SBC before attaching or moving probe
   clips; stopping acquisition does not make the circuit electrically safe.
   Save a screen image
    and waveform for every pass gate, naming it with the phase, clock rate,
    stimulus, and probed signals. Record the displayed sample rate, memory
    depth, probe ratio, bandwidth limit, and measured minima/maxima.

## Four-channel connections and expected results

Connect the channels exactly as listed, using the physical IC pin as the
probe point. CH1 is the normal trigger source unless the row explicitly names
another source. Where a row lists alternatives, repeat the capture for every
alternative while leaving the other channels in place where possible.

| Purpose | CH1 / trigger | CH2 | CH3 | CH4 | Trigger and expected outcome |
| --- | --- | --- | --- | --- | --- |
| Clock translation | Pico GP2, header pin 4 | AHCT244 pin 18 | Z80 CLK pin 6 | Z80 RESET# pin 26 | CH1 rising. At 1 kHz, 100 kHz, 1 MHz, and each qualification rate, CH1 is about 0-3.3 V and CH2/CH3 about 0-5 V. CH2 and CH3 match frequency and duty cycle, contain no extra edges, and differ only by interconnect delay. RESET# stays HIGH while running. |
| Reset sequence | Z80 RESET# pin 26 | Z80 CLK pin 6 | Z80 M1# pin 27 | Z80 MREQ# pin 19 | CH1 rising, Single. RESET# remains LOW for at least three complete clocks. After release, M1# and MREQ# produce valid active-LOW opcode-fetch activity with no runt RESET# or CLK pulse. |
| Flash quiescence | Z80 BUSREQ# pin 25 | Z80 BUSACK# pin 23 | Z80 RESET# pin 26 | Z80 CLK pin 6 | CH1 falling, Single. BUSACK# subsequently falls for flash erase/program, not memory DMA. Pico data paths remain isolated. Trap is rearmed before release; clocks contain no runt edges. |
| SRAM write control | Z80 CLK pin 6 | Z80 MREQ# pin 19 | Z80 WR# pin 22 | SRAM WE# pin 29 | CH1 rising. During CPU writes, including injected LD (HL),n, SRAM WE# follows WR# through AHCT244. WE# has no extra pulse and its LOW width is at least 45 ns. |
| SRAM read timing | Z80 CLK pin 6 | Z80 MREQ# pin 19 | SRAM OE# pin 24 | One SRAM data pin D0-D7: pins 13-15, 17-21 | CH1 rising. Repeat CH4 for all eight bits and the 00/FF/55/AA patterns. MREQ# and OE# assert LOW once per read; CH4 reaches the expected 0 V or 5 V state and is stable before the Z80 sampling edge. Use the DSLogic Group B capture to prove the complete byte and digital ordering. |
| Address integrity | Z80 CLK pin 6 | SRAM A0 pin 12 or A7 pin 5 | SRAM A8 pin 27 | SRAM A15 pin 31 | CH1 rising. Repeat with A0 and A7 on CH2 at 1, 2, 3, and 4 MHz, then at every [frequency-qualification rate](../implementation/frequency-qualification.md). For 0000/FFFF/5555/AAAA and walking patterns, each observed line matches the commanded bit, reaches valid 0/5 V levels, is stable during the active memory control interval, and has no double edge or excessive ringing. The DSLogic Group A capture proves A0-A15 together. |
| Boot-read inhibit | BOOT_READ_DISABLE at GP5, header pin 7 | Buffered RD# at AHCT244 pin 7 | SRAM OE# pin 24 | Z80 CLK pin 6 | CH1 edge, Single. Inhibit HIGH keeps OE# HIGH throughout injected instruction reads. Lower it only with Pico output isolated for intentional SRAM reads or normal execution. Repeat CH1 at AHCT244 pin 14. No unintended OE pulse is permitted. |
| Data-path exclusion | GP6 up-OE request, header pin 9 | GP7 down-OE, header pin 10 | AHCT245 OE# pin 19 | LVC245 OE# pin 19 | CH1 edge, Single; repeat CH2 edge and RD# at pin 21. Both OEs default HIGH. Up OE is GP6 OR RD#; down OE follows GP7. Both actual OEs must never be LOW together. This is a firmware rule with RD qualification, not an all-state hardware interlock; do not force both requests LOW. |
| I/O trap and WAIT# | Z80 IORQ# pin 20 | Z80 WAIT# pin 24 | Z80 CLK pin 6 | IO_RELEASE at GP9, header pin 12 | CH1 falling, Single. HCT32 asserts WAIT before the sampling edge. GP9 rises only after the data path is ready. Slow clocks complete I/O, then isolate/rearm before PWM resumes. WAIT must not reassert while IORQ/strobe remain active. Repeat CH4 at RD#, WR#, and both actual OEs. |
| Supply integrity | +5 V logic-rail entry at the 100 uF bulk capacitor | Farthest-board +5 V rail | Pico 3V3 header pin 36 | Z80 CLK pin 6 | Trigger on CH4 rising for repetitive operation; use Single on commands. Repeat at idle, injected RAM patterns, Z80 memory loop, disk write, and Wi-Fi traffic. CH1/CH2 remain 4.75-5.25 V; CH3 meets the Pico rail specification. No capture may show more than 250 mV droop or reset/clock disturbance. Keep DC-coupled full-bandwidth pass evidence. |

For logic nodes, a measured LOW must satisfy the receiving device's LOW
limit and a measured HIGH must satisfy its HIGH limit; use the device-specific
thresholds and margins stated in the relevant phase rather than treating the
trigger level as a pass threshold. Investigate overshoot below GND or above
the node's supply, non-monotonic threshold crossings, ringing that creates a
second crossing, or an unusually fast rise/fall time. With the supplied
150 MHz passive probe, treat a measured rise/fall time approaching about
4.2 ns as limited by the probe-plus-scope measurement system, not as a
definitive measurement of the device-under-test edge. The 4.2 ns value is the
root-sum-square combination of the probe's approximately 2.3 ns and the
DHO814 front end's approximately 3.5 ns calculated rise-time limits. The scope
validates analogue quality on the listed nodes; the DSLogic Plus remains
mandatory for repeated bus-wide digital capture groups and the final frequency
claim.

## MCP Acceptance Runner

The maintained runner is `scripts/scope-acceptance.mjs`, with evaluation in `scripts/scope-evidence.mjs`. It collects Stage 2 clock translation or Stage 8 I/O captures. External SPI mode is retired. It does not flash firmware, inject electrical faults, or certify a board. Reports retain `qualified=false` until all manual and independent gates pass. The legacy single-pause evaluator returns inconclusive for multi-step completion: inspect the raw clocks, data OEs, and rearming manually before accepting this revision.

### Preparation

1. Build the correct cumulative firmware and pass preceding phases. Stage 2 uses the empty Z80 socket; Stage 8 requires the assisted-loader board at the 1 MHz baseline.
2. Power off before changing probes. Use compensated 10X probes, full bandwidth, DC coupling, 1 MOhm inputs, verified deskew and common circuit ground. Scope ground clips are not floating inputs.
3. Install Node.js 20 or later and run `npm ci`. Build the current [rigol-mcp image](https://github.com/gloveboxes/rigol-mcp) in the selected runtime and initialize its capture volume using that project's instructions.
4. Stop the registered Rigol MCP server yourself and close the Pico serial terminal. The runner starts one dedicated server and calls it sequentially. `--exclusive-session` is your acknowledgement, not automatic proof that no other client is connected.
5. Use a new output directory under `build/bench/`. The runner checks firmware identity and scope readbacks before collecting evidence, and asks for board/probe/datasheet provenance.

### Stage 2 Clock Translation

Connect CH1 to Pico GP2 (3.3 V), CH2 to Z80 socket CLK pin 6 (5 V), and CH3 to the Z80 VCC supply node. Repeat a separate capture on AHCT244 pin 18 to distinguish buffer delay from interconnect delay. Keep the Stage 2 reset state; do not release a CPU merely to obtain a clock measurement.

```sh
mkdir -p build/bench
npm run scope:acceptance -- --mode stage2 --runtime container \
   --ip 192.168.1.43 --port /dev/cu.usbmodemYOUR_DEVICE \
   --output build/bench/stage2-first --exclusive-session --bench-confirmed
```

Use `--runtime docker` for Docker. Commands `1`, `2`, and `3` report requested and calculated actual clock rates. The runner compares hardware frequency to the calculated rate within 1%, requires 45-55% duty and corresponding pulse widths, checks the 4.75-5.25 V supply, and compares settled Z80 clock HIGH against measured VCC minus 0.5 V. The 1% check detects gross errors, not oscillator accuracy or jitter. Sentinel/missing readings are inconclusive, never zero or PASS.

Use robust settled levels for logic swing and raw extrema for overshoot/undershoot; inspect both against receiving-device limits. Check extra threshold crossings and the first/last pulses. Full qualification still requires every buffer path, HCT32 gates, startup and power-cycle tests. A firmware `DONE` message means only that stimulus completed.

### Stage 8 Clock Stop and Resume

Connect CH1=Z80 IORQ# pin 20, CH2=WAIT# pin 24, CH3=CLK pin 6, CH4=Pico IO_RELEASE GP9 (header pin 12). Thresholds are 2.5 V on the 5 V side and 1.65 V on the Pico side. Verify actual rails separately. Set these variables using the CPU datasheet and approved latency/pause budget; the runner has no guessed limits:

```sh
npm run scope:acceptance -- --mode stage8 --runtime container \
   --ip 192.168.1.43 --port /dev/cu.usbmodemYOUR_DEVICE \
   --output build/bench/stage8-first \
   --minimum-high-ns "$MIN_CLK_HIGH_NS" --minimum-low-ns "$MIN_CLK_LOW_NS" \
   --maximum-last-edge-us "$MAX_LAST_EDGE_US" --maximum-pause-us "$MAX_PAUSE_US" \
   --duration 60 --captures 3 --exclusive-session --bench-confirmed
```

The runner starts the existing one-hour RAM/USB checker, samples I/O events, checks fault-counter deltas, and holds the CPU in reset on exit. A 60-second run is not a one-hour pass. For a full run use `--duration 3600`; the firmware's one-hour PASS message must also be observed. Exercise USB traffic externally; later-stage Wi-Fi/storage workload evidence remains separate. Discrete captures cannot prove there were no faults between records.

Aligned stopped RAW CSV captures are streamed to the host. The evaluator separates ordinary pulse minima from extended intervals, checks WAIT and IO_RELEASE ordering, and rejects unaligned data. Its exactly-one-pause assumption is not sufficient for stepped completion; retain inconclusive results and review every completion clock manually. Insufficient sampling or incomplete events are inconclusive. Never include deliberate pauses/steps in periodic-jitter statistics.

The last observed edge is not the instant PWM stopped: uncertainty includes clock phase. Do not describe it as exact ISR-entry latency. Repeat CH4 on RD#, WR#, GP6/GP7 and both actual transceiver OEs. Measure WAIT and data setup/hold separately. Prove both OEs HIGH and GP9 LOW before PWM resumes. Keep DSLogic Group A-D captures for whole-bus ordering.

Requalify service time, throughput, first resumed pulse, and watchdog margins
after trap changes; the 500 ms software deadline is not a CPU timing limit.

### Reports and Restoration

Reports include source revision/dirty state, scope identity, requested/applied settings, probe ratios, thresholds, clock rates, sample-rate/memory metadata, counters, measurements, capture paths and manual gates. `serial.log`, copied captures and setup binary/sidecar are retained. Source revision describes the current source tree; verify separately that the flashed UF2 matches it.

The runner stops the diagnostic workload and asks before restoring the saved scope setup and waveform-transfer settings. Check the restoration result and final SCPI queue; a write alone is not proof of restoration. Declined restoration, mismatched settings or errors produce a nonzero exit. Instrument restoration does not restart the preceding firmware workload. If killed or disconnected, cleanup may be incomplete: power down or hold reset safely and inspect state before retrying. Restart the registered MCP server yourself only after the runner exits.

### Fault Injection

Run host simulation first:

```sh
npm run test:control
npm run test:scope
```

Native tests compile production bus, CPU, loader, and trap code against GPIO,
clock, and time fakes. They cover injection cycle/byte behavior, data exclusion,
address boundaries, verification corruption, absent/stuck BUSACK, stuck I/O
controls, and invalid RD/WR combinations. They prove code behavior, not real
Z80 reset-exit phase, analogue safety, or timing.

| Fault | Required observed behavior |
| --- | --- |
| BUSACK never asserts | Request timeout, BUSREQ HIGH, Pico data isolated, no flash programming. The request-timeout path leaves the CPU running. |
| BUSACK stuck LOW on release | RESET LOW, Pico data isolated, boot inhibit HIGH, clock stopped. Capture translated SRAM controls too. |
| IORQ or selected RD/WR stuck LOW | Release deadline expires; isolate, assert reset, supply reset clocks and reboot via watchdog. Capture the transient recovery, not just the final pins. |
| Injected image verification failure | Hold RESET LOW, both data OEs HIGH, inhibit HIGH and clock stopped; do not execute an unverified image. |
| Early IO_RELEASE or overlapping transceiver OEs | Reject qualification; capture GP9, both actual OEs, and data setup at the CPU sampling edge. |

Physical fault injection needs an approved interposer that disconnects the real driver before forcing a level, power-off fixture changes, and explicit operator approval. Never short active push-pull outputs, bypass voltage translation, or leave installed 5 V ICs unpowered while driven. No electrical fault injection is automated here. Persistent faults may reboot repeatedly. Record recovery at receiving pins and repeat baseline qualification after wiring changes.