# Phase 8 - Integrated Boot and Virtual I/O

**Prerequisite:** [Phase 7](phase-7-z80.md#pass-gate).
**Install:** No further chips. Build `z80_stage08_virtual_io`; UF2 lives in
`build/src/stage08_virtual_io/`.
[Application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage08_virtual_io/main.c)
and [trap implementation](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c).

## Synchronous I/O Trap Handler (Phase 8)

Raw IORQ# immediately asserts WAIT through U3 while GP9 IO_RELEASE is LOW.
The buffered falling edge interrupts core 0, which stops PWM, samples the
four port bits, and requires exactly one of RD#/WR# LOW. OUT enables the
downward path; IN preloads a reply and enables the upward path.

Only after the data path is ready does GP9 rise. Firmware completes the I/O
cycle using slow single clocks, waits for IORQ and the active strobe to rise,
disables both paths, lowers GP9 to rearm WAIT, then restarts PWM. A stuck
strobe times out after 500 ms and causes fail-closed reset/watchdog recovery.
U3 also gates upward OE with RD; no SPI or address-driver operation occurs.

Decode is `address & 0x17`: `00/01` terminal and `10-14` disk ports retain
their contracts. A3/A5-A7 and high-byte aliases are intentional. A trap while
BUSACK LOW is ignored and IRQ disabled until the grant-release path rearms it.

Application read/write hooks must not print, sleep, touch networking, wait
for a queue, or initiate another bus operation. Pico queue operations use
bounded spinlock critical sections, not lock-free access.

## USB Diagnostic Commands

Ordinary console bytes go to the Z80. Ctrl-] followed by one command byte
selects framed supervisor diagnostics; configure the serial terminal to pass
this escape through. Use `Ctrl-] s` for clock and fault-counter status, and
the application's command list for echo, port, RAM, reboot, and rate tests.
Do not treat supervisor diagnostics as CP/M input.

## Test Plan

Capture IORQ, WAIT, CLK, RD/WR and both OEs. Confirm WAIT asserts before the
CPU could complete the I/O cycle, and the stepped release isolates before
the next memory fetch. Check `IN` replies and `OUT` bytes at every bit pattern,
all supported ports, and intentional aliases. Run sustained USB echo and
repeated boot/verify/RAM tests; counters must not increase.

Use isolated test-only inputs to exercise neither/both strobes asserted,
stuck IORQ/RD/WR, and BUSACK races. Do not short CPU-driven outputs. Faults
must leave RESET LOW, both OEs HIGH, boot inhibit HIGH, and clock stopped
before watchdog reboot. Repeat after cold power cycles.

## Pass Gate

Correct terminal bytes and aliases, no RAM corruption, no enable overlap,
bounded WAIT release, and zero unexpected control/timeout/readback failures.
Save analog and digital evidence before adding flash/network concurrency.
