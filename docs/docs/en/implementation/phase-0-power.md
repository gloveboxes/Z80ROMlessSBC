# 8.1 Phase 0 - Empty Sockets and Power Distribution

**Prerequisites:** Review the [inventory](../hardware/inventory.md) and
[construction plan](../hardware/construction.md). Install no active device in
this phase.

**Install:** Breadboards, sockets, decoupling capacitors, pull-ups,
pull-downs, and power wiring. Install no active device, including the Pico 2 W.
Signal wiring is added and continuity-checked in the phase that first uses
each connection.

## Wiring - sockets, rails, and passive defaults

Place and orient every socket using the
[package-orientation plan](../hardware/construction.md#31-package-orientation-and-pin-1).
Wire the common ground, regulated +5 V, and Pico-derived 3.3 V rails exactly
as shown in the [construction plan](../hardware/construction.md), including
the 1N5819 between external +5 V and Pico VSYS. Fit every passive component
specified below. Do not add point-to-point signal jumpers yet.

### Passive-component installation

- **Local decoupling:** Place each 100 nF capacitor directly across its IC
  supply pins with the shortest practical leads. If scope captures show more
  than 250 mV rail excursion at the farthest board, add 47-100 uF there and
  repeat the capture. Bulk capacitance does not replace local 100 nF
  capacitors.

- **5 V pull-ups:** Fit 10 kOhm pull-ups to BUSREQ#, BUSACK#, MREQ#, IORQ#,
  RD#, WR#, MCP SO, SRAM CE#, SRAM OE#, SRAM WE#, WAIT#, INT#, and NMI#.
  RESET# is the direct GP3 node and must not have a 5 V pull-up.

- **GAL and SRAM defaults:** Pull SN74AHCT244 inputs 2A2, 2A3, and 2A4 up to
  5 V so its SRAM-control outputs remain inactive if the GAL is absent. Fit
  10 kOhm pull-ups on A0-A15 using two 9-pin bussed SIP networks. The
  AHCT244-input pull-ups cover an absent GAL, and the final SRAM-node pull-ups
  cover an absent AHCT244. They do not provide power-off isolation for an
  installed source IC.

- **Pico-side control defaults:** Fit 10 kOhm pull-ups to 3.3 V on GP4
  (BUSREQ#), GP5 (SRAM CE#), GP21 (SPI CS#), GP22 (SRAM WE#), and GP26
  (SRAM OE#). Fit 10 kOhm pull-downs to GND on GP2 (CLK), GP3 (RESET#), GP6
  (DATA_DIR), GP7 (DATA_ENABLE), GP9 (ADDR_ENABLE), and GP18/GP19 (SPI
  SCK/SI). Leave GP8 unconnected.

- **GAL control inputs:** Connect GP7 and GP6 directly to ATF22V10 pins 9 and
  11. Its TTL-compatible inputs accept 3.3 V.

- **Pico data-bus defaults:** Fit the third 8x10 kOhm bussed SIP network from
  GP10-GP17 to GND. This keeps the SN74AHCT245N A inputs defined while the Pico
  GPIOs are inputs or the Pico is absent, and loads each active HIGH by only
  0.33 mA at 3.3 V.

- **Startup behavior:** Once the Pico 3.3 V rail is valid, the external
  resistors establish safe levels before firmware configures SIO. During a
  cold power ramp, Pico-side pull-ups cannot hold active-low controls HIGH
  while the 3.3 V rail is still at 0 V. RESET# therefore remains asserted,
  and SRAM contents remain indeterminate until the boot image is loaded and
  verified.

- **Unused inputs:** Tie every unused CMOS input to a defined level.

### Power distribution and isolation

- Feed the breadboard's +5 V logic rail directly from the regulated supply.
  Feed Pico VSYS from that rail only through the 1N5819, with its anode toward
  external +5 V and banded cathode toward VSYS. The Pico's internal Schottky
  diode and the 1N5819 safely OR USB and external power. Never connect the
  external +5 V rail directly to Pico VBUS or VSYS.

- Power the SN74LVC245AN and SN74LVC244AN from the Pico 3.3 V rail. Power the
  SN74AHCT245N and all other logic from the regulated 5 V rail. Tie Pico AGND
  pin 33 to common digital ground; this design needs no separate analogue
  ground plane.

- Never connect a 5 V output directly to a Pico GPIO. The LVC devices provide
  power-off isolation, the GAL inputs do not source 5 V into GP6/GP7, and the
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
  do not test GAL output levels. Verify the GAL socket and associated
  nets have no unintended continuity to GND, 5 V, or adjacent signals.
  GAL output-level verification is performed in
  [Phase 2](phase-2-buffer-clock.md) after the GAL is installed.
5. If using the photographed plug-in supply, confirm that its body
  obscures no more than Core Board rows 1-3. Load its 5 V output to at
  least 500 mA, require 4.75 V to 5.25 V at the farthest board, and
  confirm no regulator becomes too hot to touch. Leave its 3.3 V output
  disconnected.
6. Verify the external +5 V rail reaches Pico VSYS only through the
  1N5819 and does not reach Pico VBUS, the 3.3 V rail, or any GPIO
  contact. With external power applied, VSYS must be one Schottky drop
  below the +5 V rail. Verify each 5 V-side active-low control is pulled
  HIGH. With power removed, measure approximately 10 kOhm from every
  Pico-side pull-up contact to the unpowered 3.3 V rail and from every
  pull-down contact to GND, as listed in the passive-component installation;
  powered Pico-side logic levels are checked in
  [Phase 1](phase-1-supervisor.md). Measure approximately 10 kOhm
  from each GP10-GP17 contact to GND through the data SIP network.
  Specifically require approximately 10 kOhm from the Z80 WAIT# pin 24 and
  GAL pin 20 contacts to +5 V. Their point-to-point connection, and the
  IORQ# connection to GAL pin 13, are installed and checked in Phase 2.

## Pass gate

No shorts or crossed nets, correct supply voltage at
every socket, and negligible current with all devices removed.
