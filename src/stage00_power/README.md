# Stage 0: Power and passive wiring

There is intentionally no Pico firmware for this stage. The Pico and every
other active device remain out of their sockets while the Section 8.1
resistance, continuity, rail-voltage, and diode-OR tests are performed.
The GAL is therefore removed: check only socket continuity and passive
pulls, and do not assign expected logic levels to GAL output pins.

Do not advance to `stage01_supervisor` until every Stage 0 pass gate in the
project specification has passed.