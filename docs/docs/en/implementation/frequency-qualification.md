# Frequency Qualification (Optional)

The project breadboard has already demonstrated 20 MHz operation. See the
[20 MHz oscilloscope measurement](../hardware/oscilloscope.md#20-mhz-no-io-clock-measurement)
for the recorded clock evidence and capture procedure. Phases 9 and 10 use
this 20 MHz configuration after SRAM loading and verification.

Additional frequency testing is optional. Complete the normal phase tests;
use this check if you change the wiring, components, or clock configuration,
or want measurements from your own board.

1. Run the [Phase 6 SRAM screening](phase-6-sram.md#pwm-speed-screening-after-the-slow-test)
   or [20 MHz soak test](20mhz-soak.md) appropriate to your setup.
2. Measure the clock using the [oscilloscope guide](../hardware/oscilloscope.md).
3. Repeat the applicable Phase 9/10 boot, disk, and terminal checks. Record
   the measured frequency, firmware revision, and any errors.

A clock capture confirms frequency; functional tests confirm operation under
the tested workload. If a check fails, resolve it before continuing.
