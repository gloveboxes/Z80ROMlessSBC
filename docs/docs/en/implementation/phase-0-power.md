# 8.1 Phase 0 - Empty Sockets and Power Distribution

**Prerequisites:** Review the [inventory](../hardware/inventory.md) and
[construction plan](../hardware/construction.md).

**Install:** Breadboards, sockets, decoupling capacitors, pull-ups,
pull-downs, and power wiring. Install no active device, including the Pico 2 W.
Signal wiring is added and continuity-checked in the phase that first uses
each connection.

**What you are proving:** every future chip will receive the correct supply,
and no wiring fault will short that supply when power is first applied.
There is no firmware to run yet. Read the
[meter guide](../hardware/construction.md#use-the-meter-safely) before testing.

!!! warning "Keep powered and unpowered checks separate"
    Continuity and resistance checks require external power and USB to be
    disconnected. Use DC voltage mode for powered checks. With no Pico fitted,
    the 3.3 V rail must remain unpowered; do not add another supply to test it.

## Wiring - sockets, rails, and passive defaults

- Place and orient every socket using the
  [package-orientation plan](../hardware/construction.md#31-package-orientation-and-pin-1).
- Wire the power rails as shown in the
  [construction plan](../hardware/construction.md):
    - Distribute the regulated +5 V supply to three separate physical +5 V
      rails, one on each Memory, Core, and Peripheral breadboard. The Pico
      provides a separate +3.3 V rail on the Peripheral breadboard; keep it
      separate from all +5 V rails.
    - **Common GND:** Connect Pico GND header pins 3, 8, 13, 18, 23, 28,
      and 38, plus AGND pin 33, to common GND. Also connect Z84C0020PEC
      GND pin 29, AS6C1008-55PCN GND pin 16, MCP23S17-E/SP VSS pin 10,
      ATF22V10B/C GND pin 12, SN74AHCT244N GND pin 10, SN74AHCT245N GND
      pin 10, SN74LVC245AN GND pin 10, and SN74LVC244AN GND pin 10.
    - **Regulated +5 V:** Connect to Z84C0020PEC VCC pin 11,
      AS6C1008-55PCN VCC pin 32, MCP23S17-E/SP VDD pin 9,
      ATF22V10B/C VCC pin 24, and VCC pin 20 on SN74AHCT244N and
      SN74AHCT245N. Do not connect the two LVC buffers to +5 V; they are
      powered from the Pico-derived +3.3 V rail below.
    - **Pico-derived +3.3 V:** Pico 3V3 header pin 36 supplies VCC pin 20
      on SN74LVC245AN and SN74LVC244AN, plus the Pico-side pull-ups listed
      below. No other IC is powered from this rail. Leave any plug-in supply
      module's 3.3 V output disconnected; the Pico is the only 3.3 V source.
- Connect the 1N5819 in series between external +5 V and Pico VSYS header
  pin 39:
    - Unbanded anode to external +5 V.
    - Banded cathode (stripe) to Pico VSYS, header pin 39.
- Never connect external +5 V directly to Pico VSYS or VBUS (header pin 40).
- Do not add point-to-point signal jumpers yet.

## Passive-component installation

#### IC decoupling and board bulk capacitors

- **Local decoupling:** Fit one non-polarized 100 nF ceramic capacitor per DIP
  IC (eight total, excluding the Pico module). Place it close to the supply
  pin, with short connections to that pin and nearby common GND. The pin pairs
  below identify the electrical nets; do not stretch the capacitor across the
  package to reach a distant ground pin. Either capacitor lead can go to GND.

    - Z84C0020PEC: VCC pin 11 to GND pin 29.
    - AS6C1008-55PCN: VCC pin 32 to GND pin 16.
    - MCP23S17-E/SP: VDD pin 9 to VSS pin 10.
    - ATF22V10B/C: VCC pin 24 to GND pin 12.
    - SN74AHCT244N: VCC pin 20 to GND pin 10.
    - SN74AHCT245N: VCC pin 20 to GND pin 10.
    - SN74LVC245AN: VCC pin 20 to GND pin 10.
    - SN74LVC244AN: VCC pin 20 to GND pin 10.

- **Bulk capacitors:**
    - Fit one polarized 22 uF capacitor (at least 10 V) across +5 V and GND
      near the supply feed on each board: Memory, Core, and Peripheral.
    - Fit the polarized 100 uF capacitor (at least 10 V) across +5 V and GND
      at the common supply entry, before power fans out to the boards.
    - Connect all four in parallel: `+` to +5 V and marked `-` to GND. Check
      the case polarity marks, not just lead length, before powering on.
    - These bulk capacitors supplement the eight local 100 nF IC capacitors;
      they do not replace them. No separate bulk capacitor is specified for
      +3.3 V; retain the local 100 nF capacitor at each LVC IC.

### Pull resistors

Fit one resistor per listed signal, between that signal and the specified
rail. A pull-up goes to +5 V or +3.3 V as stated; a pull-down goes to common
GND. Do not join separate signals together. Leave all pull resistors installed
in the completed circuit; active outputs override their weak bias.

#### Z80 control-signal pull-ups

- Fit a 10 kOhm resistor from +5 V to each listed Z84C0020PEC signal net:
    - BUSREQ# (pin 25).
    - BUSACK# (pin 23).
    - MREQ# (pin 19).
    - IORQ# (pin 20).
    - RD# (pin 21).
    - WR# (pin 22).
    - WAIT# (pin 24).
    - INT# (pin 16).
    - NMI# (pin 17).
- Do not add a +5 V pull-up to RESET# (pin 26); it is connected directly to
  Pico GP3 in Phase 1.

#### MCP23S17 SO and SRAM CE#/OE#/WE# control pull-ups

- Fit a 10 kOhm resistor from +5 V to each listed signal:
    - MCP23S17 SO (pin 14).
    - AS6C1008-55PCN SRAM CE# (pin 22).
    - AS6C1008-55PCN SRAM OE# (pin 24).
    - AS6C1008-55PCN SRAM WE# (pin 29).
#### SN74AHCT244 control-input pull-ups

- Fit a 10 kOhm pull-up from +5 V to each SN74AHCT244N input:
    - 2A2 (pin 13).
    - 2A3 (pin 15).
    - 2A4 (pin 17).
- SRAM `CE#`, `OE#`, and `WE#` are active-low: HIGH keeps them inactive.
  The AHCT244 input pull-ups keep its control outputs HIGH when the GAL is
  absent; the pull-ups at the SRAM control pins keep those pins HIGH when the
  AHCT244 is absent. Both sets are required for staged bring-up to prevent
  floating controls from selecting the SRAM, enabling outputs, or starting
  a write. They do not isolate installed but unpowered ICs; follow the
  power safety rules below.

#### SRAM address pull-ups

- Fit one individual 10 kOhm pull-up resistor from +5 V to each
  AS6C1008-55PCN address net (16 resistors total):
    - A0 (pin 12).
    - A1 (pin 11).
    - A2 (pin 10).
    - A3 (pin 9).
    - A4 (pin 8).
    - A5 (pin 7).
    - A6 (pin 6).
    - A7 (pin 5).
    - A8 (pin 27).
    - A9 (pin 26).
    - A10 (pin 23).
    - A11 (pin 25).
    - A12 (pin 4).
    - A13 (pin 28).
    - A14 (pin 3).
    - A15 (pin 31).
#### Pico 2 W control-signal defaults

- Fit 10 kOhm pull-ups from 3.3 V to these Pico 2 W GPIOs:
    - GP4, header pin 6 (BUSREQ#).
    - GP5, header pin 7 (SRAM CE#).
    - GP21, header pin 27 (SPI CS#).
    - GP22, header pin 29 (SRAM WE#).
    - GP26, header pin 31 (SRAM OE#).
- Fit 10 kOhm pull-downs from these GPIOs to GND:
    - GP2, header pin 4 (CLK).
    - GP18, header pin 24 (SPI SCK).
    - GP19, header pin 25 (SPI SI).
- Fit **4.7 kOhm, 5% or better** pull-downs from these GPIOs to GND:
    - GP3, header pin 5 (RESET#).
    - GP6, header pin 9 (DATA_DIR).
    - GP7, header pin 10 (DATA_ENABLE).
    - GP9, header pin 12 (ADDR_ENABLE).
- The supported ATF22V10B can source 100 uA through an input's internal
  pull-up. A 10 kOhm pull-down cannot guarantee its 0.8 V maximum LOW;
  4.7 kOhm provides margin for resistor tolerance and other input leakage.
- Leave GP8, header pin 11, unconnected.

#### SN74AHCT245 and Pico data-bus defaults

- Fit one 10 kOhm pull-down from each Pico data GPIO to GND (eight total):
    - GP10, header pin 14.
    - GP11, header pin 15.
    - GP12, header pin 16.
    - GP13, header pin 17.
    - GP14, header pin 19.
    - GP15, header pin 20.
    - GP16, header pin 21.
    - GP17, header pin 22.
- These pull-downs keep the SN74AHCT245N A inputs defined while the Pico GPIOs
  are inputs or the Pico is absent.

## Power distribution and isolation

These safety rules also apply as devices are installed in later phases.

- On the populated board, **apply external +5 V before connecting USB;
  disconnect USB before removing external +5 V**. Do not operate or program
  the populated board from USB alone. USB can keep Pico data outputs HIGH
  while the SN74AHCT245N's +5 V supply is absent; its data-port clamps can
  then back-power it even with its output enable inactive. The supply diodes
  do not isolate these signal paths. Program a removed Pico
  for USB-only use. Arbitrary external-supply loss while USB remains attached
  is not protected by this design and requires additional hardware isolation.

- Never connect a 5 V output directly to a Pico GPIO. The LVC devices provide
  power-off isolation, GP6/GP7 connect only to biased GAL inputs, and the
  SN74LVC244AN's `Ioff` protection isolates its monitored inputs while its
  3.3 V supply is absent or ramping.

- Power every installed 5 V logic device whenever the 5 V rail is energized.
  Do not apply 5 V with an installed ATF22V10 or SN74AHCT244 unpowered:
  downstream pull-ups could raise an output above the GAL's `VCC + 0.75 V`
  limit or the corresponding logic-family absolute maximum. Absent-device
  pull-up behavior applies only when that device is physically removed from
  its socket. Verify 5 V continuity at every installed IC before power-up; a
  missing VCC socket contact is a fault.

## Test plan

Keep every active device, including the Pico, removed throughout these tests.
Do not test output logic levels at empty sockets; only the fitted passive
connections and supply contacts can be checked in Phase 0.

### Power disconnected

Disconnect external power and USB before resistance or continuity checks.

1. With every IC still removed, verify each socket's occupied rows,
  notch direction, pin-1 corner, and width against the
  [package-orientation plan](../hardware/construction.md#31-package-orientation-and-pin-1).
  Mark pin 1 on the breadboard and socket with a paint pen, and photograph
  the empty-board orientation before wiring over the socket outlines.
2. With power disconnected, check resistance from each supply rail to
  ground. Investigate readings below 1 kOhm after the reading settles as the
  capacitors charge from the meter.
3. Check every fitted rail and passive connection end-to-end. Verify no
  continuity between neighboring socket pins or bus contacts except where
  the passive-component installation explicitly joins them. Confirm the
  +5 V and +3.3 V rails are not directly joined, and check every socket's
  supply and GND contacts against the wiring list above.
4. Measure each pull resistor from its signal contact to its specified rail:
    - Approximately 10 kOhm for every +5 V control pull-up and A0-A15
      address pull-up.
    - Approximately 10 kOhm for the Pico-side pull-ups to the unpowered
      +3.3 V rail and the GP2/GP18/GP19/GP10-GP17 pull-downs to GND.
    - Approximately 4.7 kOhm for GP3/GP6/GP7/GP9 to GND.

    Check WAIT# at the Z80 pin 24 socket contact only. Its wire to GAL pin 20,
    and IORQ# to GAL pin 13, are installed and checked in Phase 2; do not expect
    the WAIT# pull-up to reach GAL pin 20 yet.
5. Check the 1N5819 with the meter's diode-test mode while power is
  disconnected: red probe on the anode/external +5 V side and black probe on
  the banded cathode/Pico VSYS side should show forward conduction; reversing
  the probes should block current. This verifies diode orientation. Do not
  use VSYS voltage to infer a forward-voltage drop in Phase 0: with the Pico
  removed, there is no normal load current through the diode.

### Power applied

Proceed only after all unpowered checks pass. Keep USB disconnected.

- **Supply:** Use the regulated 5 V breadboard supply. Confirm capacitor
  polarity and supply connections before switching it on.
- **Meter:** Select DC voltage mode. Connect its black lead to common GND at
  the supply entry.

An adjustable current-limited supply provides extra protection; if available,
set it to 5 V with a 100 mA limit for this empty-board check. Phase 0 requires
only the multimeter resistance and voltage checks. These checks reduce wiring
risk but do not provide automatic short-circuit protection.

With all ICs removed, the board has no intended steady +5 V load. After the
bulk capacitors charge, check that the +5 V rails reach the specified range
below. If a rail is outside the range, switch off power and inspect the wiring
before continuing.

1. Measure each board's +5 V rail and every +5 V-powered DIP socket supply
  contact listed above. Require 4.75-5.25 V at each supply contact and less
  than 50 mV at each GND contact. Stop and disconnect power if a voltage is
  outside these limits.
2. Confirm the +3.3 V rail and VCC pin 20 contacts on both LVC sockets remain
  at 0 V: the Pico that supplies them is absent. Powered Pico-side levels are
  checked in Phase 1.
3. Confirm external +5 V does not reach Pico VBUS header contact 40 or the
  +3.3 V rail. The unpowered diode test above verifies the isolated VSYS
  connection; the voltage drop is load-dependent and is not a Phase 0 pass
  criterion.
4. Check the +5 V control and address contacts listed under the pull-ups;
  each must read HIGH, close to +5 V, through its resistor.
5. Disconnect power before changing wiring or inserting any device.

### Optional plug-in supply load test

If using a plug-in supply module, test its 5 V output separately with a
suitable electronic load set to 500 mA; no ICs are fitted for this test.
Connect the load with power off. If the source has an adjustable current limit,
set it high enough for this deliberate load. Require 4.75 V to 5.25 V at the farthest board and
check the regulator against its temperature rating. The load dissipates
2.5 W: do not substitute a 1/4 W resistor or use a fingertip as a temperature
probe. Power off and remove the load before connecting the supply to the board.

## Pass gate

- No shorts or crossed nets.
- All fitted pull resistors measure approximately their specified value.
- Every +5 V supply contact measures 4.75-5.25 V; every GND contact is less
  than 50 mV above supply-entry GND.
- The +3.3 V rail, both LVC VCC contacts, and Pico VBUS remain at 0 V.
- 1N5819 passes the unpowered forward/reverse diode test; Pico VBUS and the
  +3.3 V rail remain isolated from external +5 V.
