# Phase 3 - Control and Port Monitor

**Prerequisite:** [Phase 2](phase-2-buffer-clock.md#pass-gate).
**Install:** U7 SN74LVC244AN only; Z80/SRAM remain absent.
The historical source-directory name is retained, but no address expander is fitted.

## Wiring - Control and Port Monitor

Wire all eight channels from the
[monitor table](../hardware/address-interface.md#port-address-monitor).
U7 pins 1/10/19 go to GND, pin 20 to Pico-derived 3.3 V, with 100 nF nearby.
No unused monitor channels remain. Never feed 5 V directly into the Pico.

## Firmware and Tests

Build `z80_stage03_control_inputs`; UF2:
`build/src/stage03_mcp23s17/z80_stage03_control_inputs.uf2`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage03_mcp23s17/main.c).

`s` samples BUSACK/IORQ/RD/WR and `port`. With CPU absent and pulls fitted,
controls are HIGH and port is `17`. Pull each input LOW through a temporary
1 kOhm resistor; confirm only the matching GPIO/decoded bit changes and U7
outputs remain in the 3.3 V domain. Never short output pins to a rail.

Pull IORQ LOW and verify WAIT LOW. Type `w` to raise IO_RELEASE: WAIT must
rise without either data OE becoming active. Type `x` to rearm WAIT, then
remove the temporary pull. This tests WAIT independently of data direction.

## Pass Gate

All eight input routes correct; WAIT release works with data paths disabled.
Decoded address bits match `address & 0x17`, not a full 8-bit decode.
