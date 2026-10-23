# Phase 8 - Bidirectional I/O and USB Terminal Qualification

<div data-checklist="phase-8" data-checklist-label="Phase 8" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 8 prerequisite passed"> **Prerequisite:** [Phase 6](phase-6-sram.md#pass-gate), including its [bus-grant and reset/fetch measurements](phase-6-bus-grants.md#pass-gate).
**Install:** No further chips.

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Phase 8 firmware built and loaded"> Build `z80_stage08_virtual_io`; UF2 lives in
`build/src/stage08_virtual_io/`.
[Application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage08_virtual_io/main.c)
and [trap implementation](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c).

## What This Phase Adds

Phase 6 owns comprehensive CPU/arithmetic/stack/SRAM tests. Its soak's result
reports exercise **OUT to port 06h only**; an IN is treated as an error.
Those results do not qualify the bidirectional I/O or terminal service.

This phase retains the existing `z80_stage08_virtual_io` firmware at nominal
**1 MHz**, adding:

- **IN replies:** the Pico drives data to the Z80 during a trapped I/O read.
- **OUT/IN address decoding:** selected raw ports and their masked aliases.
- **USB terminal round-trip:** host input, status polling, Z80 IN, Z80 OUT
  and returned USB characters, with receive/transmit queue and drop counters.
- **Repeated boot/I/O integration:** 100 software reset/load/verify/self-test
  cycles; these are not 100 physical cold power cycles.
- **Analogue timing and fault evidence:** WAIT, stepped release, input data
  setup/hold, both actual data-buffer enables and invalid-control recovery.

The RAM/USB checker uses a single working byte at `0100h` while exchanging
terminal traffic. Its image is loaded and verified across 64 KiB before
execution, but its runtime checker is **not a full-memory sweep** or a
replacement for [Phase 6's SRAM tests](phase-6-sram.md).
Likewise, its one-hour command is an I/O/USB integration run, not another
arithmetic/SRAM qualification. A Phase 6 pass at the scope-measured 20 MHz does not
qualify IN or terminal operation at that frequency.

On boot this firmware starts the RAM/USB checker automatically. Deploy only
after another running diagnostic has finished and its results are saved.

## Synchronous I/O Trap Handler (Phase 8)

Raw IORQ# immediately asserts WAIT through U3 while GP9 IO_RELEASE is LOW.
The buffered falling edge interrupts core 0, which stops PWM, samples the
four port bits, and requires exactly one of RD#/WR# LOW. OUT enables the
downward path; IN preloads a reply and enables the upward path.

Only after the data path is ready does GP9 rise. Firmware completes the I/O
cycle using slow single clocks, waits for IORQ and the active strobe to rise,
disables both paths, waits at least 1 us for isolation, lowers GP9 to rearm
WAIT, then restarts PWM. Each clock step includes at least 1 us of LOW settling
after the falling edge before polling the buffered controls. PWM-to-SIO
handover preloads the LOW latch and extends a frozen HIGH by at least 1 us before
selecting SIO. This guard is not repeated on already-SIO steps;
capture both stopped phases to rule out short pulses. A stuck
strobe times out after 500 ms and causes fail-closed reset/watchdog recovery.
U3 also gates upward OE with RD; no SPI or address-driver operation occurs.

Shared bus access uses one GPIO snapshot for port/data sampling and masked
SIO writes for data preload and direction changes. Data GPIO pads are
initialized at safe startup rather than on every OUT. All isolation,
preload-before-drive and settling guards above remain in force.

### Register access and SRAM-resident hardware path

The trap prepares stopped-SIO clock mode once, then uses a pulse-only helper
for each release clock. Each pulse retains its requested LOW and HIGH waits
and the final 1 us LOW settling interval. General loader/reset callers still
use the self-preparing single-cycle API; the prepared-step API is restricted
to the core-0 bus owner, with no intervening clock ownership or mode change.

Each release check samples IORQ and the active RD/WR strobe together in one
GPIO snapshot. Both must be HIGH before completion, even when they release
on different clocks. Both data-buffer disable bits are set together using
the SDK masked GPIO operation, without touching unrelated outputs. The SDK
can use the RP2350 GPIO coprocessor; raw SIO-register replacements are not
required.

The trap handler/release loop, clock stop/resume/preparation/prepared pulse,
and shared data/port bus helpers use the SDK SRAM code-placement attribute.
This removes instruction fetches from flash for those selected routines,
not for the complete call chain: SDK timing/mux helpers, Phase 8 application
callbacks and failure handling can still execute from flash. Phases 9/10 also
place their terminal/disk callbacks and register handlers in SRAM; see the
[placement checks and limits](phase-9-flash-storage.md#sram-resident-terminal-and-disk-register-path).
**This is not permission
to execute during flash programming**; the existing BUSACK, IRQ and multicore
flash-lockout contracts remain mandatory.

Native tests verify constant preparation work for one to three release
clocks, independently releasing controls, both frozen PWM phases, unchanged
settling intervals, data patterns and fail-closed faults. SDK symbol and
disassembly checks verify SRAM addresses, the pulse-only body and one masked
buffer-isolation operation. These establish work counts, placement and
correctness, not measured throughput or proof that XIP-cache contention caused
a slowdown. Hardware qualification still requires before/after trap-duration
and I/O-throughput measurements, including concurrent network traffic.

Decode is `address & 0x17`: `00/01` terminal and `10-14` disk ports retain
their contracts. A3/A5-A7 and high-byte aliases are intentional. A trap while
BUSACK LOW is ignored and IRQ disabled until the grant-release path rearms it.

The Phase 8 diagnostic result port is `06`, which survives the mask. The
self-test still emits raw ports `00/01/55/AA/FF` to exercise aliases: OUT checks
the masked port and the full transmitted byte; IN expects
`(raw_port & 0x17) ^ 0xA5`. Both successful completion and the RAM-checker's
`E1` error report use `06`; `FE` would decode as `16`, not as `FE`.

Application read/write hooks must not print, sleep, touch networking, wait
for a queue, or initiate another bus operation. Pico queue operations use
bounded spinlock critical sections, not lock-free access.

## USB Diagnostic Commands

<input type="checkbox" data-checklist-id="usb-diagnostics" aria-label="Framed USB diagnostics and serial escape configured and tested"> Ordinary console bytes go to the Z80. Ctrl-] followed by one command byte
selects framed supervisor diagnostics; configure the serial terminal to pass
this escape through. Use `Ctrl-] s` for clock and fault-counter status, and
the command list below for terminal, port and repeated-boot tests.
Do not treat supervisor diagnostics as CP/M input.

| Input | Action in the existing firmware |
| --- | --- |
| Ordinary bytes | Queue terminal input; running checker reads with IN and echoes with OUT |
| `Ctrl-] s` | Print configured clock and boot/verify/RAM/trap/queue counters |
| `Ctrl-] r` | Reload/verify and restart the 1 MHz RAM/USB checker |
| `Ctrl-] p` | Run the selected-port OUT/IN self-test |
| `Ctrl-] b` | Run 100 reset/load/verify/port-test cycles |
| `Ctrl-] h` | Run the one-hour RAM/USB integration check |
| `Ctrl-] x` | Disable trapping, assert reset, isolate data paths and stop the clock |
| `Ctrl-] Ctrl-]` | Queue a literal `1Dh` terminal byte |

The built-in port test selects raw ports `00h/01h/55h/AAh/FFh`; it does not
exhaust every data byte or port alias. Record which patterns were actually
tested. The running checker also emits A-Z heartbeat bytes, so distinguish
those from echoed host input when inspecting the terminal.

## Test Plan

Use [I/O ports and services](../system/io-ports.md) as the allocation
reference. Its extended services are implemented in Phases 9/10, not in this
Phase 8 application, and are not required by these tests.

<input type="checkbox" data-checklist-id="trap-release-captures" aria-label="WAIT clock handover stepped release and isolation captured"> Capture IORQ, WAIT, CLK, RD/WR and both OEs. Confirm WAIT asserts before the
CPU could complete the I/O cycle, and the stepped release isolates before
the next memory fetch. Include both stopped clock phases described above.

<input type="checkbox" data-checklist-id="port-pattern-alias-tests" aria-label="All IN and OUT patterns supported ports and aliases passed"> Check `IN` replies and `OUT` bytes at every bit pattern,
all supported ports, and intentional aliases.

<input type="checkbox" data-checklist-id="sustained-usb-ram-tests" aria-label="Sustained USB and repeated boot verification RAM tests passed"> Run sustained USB echo and
repeated boot/verify/RAM tests; counters must not increase.

<input type="checkbox" data-checklist-id="trap-fault-tests" aria-label="Invalid strobes stuck controls and BUSACK races fail closed"> Use isolated test-only inputs to exercise neither/both strobes asserted,
stuck IORQ/RD/WR, and BUSACK races. Do not short CPU-driven outputs. Faults
must leave RESET LOW, both OEs HIGH, boot inhibit HIGH, and clock stopped
before watchdog reboot.

<input type="checkbox" data-checklist-id="cold-fault-repeats" aria-label="Virtual IO fault tests repeated after cold power cycles"> Repeat after cold power cycles.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-terminal" aria-label="Phase 8 terminal bytes and aliases passed"> Correct terminal bytes and aliases.
- <input type="checkbox" data-checklist-id="gate-ram" aria-label="Phase 8 no RAM corruption"> No RAM corruption.
- <input type="checkbox" data-checklist-id="gate-enables" aria-label="Phase 8 no enable overlap measured"> No enable overlap.
- <input type="checkbox" data-checklist-id="gate-wait" aria-label="Phase 8 bounded WAIT release measured"> Bounded WAIT release.
- <input type="checkbox" data-checklist-id="gate-counters" aria-label="Phase 8 unexpected control timeout and readback counters remain zero"> Zero unexpected control/timeout/readback failures.
- <input type="checkbox" data-checklist-id="gate-evidence" aria-label="Phase 8 analog and digital evidence saved"> Save analog and digital evidence before adding flash/network concurrency.

</div>

## Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Phase 8 application - src/stage08_virtual_io/main.c"

    ```c
    {% include "../../../../src/stage08_virtual_io/main.c" %}
    ```

??? example "Shared synchronous I/O trap - src/common/io_trap.c"

    ```c
    {% include "../../../../src/common/io_trap.c" %}
    ```
