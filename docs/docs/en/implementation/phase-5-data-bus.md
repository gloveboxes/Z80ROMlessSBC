# 8.6 Phase 5 - All-PDIP Data Transceivers and Interlock

**Prerequisite:** The [Phase 4 pass gate](phase-4-address-bus.md#pass-gate) must pass.

**Install sequence:** Start with the verified GAL installed and both
data transceivers removed. Perform test 1, power off, then install the
SN74AHCT245N and SN74LVC245AN in that order. Keep Z80 and SRAM removed.
Tie AHCT DIR HIGH and LVC DIR LOW before insertion.

**What you are proving:** bytes can cross the voltage boundary in either
direction, but the two physical drivers can never be enabled together.
AHCT245 sends Pico data to the 5 V bus; LVC245 receives bus data into the
3.3 V domain. Their fixed DIR pins are not the same as the Pico's DATA_DIR
selection signal. Change DATA_DIR only with DATA_ENABLE LOW.

For manual receive tests, power off before fitting test resistors. A test
pattern such as `0xAA` means D7/D5/D3/D1 HIGH and D6/D4/D2/D0 LOW; connect
each bit through its own 1 kOhm resistor to +5 V or GND, never directly.
Use only the specified receive/cycling test with those pulls fitted and
remove them before the next phase. Temporary resistors limit mistakes;
they do not make arbitrary opposing drivers safe.

## Wiring - bidirectional data path

With both transceivers removed, install and continuity-check every D0-D7 tap,
fixed DIR connection, GAL-controlled OE# connection, supply connection, and
local decoupling capacitor below. Repeated D0-D7 labels identify one shared
physical trunk. Verify both driver paths are isolated before inserting the
transceivers in the stated sequence.

{%
  include-markdown "../hardware/bus-isolation.md"
  start='<template id="phase-5-data-wiring">'
  end="phase-5-data-wiring-end</template>"
%}

**Firmware feature:** Add an 8-bit data-bus test using the same
disable-change-enable sequence and fixed, walking-one, and walking-zero
patterns on the Pico data GPIOs.

**Implementation:** [Phase 5 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage05_data_bus/main.c),
using the shared [bus module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/bus.c).

**Test plan:**

1. **GAL installed; data transceivers removed:** With GP6/GP7 LOW,
  verify GAL pins 17 and 18 are both HIGH. Toggle
  GP7 HIGH and GP6 LOW/HIGH; require exactly one GAL OE# output to go
  LOW and verify the other remains HIGH.
2. **GAL and data transceivers installed:** With GP7 LOW, verify GAL
  pins 17/18 and both transceiver OE# pin 19 contacts are HIGH. Confirm
  both A and B buses remain high-impedance and neither supply current
  changes abnormally. This is the end-to-end safety-property check:
  DATA_ENABLE LOW must disable both physical data drivers.
  Verify the AHCT A1-A8 pins remain below 0.8 V while Pico data GPIOs
  are inputs.
3. Select Pico-to-bus direction and test 0x00, 0xFF, 0x55, 0xAA,
  walking-one, and walking-zero with command `d`. Verify levels and bit order.
4. Drive GP7 LOW, change GP6 LOW, and drive GP7 HIGH. Drive each bus input with
  0 V and 5 V through 1 kOhm and verify the Pico reading.
5. Fit temporary 1 kOhm test pulls that present 0xAA on D0-D7, then run
  command `e` for 1,000 disable-change-enable cycles while checking exact
  0xAA readback and supply current. Remove the temporary pulls afterward.

## Pass gate

All eight bits pass both ways, isolation works, and no
direction change causes contention or unexpected current.
