# Phase 2 - Output Buffer, Fixed Logic, and Clock

**Prerequisite:** [Phase 1](phase-1-supervisor.md#pass-gate).
**Install:** U4 SN74AHCT244N and U3 SN74HCT32N. CPU/SRAM/data translators
remain absent. No device requires programming.

## Wiring - Output Buffer and Fixed Logic

Wire every channel and supply from [the buffer and gate tables](../hardware/output-buffer.md).
Keep U4 pin 18 to Z80 socket pin 6 entirely on Core. Tie both U4 OEs to GND,
unused U4 inputs 15/17 and U3 inputs 12/13 to GND; leave their outputs open.
Check continuity before inserting either device.

{%
  include-markdown "../hardware/output-buffer.md"
  start='<template id="phase-2-output-buffer-wiring">'
  end="phase-2-output-buffer-wiring-end</template>"
%}

## Firmware and Tests

Build `z80_stage02_buffers_clock`; load its UF2 from
`build/src/stage02_buffers_clock/`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/main.c).

`d` holds LOW then HIGH on GP2, GP4, GP5 in order. Probe U4 input/output pairs
2/18, 4/16, 6/14 and their empty destinations. Inputs should be near 0/3.3 V;
outputs near 0/+5 V, LOW below 0.3 V and HIGH at least 4.4 V. The seventh
step restores defaults; `x` restores at any time.

`1`, `2`, `3` select 1 kHz, 100 kHz, 1 MHz. Probe U4 pin 18 and Z80 socket
pin 6: correct frequency, 45-55% duty, no runt pulses or ringing across CPU
thresholds. The clock HIGH must satisfy the Z80 clock's VCC-minus-0.5 V
requirement; a meter does not qualify pulse shape.

With CPU absent, manually drive raw RD#/IORQ# through 1 kOhm to GND, one
signal at a time. Confirm SRAM OE remains HIGH while boot inhibit is HIGH;
confirm WAIT LOW only while IORQ LOW and IO_RELEASE LOW. Test all four input
combinations of each OR gate using isolated, current-limited logic sources;
do not use the walking Stage 1 image after these devices are installed.
Never short an active output to a rail. Restore temporary wiring afterward.

## Pass Gate

All buffer/gate truth tables and clock levels measured correctly. Start later
execution at 1 MHz, not the CPU's 20 MHz rating. This phase qualifies the
unloaded socket clock only; repeat with the CPU fitted.
