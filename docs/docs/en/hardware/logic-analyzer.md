# DreamSourceLab DSLogic Plus Capture Plan

## Purpose and instrument limits

The DSLogic Plus proves simultaneous digital state, event ordering, bus values,
and control sequencing. It complements the
[RIGOL DHO814](oscilloscope.md): the logic analyzer
answers **what happened and in what order**, while the oscilloscope proves
voltage margin, rise/fall time, ringing, overshoot, and close analogue timing.
Never use a logic-analyzer HIGH or LOW display as proof that a receiving device's
datasheet voltage limits were met.

A displayed bus value is only as correct as the channel mapping: label the
channels before capture and put bit 0 at the least-significant position.
The analyzer samples rather than recording every instant; at 100 MHz,
samples are 10 ns apart, so a shorter pulse may be missed. A clean-looking
digital trace is not proof that the analog waveform has no glitch.

The official DSLogic Plus data sheet specifies:

| Property | DSLogic Plus limit |
| --- | --- |
| Digital channels | 16, CH0-CH15 |
| Buffer Mode sample rate | 400 MHz / 4 channels, 200 MHz / 8 channels, 100 MHz / 16 channels |
| Stream Mode sample rate | 100 MHz / 3 channels, 50 MHz / 6 channels, 25 MHz / 12 channels, 20 MHz / 16 channels |
| Hardware memory | 256 Mbits |
| Buffer depth without RLE | 256 Mbits divided by enabled channel count |
| Maximum stream depth | 16G samples |
| Input threshold | 0-5 V, adjustable in 0.1 V steps |
| Input impedance | 250 kOhm in parallel with approximately 13 pF |
| Protected CH0-CH15 range | -30 V to +30 V with the supplied fly wires |
| 100 MHz timing accuracy | Within one sample interval, approximately +/-10 ns per captured edge |

All 16 channels are required for a complete address-bus capture, so this project
uses **16-channel, 100 MHz Buffer Mode** for qualification. At the experimental
8 MHz clock limit this gives 12.5 samples per clock period, satisfying DSView's
recommendation to use at least 10 times the measured signal frequency when phase
accuracy matters. It cannot resolve analogue behavior or prove a timing margin
smaller than its sample uncertainty; use the
[DHO814 capture plan](oscilloscope.md) for those cases.

The 16-channel Stream Mode limit is 20 MHz, only 2.5 samples per 8 MHz clock.
Use it for long-duration state/fault searches at low clock rates, not as timing
qualification evidence at the final operating frequency.

## Safety and connection preparation

1. Install the current DSView release from the
   [DreamSourceLab download page](https://www.dreamsourcelab.com/download/).
   Connect DSLogic Plus directly to a reliable USB port. Open DSView and require
   the hardware indicator to turn green and the device selector to identify
   **DSLogic Plus** before attaching it to the circuit.
2. The probe grounds are connected through USB to the host computer ground.
   Connect them only to the SBC's common GND. Never attach a ground lead to
   +3.3 V, +5 V, a signal, or a floating/hot DUT. If the host is mains-grounded,
   DSLogic ground is earth-referenced.
3. Power off the SBC before attaching or moving clips. Connect ground leads
   first, then signal leads. Remove signal leads first when disconnecting.
4. Use the supplied shielded fly wires. Each active channel has an independent
   ground return; connect those returns to nearby common-GND points. Although
   the Z80 clock is at most 8 MHz, its edges are fast, so use the guide's
   high-frequency connection method rather than one long shared ground lead.
5. Probe the physical destination pin listed below, not a distant jumper end.
   Sixteen inputs add meaningful capacitive loading to a solderless breadboard.
   Recheck operation at 1 MHz after connecting each group, and do not stack a
   scope probe on the same node unless that combined loaded condition is the
   test being qualified.
6. Do not connect CK, TI, or TO for the standard project captures. CK and TI are
   limited to the 0-3.3 V domain; attaching either to a 5 V Z80 signal violates
   the hardware data sheet. CH0-CH15 provide every trigger used here.

## Baseline DSView configuration

1. Press **O** or open **Options**. Select **Logic Analyzer** and
   **Buffer Mode**, with **Upload captured data** as the stop option.
2. Select **Use 16 Channels (Max 100 MHz)**, enable CH0-CH15, set
   **Sample Rate = 100 MHz**, and begin with **Sample Duration = 100 ms**.
   The official maximum at 100 MHz with 16 channels is approximately 167.77 ms.
3. Set **Filter Targets = None**. The **1 Sample Clock** filter suppresses pulses
   shorter than one sample interval and must not be used as pass evidence.
4. Disable **RLE Compress** for baseline qualification so capture depth is fixed
   and reproducible. A separately labelled RLE capture may be used to search for
   sparse events, but it does not replace the baseline capture.
5. Set the single device threshold for the selected group: **3.5 V** for Group A,
   **2.2 V** for Group B, or **1.6 V** for mixed 3.3 V/TTL Groups C and D. These
   thresholds make transitions visible at useful digital decision levels; the
   DHO814 still proves device-specific $V_{IH}$/$V_{IL}$ margins.
6. Select **Single Capture** and **Normal Capture**. Do not use Instant Capture
   as pass evidence because it ignores trigger settings. Repetitive Capture is
   useful only as additional evidence for repeatable events.
7. Press **T** to open Trigger. Use **Simple Trigger** unless a scenario below
   requests a bus pattern. Clear every stale channel condition first: multiple
   Simple Trigger conditions are ANDed at the same sample point.
8. Start with the trigger position at **30%** to retain the cause and more
   post-trigger response. Use 50% when equal pre/post context is useful. Stream
   Mode fixes the trigger close to the start and is not used for these gates.
9. Rename every channel to the signal shown in its group before capture. Use
   cursors and edge snap to measure ordering; record the sample count between
   relevant edges as well as the displayed time.
10. Save the complete `.dsl` capture and DSView session, export a VCD or raw CSV,
    and save a PNG screenshot. Include phase, clock rate, stimulus, group,
    threshold, sample rate, duration, filter, RLE state, and trigger in the name.

## Group A: complete SRAM address bus

Use threshold **3.5 V**. This group proves all address bits simultaneously at the
SRAM receiver pins.

| DSLogic channel | Signal | Physical probe point |
| --- | --- | --- |
| CH0 | A0 | SRAM pin 12 |
| CH1 | A1 | SRAM pin 11 |
| CH2 | A2 | SRAM pin 10 |
| CH3 | A3 | SRAM pin 9 |
| CH4 | A4 | SRAM pin 8 |
| CH5 | A5 | SRAM pin 7 |
| CH6 | A6 | SRAM pin 6 |
| CH7 | A7 | SRAM pin 5 |
| CH8 | A8 | SRAM pin 27 |
| CH9 | A9 | SRAM pin 26 |
| CH10 | A10 | SRAM pin 23 |
| CH11 | A11 | SRAM pin 25 |
| CH12 | A12 | SRAM pin 4 |
| CH13 | A13 | SRAM pin 28 |
| CH14 | A14 | SRAM pin 3 |
| CH15 | A15 | SRAM pin 31 |

## Group B: memory data and controls

Use threshold **2.2 V**. Group B correlates the complete byte with the CPU and
SRAM control cycle. The analogue scope captures remain responsible for proving
the SRAM's stricter write-data HIGH margin.

| DSLogic channel | Signal | Physical probe point |
| --- | --- | --- |
| CH0-CH7 | D0-D7 in ascending order | SRAM pins 13, 14, 15, 17, 18, 19, 20, 21 |
| CH8 | CLK | Z80 pin 6 |
| CH9 | MREQ# | Z80 pin 19 |
| CH10 | RD# | Z80 pin 21 |
| CH11 | WR# | Z80 pin 22 |
| CH12 | SRAM CE# | SRAM pin 22 |
| CH13 | SRAM OE# | SRAM pin 24 |
| CH14 | SRAM WE# | SRAM pin 29 |
| CH15 | M1# | Z80 pin 27 |

### Optional DSView Z80 decoder cross-check

After preserving the raw Group B capture, add DSView's **Z80** protocol
decoder and bind D0-D7 to CH0-CH7, M1# to CH15, RD# to CH10, WR# to CH11,
and optional MREQ# to CH9. Leave IORQ# and A0-A15 unbound for this group.
The decoder can annotate opcode fetches and memory read/write bytes, but it
cannot report the full address and must not replace manual inspection of the
raw controls. Group C remains authoritative for trapped I/O, and Group A
remains authoritative for the complete address bus.

## Group C: trapped I/O and data-path interlock

Use threshold **1.6 V**. Repeat the group once for IN with RD# on CH10 and once
for OUT with WR# on CH10.

| DSLogic channel | Signal | Physical probe point |
| --- | --- | --- |
| CH0-CH7 | D0-D7 in ascending order | SRAM pins 13, 14, 15, 17, 18, 19, 20, 21 |
| CH8 | CLK | Z80 pin 6 |
| CH9 | IORQ# | Z80 pin 20 |
| CH10 | RD# for IN; WR# for OUT | Z80 pin 21; repeat at pin 22 |
| CH11 | WAIT# | Z80 pin 24 |
| CH12 | DATA_ENABLE | Pico GP7, header pin 10 |
| CH13 | DATA_DIR | Pico GP6, header pin 9 |
| CH14 | Pico-to-bus transceiver OE# | SN74AHCT245 pin 19 |
| CH15 | Bus-to-Pico transceiver OE# | SN74LVC245 pin 19 |

## Group D: SRAM ownership and control propagation

Use threshold **1.6 V**. This mixed group follows CPU/Pico ownership through the
GAL and AHCT244 to the final SRAM pins.

| DSLogic channel | Signal | Physical probe point |
| --- | --- | --- |
| CH0 | RESET# | Z80 pin 26 |
| CH1 | BUSREQ# | Z80 pin 25 |
| CH2 | BUSACK# | Z80 pin 23 |
| CH3 | Z80 MREQ# | Z80 pin 19 |
| CH4 | Z80 RD# | Z80 pin 21 |
| CH5 | Z80 WR# | Z80 pin 22 |
| CH6 | SRAM_CE_PRE# | ATF22V10 pin 16 |
| CH7 | SRAM_OE_PRE# | ATF22V10 pin 15 |
| CH8 | SRAM_WE_PRE# | ATF22V10 pin 14 |
| CH9 | SRAM CE# | SRAM pin 22 |
| CH10 | SRAM OE# | SRAM pin 24 |
| CH11 | SRAM WE# | SRAM pin 29 |
| CH12 | PICO_CE# | Pico GP5, header pin 7 |
| CH13 | PICO_OE# | Pico GP26, header pin 31 |
| CH14 | PICO_WE# | Pico GP22, header pin 29 |
| CH15 | CLK | Z80 pin 6 |

## Trigger and expected outcomes

| Purpose | Group and trigger | Expected result |
| --- | --- | --- |
| Address integrity | Group A. Set all 16 Simple Trigger levels to match 0000, FFFF, 5555, or AAAA; all conditions are ANDed. Repeat for each fixed pattern and trigger on 0000 for walking/CPU-read sequences. | Every captured word equals the commanded address, each walking pattern changes only the intended bit, and no address transition occurs during the active sampling interval of a memory cycle. Repeat at every qualified clock rate. |
| Opcode fetch | Group B, CH15 M1# falling. | M1#, MREQ#, RD#, CE#, and OE# assert in the expected active-LOW sequence; WE# remains HIGH and D0-D7 contain the fetched opcode before the Z80 sampling edge. |
| SRAM read | Group B, CH10 RD# falling with CH9 MREQ# LOW. | CE# and OE# assert once, WE# remains HIGH, and all eight data bits settle to the expected byte before the sampling edge. No extra control transition is permitted. |
| SRAM write | Group B, CH11 WR# falling with CH9 MREQ# LOW. | D0-D7 contain the expected byte before WE# falls and remain stable through its LOW pulse; CE# and WE# assert once and OE# remains HIGH. DSLogic screens pulse presence and ordering only; the DHO814 must prove the exact 45 ns minimum WE# LOW width. |
| Reset and restart | Group D, CH0 RESET# rising, trigger position 30%. | RESET# was LOW for at least three clocks. While LOW, final SRAM controls follow inactive Pico controls. After release, the Z80-side controls become authoritative without any active-LOW glitch. |
| DMA ownership | Group D, CH2 BUSACK# falling for acquisition; repeat rising for release. | BUSREQ# falls before BUSACK#. Once BUSACK# is LOW, GAL and SRAM controls select the Pico candidates. On release, Pico controls are inactive before BUSREQ# rises; BUSACK# then rises and CPU controls resume without overlap. |
| Ownership hazard | Group D, CH0 or CH2 either edge while both candidate controls are HIGH. | GAL pre-controls and final SRAM controls remain continuously HIGH through the ownership transition. Any sampled LOW pulse fails the phase and requires a DHO814 close-up. |
| Trapped IN | Group C with RD# on CH10; trigger CH9 IORQ# falling. | WAIT# falls before the Z80 WAIT sampling edge. DATA_DIR selects bus-to-Pico, only the LVC245 OE# falls, D0-D7 are sampled, DATA_ENABLE releases WAIT#, and both IORQ#/RD# return HIGH before the path is disabled. |
| Trapped OUT | Group C with WR# on CH10; trigger CH9 IORQ# falling. | WAIT# falls first. DATA_DIR selects Pico-to-bus, only the AHCT245 OE# falls, D0-D7 contain the expected output byte, and WAIT#/clock release ordering matches the trap protocol. The two OE# signals are never LOW together. |

For the final frequency claim, save Group A, B, C-IN, C-OUT, and D captures at
each tested rate. The separate groups must use the same firmware build, clock
rate, and deterministic stimulus. A 16-channel DSLogic Plus cannot establish
simultaneous whole-address, whole-data, and control ordering in one acquisition;
do not imply otherwise in qualification records.

## Authoritative documents

- [DSLogic Plus Data Sheet](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/datasheets/DSLogic_Plus_Datasheet.pdf)
- [DSView User Guide](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/datasheets/DSView_User_Guide.pdf)
- [DreamSourceLab DSLogic product page](https://www.dreamsourcelab.com/product/dslogic-series/)