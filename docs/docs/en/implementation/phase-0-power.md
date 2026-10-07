# 8.1 Phase 0 - Empty Sockets and Power Distribution

**Prerequisites:** Review the [inventory](../hardware/inventory.md) and
[construction plan](../hardware/construction.md). Install no active device in
this phase.

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
      on SN74LVC245AN and SN74LVC244AN. It also feeds the 10 kOhm pull-ups
      on GP4, GP5, GP21, GP22, and GP26 listed below. The two LVC ICs each
      have a local 100 nF decoupling capacitor from VCC to GND. These are
      the external loads on the 3.3 V rail; no other IC is powered from it.
- Connect the 1N5819 in series between external +5 V and Pico VSYS header
  pin 39:
    - Unbanded anode to external +5 V.
    - Banded cathode (stripe) to Pico VSYS, header pin 39.
- Fit every passive component specified below.
- Connect pull-ups from each signal to the positive rail specified for its
  signal group; that rail is not always 3.3 V.
- Connect pull-downs from each signal to GND.
- Do not add point-to-point signal jumpers yet.

### Passive-component installation

#### IC decoupling and board bulk capacitors

- **Local decoupling:** Fit one 100 nF capacitor directly between the supply
  and ground pins of each DIP IC. Keep its leads as short as practical and
  place it close to the IC:
  
    - Z84C0020PEC: VCC pin 11 to GND pin 29.
    - AS6C1008-55PCN: VCC pin 32 to GND pin 16.
    - MCP23S17-E/SP: VDD pin 9 to VSS pin 10.
    - ATF22V10B/C: VCC pin 24 to GND pin 12.
    - SN74AHCT244N: VCC pin 20 to GND pin 10.
    - SN74AHCT245N: VCC pin 20 to GND pin 10.
    - SN74LVC245AN: VCC pin 20 to GND pin 10.
    - SN74LVC244AN: VCC pin 20 to GND pin 10.

- **Pico 2 W:** The eight capacitors above are for the eight DIP ICs; the
  Pico module is not part of this count.

- **Ceramic capacitors:** A ceramic 100 nF capacitor is non-polarized; either
  lead can connect to GND.

- **Electrolytic bulk capacitors:** These are polarized. Connect `+` to the
  positive rail and the marked negative lead to GND. Check the case markings,
  not just lead length.

- **Bulk capacitors:**
    - Fit one polarized 22 uF capacitor (at least 10 V) across +5 V and GND
      near the supply feed on each board: Memory, Core, and Peripheral.
    - Fit the polarized 100 uF capacitor (at least 10 V) across +5 V and GND
      at the common supply entry, before power fans out to the boards.
    - Connect all four in parallel: `+` to +5 V and marked `-` to GND. Check
      the case polarity marks before powering on.
    - These bulk capacitors supplement the eight local 100 nF IC capacitors;
      they do not replace them.

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
  Pico GP3.

#### MCP23S17 SO and SRAM CE#/OE#/WE# control pull-ups

- Fit a 10 kOhm resistor from +5 V to each listed signal:
    - MCP23S17 SO (pin 14).
    - AS6C1008-55PCN SRAM CE# (pin 22).
    - AS6C1008-55PCN SRAM OE# (pin 24).
    - AS6C1008-55PCN SRAM WE# (pin 29).
- These are not Z80 pins.

#### SN74AHCT244 control-input and SRAM address defaults

- Connect SN74AHCT244 inputs to +5 V through pull-up resistors:
    - 2A2 (pin 13).
    - 2A3 (pin 15).
    - 2A4 (pin 17).
- These input pull-ups keep the SRAM-control outputs inactive if the GAL is
  absent.
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
- Connect each resistor between +5 V and its address signal only; do not
  connect the address signals together.
- SRAM `CE#`, `OE#`, and `WE#` are active-low. These pull-ups are required for
  the documented staged configurations: if the GAL is absent, the AHCT244
  input pull-ups hold its control outputs HIGH; if the AHCT244 is absent, the
  pull-ups directly on the SRAM control pins hold them HIGH. This prevents an
  undriven control from selecting the SRAM, enabling its outputs, or starting
  a write.
- Leave these pull-ups installed in the completed circuit. They provide safe
  default levels when a source is absent or not driving the control signal;
  an active output overrides the weak pull-up during normal operation. They do
  not isolate an installed but unpowered GAL or AHCT244. Keep installed 5 V
  logic powered whenever +5 V is applied, as described in the power warning
  below.

#### Pico 2 W control-signal defaults

- Fit 10 kOhm pull-ups from 3.3 V to these Pico 2 W GPIOs:
    - GP4 (BUSREQ#).
    - GP5 (SRAM CE#).
    - GP21 (SPI CS#).
    - GP22 (SRAM WE#).
    - GP26 (SRAM OE#).
- Fit 10 kOhm pull-downs from these GPIOs to GND:
    - GP2 (CLK).
    - GP18 (SPI SCK).
    - GP19 (SPI SI).
- Fit **4.7 kOhm, 5% or better** pull-downs from these GPIOs to GND:
    - GP3 (RESET#).
    - GP6 (DATA_DIR).
    - GP7 (DATA_ENABLE).
    - GP9 (ADDR_ENABLE).
- The supported ATF22V10B can source 100 uA through an input's internal
  pull-up. A 10 kOhm pull-down cannot guarantee its 0.8 V maximum LOW;
  4.7 kOhm provides margin for resistor tolerance and other input leakage.
- Leave GP8 unconnected.

#### SN74AHCT245 and Pico data-bus defaults

- Fit one individual 10 kOhm resistor from each Pico GPIO GP10-GP17 to GND
  (eight resistors total). Connect each resistor between its GPIO signal and
  common GND; do not connect the GPIO signals together.
- These pull-downs keep the SN74AHCT245N A inputs defined while the Pico GPIOs
  are inputs or the Pico is absent.
- Each active HIGH GPIO sources only 0.33 mA through its own pull-down.

#### Pico 2 W startup behavior

- Once the Pico 3.3 V rail is valid, the external resistors establish safe
  levels before firmware configures SIO.
- During a cold power ramp, Pico-side pull-ups cannot hold active-low
  controls HIGH while the 3.3 V rail is still at 0 V.
- RESET# therefore remains asserted, and SRAM contents remain indeterminate
  until the boot image is loaded and verified.

### Power distribution and isolation

- Feed the breadboard's +5 V logic rail directly from the regulated supply.
  Feed Pico VSYS from that rail only through the 1N5819, with its anode toward
  external +5 V and banded cathode toward VSYS. The Pico's internal Schottky
  diode and the 1N5819 OR USB and external power at VSYS, but do not isolate
  every signal pin between power domains. Never connect the external +5 V
  rail directly to Pico VBUS or VSYS.

- On the populated board, **apply external +5 V before connecting USB;
  disconnect USB before removing external +5 V**. Do not operate or program
  the populated board from USB alone. USB can keep Pico data outputs HIGH
  while U9's +5 V supply is absent; the AHCT245 data-port clamps can then
  back-power U9 even with its output enable inactive. Program a removed Pico
  for USB-only use. Arbitrary external-supply loss while USB remains attached
  is not protected by this design and requires additional hardware isolation.

- Power the SN74LVC245AN and SN74LVC244AN from the Pico 3.3 V rail. Power the
  SN74AHCT245N and all other logic from the regulated 5 V rail. Tie Pico AGND
  pin 33 to common digital ground; this design needs no separate analogue
  ground plane.

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

- If using the plug-in supply module, leave its 3.3 V output disconnected;
  the Pico must remain the only 3.3 V source. Follow the
  [construction plan](../hardware/construction.md#31-package-orientation-and-pin-1)
  for its placement and polarity.

**Implementation:** [Phase 0 power checklist](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage00_power/README.md).

**Test plan:**

1. With every IC still removed, verify each socket's occupied rows,
  notch direction, pin-1 corner, and width against the
  [package-orientation plan](../hardware/construction.md#31-package-orientation-and-pin-1).
  Mark
  pin 1 on the breadboard and socket with a paint pen, and photograph
  the empty-board orientation before wiring over the socket outlines.
2. With power disconnected, check resistance from each supply rail to
  ground. Investigate readings below 1 kOhm after capacitors charge.
3. Check every fitted rail and passive connection end-to-end. Verify no
  continuity between neighboring socket pins or bus contacts except where
  the passive-component installation explicitly joins them. At the empty GAL socket,
  verify each fitted passive has no unintended short to GND, 5 V, or an
  adjacent pin. Do not attempt to verify GAL output levels while it is
  removed.
4. Apply 5 V and measure every 5 V-powered DIP socket supply pin.
  Require 4.75 V to 5.25 V at VCC and less than 50 mV at each ground
  pin. The AHCT245 and GAL VCC socket contacts must read 5 V, while the
  LVC245 and LVC244 VCC contacts must remain at 0 V because their 3.3 V
  source, the absent Pico, is not yet installed. With the GAL removed,
  do not test GAL output levels. The unpowered continuity checks above must
  already have passed; do not use continuity mode with the rail energized.
  GAL output-level verification is performed in
  [Phase 2](phase-2-buffer-clock.md) after the GAL is installed.
5. If using the photographed plug-in supply, confirm that its body
  obscures no more than Core Board rows 1-3. Test its 5 V output separately
  with a suitable electronic load set to 500 mA; no ICs are fitted for this
  test. Connect the load with power off, then set the supply current limit
  high enough for this deliberate load. Require 4.75 V to 5.25 V at the
  farthest board and check the regulator against its temperature rating.
  The load dissipates 2.5 W: do not substitute a 1/4 W resistor or use a
  fingertip as a temperature probe. Power off, remove the load, and restore
  the 100 mA first-power-up limit afterward. Leave the module's 3.3 V output
  disconnected.
6. Verify the external +5 V rail reaches Pico VSYS only through the
  1N5819 and does not reach Pico VBUS, the 3.3 V rail, or any GPIO
  contact. With external power applied, VSYS must be one Schottky drop
  below the +5 V rail. Verify each 5 V-side active-low control is pulled
  HIGH. With power removed, measure approximately 10 kOhm from every
  Pico-side pull-up contact to the unpowered 3.3 V rail. Require approximately
  4.7 kOhm from GP3/GP6/GP7/GP9 to GND, and 10 kOhm from the other
  pull-down contacts to GND, as listed in the passive-component installation;
  powered Pico-side logic levels are checked in
  [Phase 1](phase-1-supervisor.md). Measure approximately 10 kOhm from each
  GP10-GP17 contact to GND through its individual pull-down resistor.
  Also measure approximately 10 kOhm from each A0-A15 address contact to
  +5 V through its individual pull-up resistor.
  Specifically require approximately 10 kOhm from the Z80 WAIT# pin 24 and
  GAL pin 20 contacts to +5 V. Their point-to-point connection, and the
  IORQ# connection to GAL pin 13, are installed and checked in Phase 2.

## Pass gate

- No shorts or crossed nets.
- Correct supply voltage at every socket.
- Negligible current with all devices removed.
