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
  assisted SRAM loading/read-back, Z80 clock and I/O trap. It executes queued
  Z80 supervisor requests and participates in flash quiescence; it never
  accesses USB endpoints.
- **Core 1:** all USB initialization, IRQ/background work, terminal input/output,
  ANSI mapping and supervisor framing, runtime flash/disk writes, ROMLESS-SVC/1 services,
  ENV provisioning, Wi-Fi, HTTP and WebSocket. It services disk work first.
  The `pico_cyw43_arch_lwip_poll` architecture runs network callbacks only
  when core 1 polls, not through core-0 alarm/interrupt callbacks.
- **Startup and USB isolation:** core 1 starts before loading, initializes the
  SDK default alarm pool and USB there, and registers for flash lockout before
  core 0 can recover the journal. Runtime workers remain gated until verified
  CP/M startup. A four-entry supervisor queue and atomic state/cancellation
  publication separate USB handling from Z80 operations. A 4 KiB diagnostic
  ring routes all stdout to core 1, with a 128-byte per-pass drain budget,
  explicit overflow reporting and no USB calls under its lock. USB remains
  available during loading and stop/flush pauses.
- **Shared terminal queues:** 128-byte RX and 512-byte TX fixed-size rings
  shared with Phase 9. One short spinlock critical section protects each
  operation; there are no nested per-byte SDK queue locks. Transport handover
  resets ring indices in constant time. The Z80 trap never calls network code
  or waits for queue space, although cross-core critical sections can delay it.
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

### Module boundaries

- `network_runtime.cpp` owns CYW43 polling, Wi-Fi association/retry, IPv4
  address publication and SNTP. It does not call terminal queues or WebSocket
  code. It disables CYW43 power saving after association; if the driver rejects
  that setting, it drops the link and retries instead of treating the connection
  as online. Address publication is refreshed once per second, not on every loop.
- `web_terminal.cpp` owns HTTP/WebSocket sessions, protocol negotiation,
  heartbeat and terminal flow control. It receives online state and a network
  generation from the application; it does not configure or poll Wi-Fi.
- `network_services.c` schedules these independent network services, weather
  and chat.
  The application still services disk work before network work.
- `chat.c` owns the CHATC11 framed ABI, bounded request/response storage and
  per-request ENV settings on core 1. The existing atomic service mailbox
  remains its only core-0 boundary. `chat_stream.c` decodes bounded HTTP/SSE
  records; `chat_network.cpp` independently owns asynchronous DNS/TCP/TLS,
  backpressure and generation-safe cancellation. Neither touches terminal
  queues, USB input or Z80 GPIO. See
  [CHAT configuration and limits](../system/io-ports.md#chat-application-openai-and-ollama)
  for OpenAI and local Ollama setup. Stage 9 still reports chat UNAVAILABLE.
- `terminal_queue.c` owns transport-neutral console storage and USB/browser
  ownership. USB input, ANSI mapping and supervisor framing run on core 1
  in the shared Stage 9 application; only queued Z80 control requests execute
  on core 0. `usb_console.c` owns USB/alarm initialization and bounded diagnostic
  routing so even lower-level stdout cannot invoke USB on core 0.

The WebSocket submodule contains the transport fixes directly; there is no
generated handler patch. Enqueue success is separate from TCP flushing, with
pending flushes retried from sent/poll callbacks and normal service passes.
Terminal output queues a burst before one flush; interactive/control traffic
still flushes immediately. TCP_NODELAY remains enabled.

Continuing output is coalesced across service passes for up to **1 ms**, without
sleeping or delaying network polls. The first output after an idle interval is
sent immediately; subsequent partial frames wait only until the fixed batching
deadline, while full 512-byte frames bypass that wait. New bytes do not extend
the deadline. A partial frame uses the next free slot in the existing four-frame
array and receives its sequence number only when finalized. Cumulative ACKs
preserve that staging slot; disconnect/handover clears it. There is no extra
output queue or larger Wi-Fi/lwIP pool.
The deadline makes a partial frame eligible on the next service pass; it cannot
guarantee scheduling during flash pauses or other delayed core-1 work.

The four slots now hold **512 bytes each (2 KiB retained payload)**, rather than
256 bytes each. A typical 1,039-byte CLOCK redraw fits in three frames without
waiting for a consumption ACK, instead of at least five. A regression supplies
the redraw in realistic 512 + 512 + 15 byte console arrivals: both full frames
send immediately and the remaining 15 bytes send at the 1 ms deadline.
The console TX ring remains 512 bytes; producer backpressure is unchanged.

The earlier 1 KiB-slot experiment recorded a maximum frame of 512 bytes and
544 retained bytes across CLOCK and games, but all four frame slots were used.
Halving each slot saves **2,048 static RAM bytes** while keeping the four-slot
window. These observations support memory right-sizing, not a claim that
network/browser stalls are resolved.

The existing WebSocket
builder uses a bounded temporary heap buffer for these larger messages:
at most 521 bytes for one 512-byte terminal payload plus the five-byte
application and four-byte WebSocket headers, freed after TCP copy/admission.
Allocation failure follows the transport's explicit fatal-disconnect path.
Four maximum frames need 2,084 TCP bytes, within the unchanged 5,840-byte
send buffer. These are payload/scratch budgets, not total heap usage.
The dedicated core-1 stack remains 8 KiB. Linker success does not measure runtime stack/heap
headroom or establish a hardware latency improvement.

Receive parsing walks pbuf spans once, decodes headers once and unmasks while
copying. A final, unfragmented message moves its payload into the dispatch queue
without a second payload copy. Fragmented messages preserve interleaved controls.
The terminal limits incoming application messages to 69 bytes and queued payload
to 1,024 bytes/16 messages; each service pass dispatches at most eight messages
per connection. Oversize/overflow disconnects rather than silently dropping bytes.
These payload bounds exclude allocator, container and TCP overhead.

Close always enters closing state even if its frame cannot be queued; a
one-second grace deadline bounds cleanup. Invalid/timed-out sessions and Wi-Fi
handover use explicit callback-safe abort, serviced even on the offline path.
TCP polling provides a roughly 500 ms fallback, not a five-second slot leak.

### Acknowledged terminal protocol, version 3

The page and firmware use a small application protocol inside **binary**
WebSocket messages. Generic raw-byte WebSocket terminal clients are no longer
compatible; reload the served page after updating firmware. Version-1 and
version-2 pages are rejected during negotiation, before larger output arrives.

Every message starts with a one-byte type followed by a four-byte,
little-endian unsigned sequence number. Sequence counters wrap modulo
2^32 and restart at 1 for each connection.

| Type | Direction | Sequence and payload |
| ---: | --- | --- |
| 5 HELLO | Pico to browser | Version 3, no payload |
| 6 READY | Browser to Pico | Version 3, no payload |
| 1 OUTPUT | Pico to browser | Output sequence; 1-512 terminal bytes |
| 3 OUTPUT_ACK | Browser to Pico | Last consumed output sequence, no payload |
| 2 INPUT | Browser to Pico | Input sequence; 1-64 terminal bytes |
| 4 INPUT_ACK | Pico to browser | Accepted input sequence, no payload |

HELLO/READY negotiation precedes terminal traffic. The Pico retains at most
**four output frames (2,048 terminal bytes)**, allowing successive frames to
reach xterm without waiting one network round trip per frame. OUTPUT_ACK is
cumulative and is sent **after xterm consumes the bytes**, not on arrival.
The browser queues at most four xterm writes (2 KiB payload). Duplicate output is not rendered
twice; consumed duplicates receive the last cumulative ACK.

Unacknowledged output is retried after 250 ms. TCP write backpressure cannot
allow a later sequence to overtake an unqueued frame, and admission is retried
on the next service pass. A failed flush never changes an accepted frame back
to unqueued: the library retries output without duplicate writes. Only accepted
sequences can be acknowledged. A 15-second timeout on the
oldest retained output frame releases a browser whose JavaScript stopped
consuming data, even if its network stack still answers pings.

Input is acknowledged only when the complete frame fits in the existing
128-byte RX queue. The Pico retains one pending input frame (up to 64 bytes)
when the queue is full and retries admission on every service pass, without
waiting for the browser's 250 ms loss-recovery timer. There remains at most
one unacknowledged input frame. Duplicate accepted input is acknowledged but
never enqueued twice; pending duplicates do not use extra storage.
If TCP rejects an input ACK, the Pico retains it and retries on every service
pass, including ACKs for duplicate input, without waiting for browser
retransmission. Once TCP accepts the ACK, any failed flush is retried by the
transport without re-enqueueing it. Session teardown clears pending ACKs.
No browser-input bytes are evicted to make room. The page queues at most
64 KiB of unconfirmed input, rejects a new
paste in its entirety if that bound would be exceeded, and displays an error.
Disconnect clears pending input with a visible warning; it is never replayed
to a new session. ACK means accepted into the console queue, not that CP/M
has executed a command; handover still deliberately discards stale queued data.

The Z80 trap reports TX-room from the bounded queue without waiting for space,
so a stalled browser eventually applies ordinary console backpressure.
After a timeout, pending browser output is intentionally discarded as part
of transport handover, with a USB timeout notification.

### Terminal performance diagnostics

**Ctrl-] s** on USB includes two `[web]` lines plus `[webbuf]`, `[webbatch]`
and `[consolebuf]`, even while the browser owns CP/M.
Core 1 updates the counters, and the supervisor reads each field atomically;
the status is a per-field snapshot rather than one transactional sample.
Console RX/TX occupancy and peaks are copied together under the existing short
queue lock. Peak updates add only a comparison/store to the existing insertion
critical section, not another lock. No per-frame USB logging or transport work
runs on core 0.

| Field | Meaning |
| --- | --- |
| `frames`, `bytes` | Original output frames and terminal payload bytes accepted by TCP; excludes retransmissions and protocol headers |
| `retransmits` | Application output retries accepted by TCP, not TCP's own packet retransmissions |
| `write_blocks` | Terminal enqueue attempts rejected with temporary TCP backpressure |
| `flush_failures` | Failed explicit terminal burst flushes; excludes automatic library flush attempts |
| `timeouts` | Browser heartbeat/output timeouts that restore USB ownership |
| `window_full_polls` | Output service passes with all four frame slots occupied, whether or not more output is waiting |
| `window_wait_max_us` | Longest continuous interval with all four frame slots occupied |
| `ack_max_us` | Longest first TCP enqueue-to-consumption-ACK interval; cumulative ACKs include the oldest released frame, and retries do not restart it |
| `net_poll_gap_max_us` | Longest interval between core-1 CYW43 poll calls, including time spent in USB, storage, other services or intentional pauses |
| `[webbuf] frames`, `peak` | Current/maximum finalized output frames, including frames blocked before TCP acceptance; capacity four |
| `[webbuf] retained`, `peak` | Current/maximum payload bytes in finalized frames plus staging; capacity 2,048, excluding headers/allocator overhead |
| `[webbuf] staged` | Partial payload awaiting the batching deadline, in the next free slot; capacity 512 |
| `[webbatch] avg_bytes`, `max_bytes` | Integer lifetime `bytes / frames` and largest original TCP-accepted payload; retries excluded |
| `[webbatch] small_le16`, `full` | Original accepted payloads of at most 16 bytes or exactly 512 bytes; retries excluded |
| `[consolebuf] rx`, `tx`, `peak` | Current/capacity and high-water marks for the 128-byte Z80 input and 512-byte Z80 output rings |

Current occupancies clear on handover; peaks survive handover. Web counters
and peaks accumulate across browser sessions until reboot; console peaks reset
when the queues are initialized. Event/byte
counters wrap modulo 2^32 and microsecond maxima saturate at `UINT32_MAX`.
Compare counter deltas over the same workload. `bytes / frames` measures batching
efficiency, not network throughput; the lifetime average is meaningful only
before counter wrap, so use deltas across wrap.
Small frames alone are not a fault: isolated interactive output sends immediately.
Frequent full windows plus large ACK ages suggest consumer/transport pressure;
small frames with low occupancy suggest little data was available for batching.
A high TX peak shows producer pressure, not necessarily sustained congestion.
ACK age includes transport, browser scheduling
and xterm consumption; it is not an isolated browser-render measurement.

Before this coalescing change, a focused-browser CLOCK run on 22 October 2026
measured 19 complete redraws in a 20-second session: median redraw delivery
97.5 ms, maximum 926.2 ms, and an 863.5 ms gap within the slowest redraw. The
median WebSocket payload was seven bytes. Browser consumption was 0.3 ms at
the 95th percentile, and the browser heartbeat gap during the slowest redraw
was at most 21.1 ms. These are **pre-change hardware observations**, not a
post-change speedup claim or proof of a particular TCP/Wi-Fi failure.

Host regressions check the exact 999/1,000 microsecond batching boundary,
immediate isolated/full-frame sends, staging across cumulative ACKs, byte order,
handover cleanup and counter semantics. A synthetic 256-byte stream produced
one byte per 10 microseconds fits into four frames without any ACK, rather than
exhausting the window after four one-byte frames. Repeat CLOCK and active-play
BREAKOUT tests after authorized deployment, excluding launch/game-over/CP/M idle
periods, and compare redraw tails, key-to-paddle response and these counters
before claiming improved hardware latency.

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
    {% include "../../../../src/stage10_websocket_terminal/network_runtime.cpp" %}
    ```

??? example "Web terminal session and flow control"

    ```cpp
    {% include "../../../../src/stage10_websocket_terminal/web_terminal.cpp" %}
    ```

The browser UI follows the updated ESP32 `terminal` page (version 3.2.3):
80 columns by 30 rows, a dedicated scrollbar strip, styled scrollbars, and
horizontal scrolling on narrow screens. The Pico's acknowledged binary
transport, visible input warnings, CP/M keys and Enter button are retained;
the raw-byte ESP32 transport and emulator-only CPU Monitor control are not used.

The browser page and its adapted update tool live in
[Terminal](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/stage10_websocket_terminal/Terminal).
CMake automatically compresses the page when it changes. The server uses the
[gloveboxes/pico-ws-server fork](https://github.com/gloveboxes/pico-ws-server),
pinned to `33bc3c428451094d65b8a0f4d03283840a03e6d9`, which contains the transport
changes described above. It is based on `cadouthat/pico-ws-server` revision
`58f780d4b68c6c592f4d9eb3421cf17c25d3910b`. In the submodule, `origin` points to
the gloveboxes fork and `upstream` retains the original repository. Publish
future submodule commits to the fork before updating the parent gitlink.

## Required Integration Order

Preserve Phase 9 startup: safe GPIOs and console/control queue initialization,
then early core-1 USB/flash-lockout readiness. Core 0 performs journal recovery,
slow load and independent read-back with RESET held, initializes storage/service
queues before enabling their workers, then enables the trap and configured
20 MHz run clock. A boot or core-1 initialization failure leaves RESET asserted.
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

Terminal regressions verify the four-frame output bound, cumulative ACKs and
sequence wrap, ordered recovery from TCP backpressure, burst flushing, and pending
input admission at 10 ms without a 250 ms retransmission. A threaded host test
transfers 256 KiB in each direction through the real console rings and checks
full-queue handover. With `PICO_SDK_PATH` configured, eight WebSocket regression
groups compile the complete actual server with fake lwIP and real Mbed TLS.
They verify four 521-byte frames with one TCP flush, the larger-message heap
packing path and insufficient TCP room, accepted-write/failed-flush
recovery without re-enqueue, single-frame buffer ownership/NUL termination,
split/chained input, fragmentation/control interleaving, malformed frames,
queue byte/count limits, fair bounded dispatch, callback-safe abort, close
deadlines across clock wrap, slot reuse and HTTP backpressure/final ACK lifetime.
Terminal tests also verify the 1,039-byte/three-frame redraw without an ACK,
the 2 KiB window bound, staging/ACK/reset utilization accounting, retry-excluded
frame statistics and console peaks under concurrent transfers and handover.
These tests establish protocol behavior, not physical network throughput.
For performance acceptance, record sustained output bytes/second, key-to-echo
latency and maximum network-poll gaps separately during reads, cached writes
and flash commits; do not infer a disk bottleneck from flash operations alone.

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
