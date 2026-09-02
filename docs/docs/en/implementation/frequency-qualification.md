# 8.12 Frequency Qualification

**Prerequisite:** The [Phase 10 pass gate](phase-10-websocket.md#pass-gate) must pass at
1 MHz before testing any higher rate.

Begin only after CP/M has flushed all disks.
Use USB `+`/`-` to select each frequency under the firmware's
BUSREQ#/BUSACK# and core-1 flash-quiescence interlocks. Test 2 MHz, then
increase in 500 kHz steps to 6 MHz. If and only if 6 MHz passes with margin,
continue experimentally in 500 kHz steps to 8 MHz. At each step run `a` for
CPU SRAM readback/address activity and `h` for the one-hour self-checking
memory loop plus continuous terminal IN/OUT while measuring stop latency.
At 1, 2, 3, 4, 5, and 6 MHz, plus every experimental step, also apply address patterns 0x0000, 0xFFFF,
0x5555, 0xAAAA, walking one, and walking zero while capturing A0, A7,
A8, and A15 at the SRAM pins. Capture CLK, MREQ#, RD#/WR#, SRAM
CE#/OE#/WE#, and D0-D7 as well; require
valid read data before the Z80 setup deadline and every SRAM write pulse
to meet the 45 ns minimum after propagation through the GAL and AHCT244.
For every I/O cycle, also require WAIT# LOW before the Z80 sampling edge,
WAIT# HIGH only after DATA_ENABLE and data direction are valid, and no
WAIT# reassertion until IORQ# and RD#/WR# are inactive.
Use the DHO814 groups in the
[oscilloscope capture plan](../hardware/oscilloscope.md#four-channel-connections-and-expected-results)
and repeat the listed alternatives for each analogue signal. At every tested
rate also save the DSLogic Plus
[Group A, B, C-IN, C-OUT, and D captures](../hardware/logic-analyzer.md).
Use 16-channel, 100 MHz Buffer Mode for timing evidence. Do not infer
whole-bus ordering from four analogue channels, and do not imply that the
16-channel analyzer captured the complete address bus, complete data bus, and
controls simultaneously.
The qualified frequency is the highest error-free step at or below
6 MHz for which the DSLogic capture set proves digital ordering and the
DHO814 proves memory margin and the complete WAIT/clock-stop handshake.
Report 6.5-8 MHz separately as experimental
even if they pass; do not claim any rate without equivalent timing
evidence and repeated cold/runtime tests.
