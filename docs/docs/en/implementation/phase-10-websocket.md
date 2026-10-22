# 8.11 Phase 10 - WebSocket Terminal Console

<div data-checklist="phase-10" data-checklist-label="Phase 10" markdown="1">

Tick tests only after recording measured results. Host tests and successful
builds do not qualify the combined networked hardware.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 9 passed"> **Prerequisite:**
the tested [Phase 9 automatic CP/M loader](phase-9-flash-storage.md).
Phase 10 compiles that same application with networking enabled, rather
than maintaining a second loader. It retains the 120 MHz Pico system clock
and nominal **20 MHz Z80 clock** selected for that tested baseline. Networked
operation at that rate still requires the combined acceptance tests below.

**Install/wiring:** Pico 2 W onboard Wi-Fi only. No bus, pin, flash-layout,
disk-geometry or BIOS changes. The existing PCB remains deferred.

## First connection and USB provisioning

1. <input type="checkbox" data-checklist-id="firmware-first-connection" aria-label="Phase 10 built and provisioned">
   Initialize the WebSocket submodule and build:

   ```sh
   git submodule update --init --recursive
   cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
   cmake --build build --target z80_stage10_websocket_terminal -j
   ```

   The UF2 is `build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.uf2`.
   Use the maintained [firmware flashing workflow](../system/firmware-build.md)
   for a firmware-only update that preserves disks and ENV. The build does not
   deploy firmware.
2. Open the USB terminal. CP/M loads, verifies and starts without waiting for
   USB, Wi-Fi or a browser. Missing/invalid settings trigger a USB setup
   prompt once CP/M is running. **Ctrl-] n** starts setup again at any time
   while running, including after failed association.
3. Enter SSID, password, and the **remote filesystem server's IPv4 address**.
   Password input is not echoed. An empty password selects an open network;
   WPA2 passwords require 8-63 bytes or a 64-digit hexadecimal key.
   ESC cancels before saving. SSIDs are limited to 32 bytes.
4. Settings use the existing durable ENV store as `WIFI_SSID`,
   `WIFI_PASSWORD`, and `FS_SERVER`. They are not compiled into firmware.
   Core 1 saves each key through the existing ENV parser/snapshot backend,
   preserves unrelated keys, and reports failure explicitly. Each key save is
   atomic; the three-key setup is not one transaction. After a failed save,
   retry all settings with Ctrl-] n. ENV is plaintext onboard flash, not
   encrypted secret storage.
5. <input type="checkbox" data-checklist-id="conditional-network-diagnosis" aria-label="Network address verified">
   Wait for `[network] Terminal: http://<pico-ip>:8088/` on USB and open that
   URL. HTTP and WebSocket share port 8088 and the root path `/`. Association
   is asynchronous, times out after 30 seconds, and retries after 5 seconds.
   The page targets WebSocket port 8088 even when opened locally or served
   elsewhere; `?altair=<pico-ip>` overrides the host, not the port.
6. <input type="checkbox" data-checklist-id="conditional-boot-diagnosis" aria-label="Boot and network failures distinguished">
   If the page works but CP/M does not respond, use **Ctrl-] s** on USB to
   inspect boot evidence, disk errors, browser ownership and queue drops.
   The network line also reports the Pico's current IPv4 address, for example
   `[network] browser=0 setup=0 ip=192.168.1.123`; open
   `http://192.168.1.123:8088/`. It reports `ip=unavailable` before an address
   is assigned or after Wi-Fi disconnects. The address is refreshed if DHCP
   changes it without a link disconnect. This is the Pico's address, not
   the stored remote filesystem server address.
   A web page alone does not prove Z80 operation. A new browser does not
   receive an old prompt; press Enter to obtain a fresh CP/M prompt.
7. <input type="checkbox" data-checklist-id="functional-tests-trusted-network" aria-label="Trusted local network selected">
   Use a trusted local network only. HTTP/WebSocket has no authentication or
   TLS; do not expose port 8088 to the Internet. The copied xterm.js page
   loads its JavaScript/CSS dependencies from CDNs, so the browser needs
   Internet access on first use.

`FS_SERVER` is collected and stored **only**. This phase adds no remote-FS
client, socket, service, or I/O-port controller. Direct ENV edits are read on
the next reboot or USB setup; USB setup applies immediately.

## Core ownership and terminal routing

**Maintained application:**
[Phase 9 main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/main.c),
compiled by [Phase 10 CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/CMakeLists.txt)
with `Z80_STAGE09_AUTOBOOT=1` and `Z80_STAGE10_NETWORK=1`.

- **Core 0:** safe GPIO startup, journal recovery, boot CRC validation,
  assisted SRAM loading/read-back, Z80 clock and I/O trap, USB terminal and
  framed supervisor commands. All initialization queues precede core-1 launch.
- **Core 1:** runtime flash/disk writes, existing ROMLESS-SVC/1 services,
  ENV provisioning, Wi-Fi, HTTP and WebSocket. It services disk work first.
  The `pico_cyw43_arch_lwip_poll` architecture runs network callbacks only
  when core 1 polls, not through core-0 alarm/interrupt callbacks.
- **Shared terminal queues:** the same 128-byte RX and 512-byte TX Pico SDK
  queues used by Phase 9. The Z80 trap never calls network code or waits for
  queue space. Short spinlock critical sections protect transport handover.
- **USB fallback:** USB owns CP/M until a browser WebSocket connects. While
  connected, that browser exclusively owns CP/M input/output; ordinary USB
  input is ignored, but Ctrl-] supervisor commands and setup still work.
  Additional browser sessions are rejected. Browser disconnect or Wi-Fi
  loss returns CP/M to USB without stopping the Z80. Ping/pong checks run
  every 5 seconds; a missing valid pong for 15 seconds releases a vanished
  browser even when the Pico's own Wi-Fi stays up.
- **Handover:** queued input/output is cleared when switching transports,
  so stale keystrokes and terminal output cannot leak to the new owner.
  Without either transport, output is discarded and transmit-ready stays set.
  Intentional handover/disconnected discards do not count as queue overflow.
  Wi-Fi association/reconfiguration events do not clear USB-owned queues.
  Actual RX/TX queue overflows are counted and reported. WebSocket output
  is retained until acknowledged rather than discarded on a temporary send
  failure.

Port `0x00` remains terminal data. Port `0x01` bit 0 means RX-ready,
bit 1 TX-room (or disconnected), and bit 7 any active terminal transport.
Disk ports, service ports, SNTP/calendar support, and the Phase 9 safe
stop/flush protocol remain unchanged.

### Acknowledged terminal protocol, version 1

The page and firmware use a small application protocol inside **binary**
WebSocket messages. Generic raw-byte WebSocket terminal clients are no longer
compatible; reload the served page after updating firmware. The upstream
WebSocket submodule is unchanged.

Every message starts with a one-byte type followed by a four-byte,
little-endian unsigned sequence number. Sequence counters wrap modulo
2^32 and restart at 1 for each connection.

| Type | Direction | Sequence and payload |
| ---: | --- | --- |
| 5 HELLO | Pico to browser | Version 1, no payload |
| 6 READY | Browser to Pico | Version 1, no payload |
| 1 OUTPUT | Pico to browser | Output sequence; 1-256 terminal bytes |
| 3 OUTPUT_ACK | Browser to Pico | Last consumed output sequence, no payload |
| 2 INPUT | Browser to Pico | Input sequence; 1-64 terminal bytes |
| 4 INPUT_ACK | Pico to browser | Accepted input sequence, no payload |

HELLO/READY negotiation precedes terminal traffic. Each direction has at most
one unacknowledged data frame. The Pico retains one output frame and retries
after 250 ms until the browser acknowledges it **after xterm consumes the
bytes**. Duplicate output is acknowledged without rendering twice. This is
safe even when `tcp_write` accepted a frame but a subsequent flush reported
failure. A 15-second output-ACK timeout also releases a browser whose JavaScript
stopped consuming data, even if its network stack still answers pings.

Input is acknowledged only when the complete frame fits in the existing
128-byte RX queue; otherwise it is retried after 250 ms. Duplicate input is
acknowledged but never enqueued twice. No browser-input bytes are evicted to
make room. The page queues at most 64 KiB of unconfirmed input, rejects a new
paste in its entirety if that bound would be exceeded, and displays an error.
Disconnect clears pending input with a visible warning; it is never replayed
to a new session. ACK means accepted into the console queue, not that CP/M
has executed a command; handover still deliberately discards stale queued data.

The Z80 trap remains nonblocking and reports TX-room from the bounded queue,
so a stalled browser eventually applies ordinary console backpressure.
After a timeout, pending browser output is intentionally discarded as part
of transport handover, with a USB timeout notification.

### Maintained source

??? example "Automatic loader shared with Phase 9"

    ```c
    {% include "../../../../src/stage09_flash_storage/main.c" %}
    ```

??? example "Shared terminal queues and transport handover"

    ```c
    {% include "../../../../src/common/terminal_queue.c" %}
    ```

??? example "Core-1 network lifecycle"

    ```cpp
    {% include "../../../../src/common/terminal_network.cpp" %}
    ```

The browser UI follows the updated ESP32 `terminal` page (version 3.2.3):
80 columns by 30 rows, a dedicated scrollbar strip, styled scrollbars, and
horizontal scrolling on narrow screens. The Pico's acknowledged binary
transport, visible input warnings, CP/M keys and Enter button are retained;
the raw-byte ESP32 transport and emulator-only CPU Monitor control are not used.

The browser page and its adapted update tool live in
[Terminal](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/stage10_websocket_terminal/Terminal).
CMake automatically compresses the page when it changes. The server is the
`cadouthat/pico-ws-server` Git submodule, pinned to the latest upstream HEAD
verified when this phase was rebuilt: `58f780d4b68c6c592f4d9eb3421cf17c25d3910b`.

## Required Integration Order

Preserve Phase 9 startup: safe GPIOs, queue initialization, slow load and
independent read-back with RESET held, then core-1 launch, trap enable and
the configured 20 MHz run clock. A boot failure leaves RESET asserted.
Core 0 continuously services flash ownership requests. Stop drains pending
disk/service/provisioning work before holding RESET. Reboot reloads CP/M;
the former Phase 10 variable-clock and RAM qualification controls are not
part of this replacement application. Use the dedicated qualification builds.

## Network time and CLOCK services

Phase 10's existing SNTP client supplies the shared
[CLOCK time and timer services](../system/io-ports.md#clock-application-time-compatibility).
The dcc CLOCK app requests local time/date and decimal uptime through
ROMLESS-SVC/1 ports 2/3/4, and uses independent timer 2 on port 23 for its
50 ms refresh. These requests do not require a browser connection.

After Wi-Fi connects, the Pico synchronizes with `pool.ntp.org`; until then,
calendar queries report `CLOCK_UNSYNCED`, not a fabricated date.
Use the persistent ENV `TZ` key for local time, for example `ENV TZ=AEST-10`;
missing `TZ` selects UTC. Changes apply on the next request without rebooting.
The ESP32 app's `UTC_OFFSET` instructions do not apply to this TZ-only firmware.
The unchanged CLOCK app's weather queries are supported by the cache described
below.

## OpenWeatherMap weather

Configure `OWM_KEY`, `OWM_LOCATION` (for example `Sydney,AU`) and optionally
`OWM_UNITS` in the existing ENV store; metric is the default. See the
[weather service contract](../system/io-ports.md#openweathermap-weather)
for exact commands, field IDs, units, refresh intervals and limits.
The API key is not compiled into firmware or printed in network diagnostics.

Weather uses asynchronous certificate-verified HTTPS to
`api.openweathermap.org` on core 1, only after Wi-Fi and SNTP are ready.
It shares neither the Z80 trap nor a blocking socket with console traffic.
Current conditions and one forecast block are cached for CLOCK's frequent
status/field queries. Settings changes and disconnects cancel in-flight work;
HTTP, TLS and configuration errors are exposed through the weather status
and error field. Missing configuration does not stop CP/M or the terminal.

TLS uses an explicit 8 KiB main-RAM core-1 stack, rather than enlarging the
Pico's 4 KiB scratch-RAM stack region. Phase 9 keeps its tested launch path.

## Test plan

1. <input type="checkbox" data-checklist-id="no-client-test" aria-label="USB fallback and headless boot tested">
   Cold boot with no browser and use USB CP/M. Repeat with no USB and no
   Wi-Fi. Confirm BIOS/CCP evidence and no fatal error after reconnecting USB.
2. <input type="checkbox" data-checklist-id="client-status-test" aria-label="Exclusive browser routing tested">
   Connect a browser; run `DIR` and confirm CP/M output is not duplicated on
   USB. USB Ctrl-] s must still work. Reject a second browser without
   disturbing the first.
3. <input type="checkbox" data-checklist-id="tx-order-overflow" aria-label="Ordered output and bounded queues tested">
   Exercise sustained output and paste bursts. Inspect Ctrl-] s for zero
   RX/TX drops and trap faults. Pause acknowledgements and verify output
   retries do not duplicate terminal text. Paste more than 128 bytes and
   verify ordering while CP/M drains its console input.
4. <input type="checkbox" data-checklist-id="rx-ready-controls" aria-label="CP/M keys and status tested">
   Verify Ctrl+C, Ctrl+Z, Ctrl+M/Enter, ESC, arrows, Insert/Delete and Page
   Up/Down. Ctrl+M is carriage return, not the emulator's monitor command.
5. <input type="checkbox" data-checklist-id="cpm-storage-acceptance" aria-label="Combined terminal and disk operations tested">
   Run the [Phase 9 sentinel, A-D, warm-boot and cross-drive
   checks](phase-9-flash-storage.md#pass-gate) while using the browser.
6. <input type="checkbox" data-checklist-id="reconnect-races" aria-label="USB fallback and Wi-Fi recovery tested">
   Close the browser, then drop Wi-Fi during output/disk writes. Verify
   USB fallback, continued Z80 operation and reconnect, without stale input
   or output replay. Suspend the browser computer while leaving the Pico's
   Wi-Fi up; USB must regain ownership after the 15-second liveness timeout.
   With no browser, change network settings/drop Wi-Fi and verify queued
   USB input/output is preserved. Re-enter settings and reboot to verify persistence.
7. Verify invalid/overlong settings are rejected, password is never echoed,
   and failed ENV writes are reported without claiming success.
8. Configure weather ENV settings, wait for Wi-Fi/SNTP, and run the unchanged
   CLOCK app. Verify current conditions, units and near-term forecast while
   browser input and disk operations remain responsive. Disconnect Wi-Fi:
   cached data must remain readable with increasing age. Test a missing or
   invalid API key and inspect the explicit error/status without exposing
   credentials. Restore settings and verify recovery without a reboot.

Native tests cover all 15 weather fields through the real port parser and
service worker, cache/retry/configuration behavior, malformed HTTP/JSON,
TLS/time gating, stale DNS callbacks, cancellation, timeouts and bounded
response handling. These are not a substitute for the live weather and
combined-load hardware checks above.

With `PICO_SDK_PATH` supplied to the native test configuration,
`weather_real_tls_config` also builds the SDK's actual Mbed TLS library with
the firmware configuration and parses/verifies the bundled CA. This catches
missing signature algorithms that fake TLS transport tests cannot detect.

## Pass gate

- <input type="checkbox" data-checklist-id="gate-responsive" aria-label="Combined Phase 10 operation qualified">
  Browser terminal remains responsive at the baseline's configured clock.
- <input type="checkbox" data-checklist-id="gate-rx-ready" aria-label="Readable terminal status">
  Terminal status corresponds to queued bytes and the selected transport.
- <input type="checkbox" data-checklist-id="gate-cpm" aria-label="CP/M acceptance passed">
  Interactive commands, warm boot and A-D copies pass.
- <input type="checkbox" data-checklist-id="gate-core-ownership" aria-label="Core ownership preserved">
  No HTTP, WebSocket or Wi-Fi code runs on the Z80 core.
- <input type="checkbox" data-checklist-id="gate-counters" aria-label="No unexplained faults">
  No unexplained queue drops, trap faults or storage errors.
- <input type="checkbox" data-checklist-id="gate-storage-final" aria-label="Storage acceptance complete">
  Complete the outstanding [Phase 9 final acceptance](phase-9-flash-storage.md#pass-gate).

</div>
