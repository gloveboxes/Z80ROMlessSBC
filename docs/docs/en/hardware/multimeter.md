# Multimeter — Essential

A good-quality digital multimeter is essential for building this board. It is
used throughout the [implementation phases](../implementation/index.md) to
check wiring, power rails, pull resistors, and static signal levels. An
oscilloscope and logic analyser are optional diagnostic tools.

## Useful features

- Reliable continuity indication for checking wires and socket contacts.
- Resistance measurement for pull resistors and unexpected shorts.
- DC voltage measurement with clear resolution around 3.3 V and 5 V.
- Fine, insulated probe tips for reaching IC contacts without bridging pins.

## Use during construction

1. **Disconnect external power and USB** before continuity or resistance tests.
2. Check each connection at the actual socket contacts, then check isolation
   from adjacent pins. Tick the implementation checklist only after checking.
3. Before fitting ICs, follow the phase instructions to verify supply polarity,
   rail voltages, grounds, and fixed logic levels.
4. Repeat the specified voltage checks as devices and wiring are added.

Keep the black lead in **COM** and the red lead in the **voltage/resistance**
socket for these checks. Never put a meter in current mode across a power rail.
A running clock may display an average voltage; use the phase’s static test
mode for meter checks, or a scope to inspect the waveform.

See [breadboard and meter basics](construction.md#before-you-wire) for handling
and probing guidance. The implementation pages remain the step-by-step build
instructions and define the expected readings.
