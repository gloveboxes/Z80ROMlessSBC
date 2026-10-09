# 8.11 Phase 10 - WebSocket Terminal Console

**Prerequisite:** The [Phase 9 bring-up checkpoint](phase-9-flash-storage.md#bring-up-checkpoint)
must pass. Use this terminal to finish the interactive and network-dependent
parts of the Phase 9 storage acceptance tests.

**Install:** No further bus hardware. Use a Pico 2 W for the final
networked terminal build. Wi-Fi/WebSocket is the required final user
terminal; the [Phase 8 USB CDC transport](phase-8-virtual-io.md) remains
available for bring-up and diagnostics but is not the target operating
interface. A non-W Pico
2 may compile the same hooks as stubs only for host-side development.

**Wiring:** Make no bus-hardware changes. Use the Pico 2 W's onboard radio;
do not add a separate network module or repurpose the reserved wireless
GPIOs.

**What you are proving:** the working computer remains reliable while Wi-Fi,
terminal traffic, and disk writes run together. Keep USB connected for Pico
diagnostics; type CP/M commands in the browser, not into the USB diagnostic
console. In this stage USB `s` reports status directly, without Phase 8's
Ctrl-] prefix.

### First connection and fault isolation

1. Build with the intended Wi-Fi credentials and load the Stage 10 firmware.
  Keep the clock at 1 MHz. Find the Pico's DHCP address in the network log
  or your router's client list, then open `http://<pico-ip>:8088/` from a
  computer on the same reachable local network.
2. If the page does not load, check association, the address, and client
  isolation/firewall settings before changing breadboard wiring. Guest
  networks may block clients from communicating with each other.
3. If the page loads but no CP/M output appears, inspect USB `s` for client,
  disk/fatal, and queue-drop status. A served web page proves the Pico's
  network path, not the Z80 boot or disk path.
4. Once CP/M responds, run the functional tests below before trying a higher
  clock. Keep this unauthenticated HTTP/WebSocket terminal on a trusted
  local network; do not expose port 8088 to the Internet.

**Firmware feature:** Start the WebSocket console service on core 1
after core 0 has completed safe GPIO startup, queue initialization, and
the [Phase 9 boot-image load](phase-9-flash-storage.md) (which finishes
entirely on core 0 before
core 1 is launched). Core 0 continues to own the Z80 clock, bus
transceivers, injected SRAM loading, and I/O trap. Core 1 owns Wi-Fi
connection management, the embedded HTTP terminal page, WebSocket
client state, network polling, and the
[flash disk-write service](../system/operation.md#63-onboard-flash-cpm-disk-storage)
-- all in the same `core1_main()` task, since
`multicore_launch_core1()` only accepts one entry point. The two cores
exchange terminal bytes and immutable disk-write requests with nonwaiting
`queue_try_*` operations, following the `pico-altair-8800` console bridge
pattern. During a physical flash commit, core 1 deliberately waits on a
separate request/result queue while core 0 performs the bounded
BUSREQ#/BUSACK# ownership transfer; that rendezvous never runs in the Z80
trap.

**Application source:** [src/stage10_websocket_terminal/main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/main.c),
with the shared [terminal bridge](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_bridge.c),
[network service](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_network.cpp), and
[browser terminal](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/terminal.html).

## WebSocket Terminal I/O Bridge (Final Phase 10 Integration)

**Maintained source:** [terminal.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/terminal.h),
[terminal_bridge.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_bridge.c), and
[terminal_network.cpp](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_network.cpp).

The terminal bridge follows the `pico-altair-8800` model: load the boot image
from flash through the
[Section 6.3 path](../system/operation.md#63-onboard-flash-cpm-disk-storage)
entirely on core 0, initialize the terminal queues and timers, then launch
core 1 to own Wi-Fi and WebSocket work and
the flash disk-write service in the same task. The
maintained network service supplies the HTTP/WebSocket implementation;
builders do not need to choose or integrate another server library. Only the
queue functions are visible to the Z80 trap.

### Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Stage 10 application - src/stage10_websocket_terminal/main.c"

    ```c
    {% include "../../../../src/stage10_websocket_terminal/main.c" %}
    ```

??? example "Terminal queues and polling - src/common/terminal_bridge.c"

    ```c
    {% include "../../../../src/common/terminal_bridge.c" %}
    ```

??? example "Wi-Fi and HTTP/WebSocket service - src/common/terminal_network.cpp"

    ```cpp
    {% include "../../../../src/common/terminal_network.cpp" %}
    ```

`z80_terminal_core1_service()` calls the maintained network lifecycle and
services the timer-driven input/output polls. The network implementation
initializes CYW43, associates asynchronously, retries with backoff after
failure or link loss, and disables power-saving after association.
The Stage 10 core-1 loop also calls `z80_flash_core1_service()`, so disk
service continues with Wi-Fi absent or reconnecting. Core 0's nonblocking
USB command loop continuously services `z80_flash_core0_service()`; network
code never takes ownership of the bus GPIOs.

## Required Integration Order

**Maintained source:** [Stage 10 main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/main.c)
and [Stage 10 CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/CMakeLists.txt).

The command-loop application must call `z80_safe_startup()` as
its first GPIO action. The [Phase 3](phase-3-address-generator.md) input buffer
is always enabled; no expander initialization is required. Keep trapping disabled
during injected loading and single-step operation. After storage and terminal
initialization, launch core 1, enable the I/O trap, and call
`z80_cpu_release_reset_and_run(1000000)`. That helper resets the CPU with slow
clocks, lowers the SRAM-read inhibit, releases RESET# while CLK is stopped LOW,
waits at least 1 us, and starts PWM. Check each initialization result and fail
closed on error. To reload a running CPU, first quiesce the core-1 disk service
as the qualification helpers do, then disable trapping and call
`z80_cpu_prepare_loader()`: isolate, assert reset, clock reset, then release
into the injection sequence with SRAM reads inhibited. After verification,
reset to PC zero and lower the inhibit for SRAM execution. Do not use a bus
grant to load memory: the Pico has no address/write bus master. Drain
`z80_flash_core0_service()` continuously from core 0's nonblocking
foreground loop. For acquisition, leave trapping enabled while
asserting BUSREQ# and waiting for BUSACK# LOW, then disable it. For
release, enable trapping while BUSACK# is still LOW, then deassert
BUSREQ# and wait for BUSACK# HIGH. Core 0 must call and check
`flash_safe_execute_core_init()` before launching core 1 and must not
use the multicore FIFO for anything else, because the lockout handler
owns it. Journal recovery is the only core-0 flash write and occurs
before core 1 launch; all runtime writes execute on core 1 while the
Z80 is held in BUSACK#.

**Test plan:**

1. Boot with no browser connected. Verify the Z80 still runs the
  [Phase 8 I/O tests](phase-8-virtual-io.md#pass-gate), `IN 0x01` reports no
  client, and terminal output does not
  accumulate without bound.
2. Connect a browser to `http://<pico-ip>:8088/`, or a WebSocket client
  to `ws://<pico-ip>:8088/`. Verify `IN 0x01` sets the client-connected
  bit without disturbing the Z80 clock.
3. Run a Z80 program that writes a continuous alphabet pattern to
  `OUT 0x00`. Verify the browser receives the stream in order and that
  queue-full conditions are counted rather than blocking the trap.
4. Type from the browser and verify the Z80 receives each byte through
  `IN 0x00` only after `IN 0x01` reports data available. Test single
  characters, pasted bursts, delayed characters, an empty queue, and queue
  overflow; RX_READY must never be asserted unless an immediate data read
  returns a real queued byte.
  Verify Ctrl+C sends `0x03` and Ctrl+Z sends `0x1A`, not printable letters.
5. At the CP/M prompt, run `DIR`, `LS`, switch through B-D, and repeat the
  [Phase 9 sentinel and cross-drive checks](phase-9-flash-storage.md#pass-gate).
  This proves the terminal and disk
  port ranges remain independently routed in the final combined firmware.
6. Disconnect and reconnect the browser while the Z80 test program runs.
  Verify stale input is cleared, output resumes for the new client, and
  no trap timeout counter increments.
  Also test a disconnect/reconnect between network polls: the new client
  must remain connected and its first input must survive queue cleanup.
7. Exhaust the default alarm pool before service startup and require the
  supervisor to remain fail-closed rather than launching core 1 without
  both WebSocket polling timers.

Before using the qualification controls, issue a CP/M disk flush and wait
for READY. The final firmware then quiesces the core-1 disk service before
any CPU ownership or clock change. USB diagnostic `+` and `-` change the
requested clock by 500 kHz under BUSREQ#/BUSACK#; actual rates follow the
[qualification table](frequency-qualification.md). `a` loads a CPU-read-only bus pattern
covering 0000/FFFF/5555/AAAA plus walking-one/walking-zero addresses;
`t` starts the self-checking RAM/continuous-terminal image; and `h` runs that
image with an automatic one-hour result. These diagnostic images replace the
running CP/M image, so reboot before returning to CP/M filesystem tests.
Both one-hour tests require ongoing RAM-loop terminal writes and fail after
five seconds without a heartbeat, even if all error counters remain zero.

## Pass gate

The WebSocket service remains responsive while the Z80
runs at the [Phase 8 qualified 1 MHz setting](phase-8-virtual-io.md#pass-gate);
terminal status never claims
data that cannot be read; interactive CP/M commands, warm boot, distinct
A-D directories, and cross-drive copies still pass in the final combined
firmware; no network path runs on the core that services Z80 timing; and all
terminal queue overflow or client disconnect conditions are visible through
counters rather than blocking the CPU trap.
