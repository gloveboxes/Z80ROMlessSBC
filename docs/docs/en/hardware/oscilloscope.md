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
| DMA ownership | Z80 BUSREQ# pin 25 | Z80 BUSACK# pin 23 | Z80 RESET# pin 26 | Z80 CLK pin 6 | CH1 falling, Single. BUSACK# subsequently falls and remains LOW for DMA; CLK is deliberately stopped only by the firmware sequence. On release, BUSREQ# rises before BUSACK# returns HIGH and normal clocks resume without a runt edge. |
| SRAM write control | Z80 CLK pin 6 | Z80 MREQ# pin 19 | Z80 WR# pin 22 | SRAM WE# pin 29 | CH1 rising. During a CPU write, MREQ# and WR# assert LOW and SRAM WE# follows the selected write control through GAL/AHCT244 propagation. WE# has no extra pulse and its LOW width is at least 45 ns. |
| SRAM read timing | Z80 CLK pin 6 | Z80 MREQ# pin 19 | SRAM OE# pin 24 | One SRAM data pin D0-D7: pins 13-15, 17-21 | CH1 rising. Repeat CH4 for all eight bits and the 00/FF/55/AA patterns. MREQ# and OE# assert LOW once per read; CH4 reaches the expected 0 V or 5 V state and is stable before the Z80 sampling edge. Use the DSLogic Group B capture to prove the complete byte and digital ordering. |
| Address integrity | Z80 CLK pin 6 | SRAM A0 pin 12 or A7 pin 5 | SRAM A8 pin 27 | SRAM A15 pin 31 | CH1 rising. Repeat with A0 and A7 on CH2 at 1, 2, 3, and 4 MHz, then at every [frequency-qualification rate](../implementation/frequency-qualification.md). For 0000/FFFF/5555/AAAA and walking patterns, each observed line matches the commanded bit, reaches valid 0/5 V levels, is stable during the active memory control interval, and has no double edge or excessive ringing. The DSLogic Group A capture proves A0-A15 together. |
| GAL ownership mux | RESET# at Z80 pin 26 or BUSACK# at Z80 pin 23 | Selected Z80 control: WR# pin 22, RD# pin 21, or MREQ# pin 19 | Matching GAL output: pin 14, 15, or 16 | Matching SRAM control: WE# pin 29, OE# pin 24, or CE# pin 22 | Trigger on the ownership input transition, Single; repeat rising/falling and all three paths. GAL and SRAM outputs remain inactive HIGH while ownership changes when both candidate controls are HIGH. Under CPU ownership they follow the Z80 control; under RESET#/BUSACK# DMA ownership they follow the Pico control. No active-LOW glitch is permitted. |
| Data-transceiver interlock | DATA_ENABLE at Pico GP7, header pin 10 | DATA_DIR at Pico GP6, header pin 9 | GAL pin 17 / AHCT245 OE# pin 19 | GAL pin 18 / LVC245 OE# pin 19 | CH1 rising and falling, Single. With DATA_ENABLE LOW both OE# outputs remain HIGH. With DATA_ENABLE HIGH and DATA_DIR HIGH, CH3 is LOW and CH4 HIGH; with DATA_DIR LOW, CH3 is HIGH and CH4 LOW. CH3 and CH4 must never be LOW simultaneously, including during transitions. |
| I/O trap and WAIT# | Z80 IORQ# pin 20 | Z80 WAIT# pin 24 | Z80 CLK pin 6 | DATA_ENABLE at Pico GP7, header pin 10 | CH1 falling, Single. WAIT# falls from GAL hardware before the Z80 sampling edge; the clock stops at a complete edge. WAIT# rises only after DATA_ENABLE and direction are valid, then clocking resumes. WAIT# does not reassert before IORQ# and RD#/WR# are inactive. Use a second capture with CH4 on RD# pin 21, then WR# pin 22. |
| Supply integrity | +5 V logic-rail entry at the 100 uF bulk capacitor | Farthest-board +5 V rail | Pico 3V3 header pin 36 | Z80 CLK pin 6 | Trigger on CH4 rising for repetitive operation; use Single on the relevant command for transients. Repeat at idle, DMA patterns, Z80 memory loop, disk write, and Wi-Fi traffic. CH1/CH2 remain 4.75-5.25 V and CH3 remains within the Pico 3.3 V rail specification; no capture may show more than 250 mV rail droop or a reset/clock disturbance. For ripple detail, AC coupling or a bandwidth limit is allowed only in an additional labelled capture; retain the DC-coupled full-bandwidth capture as pass evidence. |

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