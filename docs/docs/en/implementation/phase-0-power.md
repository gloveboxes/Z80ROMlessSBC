# 8.1 Phase 0 - Empty Sockets and Power Distribution

**Prerequisites:** Review the [inventory](../hardware/inventory.md) and
[construction plan](../hardware/construction.md). Install no active device in
this phase.

**Install:** Breadboards, sockets, decoupling capacitors, pull-ups,
pull-downs, and power wiring. Install no active device, including the Pico 2.
Signal wiring is added and continuity-checked in the phase that first uses
each connection.

## Wiring - sockets, rails, and passive defaults

Place and orient every socket using the
[package-orientation plan](../hardware/construction.md#31-package-orientation-and-pin-1).
Wire the common ground, regulated +5 V, and Pico-derived 3.3 V rails exactly
as shown in the [construction plan](../hardware/construction.md), including
the 1N5819 between external +5 V and Pico VSYS. Fit every decoupling and bulk
capacitor and every pull-up, pull-down, and SIP network in the
[passive-component plan](../hardware/inventory.md#03-capacitors-and-resistors).
Do not add point-to-point signal jumpers yet.

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
  the passive-component plan explicitly joins them. At the empty GAL socket,
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
  pull-down contact to GND, as listed in the
  [passive-component plan](../hardware/inventory.md#03-capacitors-and-resistors);
  powered Pico-side logic levels are checked in
  [Phase 1](phase-1-supervisor.md). Measure approximately 10 kOhm
  from each GP10-GP17 contact to GND through the data SIP network.
  Specifically require approximately 10 kOhm from the Z80 WAIT# pin 24 and
  GAL pin 20 contacts to +5 V. Their point-to-point connection, and the
  IORQ# connection to GAL pin 13, are installed and checked in Phase 2.

## Pass gate

No shorts or crossed nets, correct supply voltage at
every socket, and negligible current with all devices removed.
