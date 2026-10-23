# 8.11 Phase 10 - WebSocket Terminal Console

<div data-checklist="phase-10" data-checklist-label="Phase 10" markdown="1">

**Goal:** use CP/M in a web browser over the Pico 2 W's Wi-Fi. USB remains the fallback terminal.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

- <input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 9 passed"> **Start here:** complete the [Phase 9 automatic CP/M loader](phase-9-flash-storage.md).
- **Unchanged:** 120 MHz Pico clock, nominal 20 MHz Z80 clock, BIOS and disk layout.
- **Hardware:** onboard Wi-Fi plus the [3.3 V matrix display](#ht16k33-8x8-matrix). PCB changes remain deferred.

## First connection and USB provisioning

### 1. Deploy from VS Code

!!! warning "Before any update"
    Save CP/M work. Apply external **+5 V before USB** and keep it applied.
    Never power the populated board from USB alone. Disk updates overwrite
    selected drives **without backups**.

Open the Command Palette, run **Tasks: Run Task**, then choose:

| Update | Exact task name | What changes |
| --- | --- | --- |
| Firmware | **Flash Pico - Stage 10** | Builds and flashes firmware; preserves disks and ENV settings |
| All disks | **Flash Pico - All disks (A-D)** | Replaces A-D; preserves firmware and ENV |
| CP/M system disk | **Flash Pico - Drive A** | Replaces A with a regenerated native image; checks boot compatibility |
| BDS C disk | **Flash Pico - Drive B** | Replaces B only |
| Escape disk | **Flash Pico - Drive C** | Replaces C only |
| Blank disk | **Flash Pico - Drive D** | Replaces D only |

- Firmware confirmation: **Proceed**. Disk confirmation: **Replace selected disks**. Both default to **Cancel**.
- Tasks use the existing `.venv-flash` environment, stop/flush safely, verify written bytes and request reboot.
- Disk tasks do not rebuild firmware or applications. Install updated applications into the source disk first.
- Setup/details: [firmware build and provisioning](../system/firmware-build.md) and [VS Code disk tasks](../tools/pico-tools.md#vs-code-disk-flashing-tasks).

<input type="checkbox" data-checklist-id="firmware-first-connection" aria-label="Phase 10 built and provisioned"> Stage 10 firmware deployed.

??? example "First checkout or manual build"

    Initialize the WebSocket submodule before building or running the firmware task:

    ```sh
    git submodule update --init --recursive
    cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
    cmake --build build --target z80_stage10_websocket_terminal -j
    ```

    Output: `build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.uf2`.
    These commands build only; they do not deploy. Flash verification proves
    written bytes, not a successful CP/M boot.

### 2. Set Wi-Fi over USB

1. Open the USB terminal. CP/M starts without waiting for Wi-Fi or a browser.
2. Press **Ctrl-] c** to open configuration. Missing/invalid Wi-Fi credentials open it automatically.
3. Select **1 - Wi-Fi**, then **C**. Enter your SSID and password.
4. Press **Q** to return to the main menu, then **Q** again to exit.

**Saved changes trigger a safe reboot.** No changes means no reboot.
A storage error cancels reboot and is reported. Save application work first:
flushing disks cannot save unsaved application RAM.

### 3. Open the browser terminal

1. Press **Ctrl-] s** on USB to display the Pico's address.
2. Open **`http://<pico-ip>:8088/`**.
3. Press **Enter** for a fresh CP/M prompt, then try `DIR`.

- <input type="checkbox" data-checklist-id="conditional-network-diagnosis" aria-label="Network address verified"> Pico address verified and terminal page opened.
- <input type="checkbox" data-checklist-id="functional-tests-trusted-network" aria-label="Trusted local network selected"> Use a **trusted local network only**. The terminal has no authentication or TLS; never expose port 8088 to the Internet.
- The browser needs Internet access on first use to load xterm.js dependencies.
- Reload the terminal page after firmware updates.

## Unified system configuration

**Ctrl-] c** opens setup over USB, even while a browser owns the terminal.
It saves the same [environment variables](#system-environment-variables) that
you can edit from CP/M with **`C:ENV`**.

| Menu | Use it for |
| --- | --- |
| **1 - Wi-Fi** | **C** sets SSID/password; **U** clears them |
| **2 - Remote filesystem** | Stores `FS_SERVER` only; remote file access is not implemented or required for Wi-Fi |
| **3 - Chat** | **1** OpenAI, **2** compatible endpoint, **3** Ollama; **4-6** model, token limit and temperature; **7** thinking |
| **4 - OpenWeatherMap** | **C** sets API key, location and units |
| **5 - Timezone** | **C** sets POSIX `TZ`; for example, `AEST-10` means UTC+10 |

| Key | Action |
| --- | --- |
| **Enter** | Keep the existing value |
| **-** | Clear the value; clearing the Wi-Fi password selects an open network |
| **ESC** | Cancel the current unsaved edit |
| **S** | Show settings, with secrets masked |
| **Q** | Return to the main menu; at the main menu, exit |

**Settings persist in plaintext onboard flash, not encrypted storage.**
Passwords and API keys are hidden during entry and masked on display.

??? info "Validation, saved changes and service settings"

    - **Wi-Fi:** SSID up to 32 bytes; WPA2 password 8-63 bytes or a 64-digit hexadecimal key.
    - **Saving:** each key is atomic, but a multi-field edit is not. After a failed save, reopen setup and retry affected fields. Unrelated keys are preserved.
    - **Disconnect:** unsaved edits are discarded; saved edits still require reboot when exiting setup. Viewing settings does not count as a change.
    - **ENV:** values are stored literally. Network ENV edits apply on reboot or setup reload; saved Wi-Fi edits can reconnect immediately.
    - **Chat:** custom/Ollama endpoints need scheme, host and path, for example `http://192.168.1.26:11434/v1/chat/completions`. Only OpenAI receives `CHAT_OPENAI_KEY`; custom-provider authentication is unsupported. Model/options are consumed by the CP/M application. See [CHAT configuration](../system/io-ports.md#chat-application-openai-and-ollama).
    - **Weather:** location can be `Sydney,AU` or `lat=-33.8&lon=151.2`; units are `metric`, `imperial` or `standard`.
    - **Timezone:** empty means UTC; `AEST-10AEDT,M10.1.0,M4.1.0/3` includes daylight saving. Use `TZ`, not `UTC_OFFSET`.

## Troubleshooting

| Symptom | Check or action |
| --- | --- |
| No Pico address | **Ctrl-] s** shows `ip=unavailable`; check Wi-Fi settings. Association times out after 30 seconds and retries after 5 seconds. |
| Page opens, but no prompt | Press **Enter**. Old output is not replayed to a new browser. |
| CP/M still does not respond | Use **Ctrl-] s** to inspect boot evidence, disk errors, queue drops and browser ownership. A working web page does not prove Z80 operation. |
| USB ignores normal typing | A browser owns CP/M. Close it to return to USB; USB supervisor commands still work. |
| Second browser cannot connect | Only one browser may own the terminal. |
| Browser disappears | USB ownership returns on disconnect/Wi-Fi loss, or after a 15-second liveness timeout. The Z80 keeps running. |
| Wrong address | Use the Pico's address, not `FS_SERVER`. HTTP/WebSocket use port 8088; `?altair=<pico-ip>` changes the host only. |
| Configuration save fails | Reopen **Ctrl-] c**, inspect saved values and retry affected fields. |

<input type="checkbox" data-checklist-id="conditional-boot-diagnosis" aria-label="Boot and network failures distinguished"> Confirm boot and network faults separately.

**Ctrl-] s** also shows uptime, reset causes and matrix status without injecting
periodic diagnostics into CP/M output.

## HT16K33 8x8 matrix

**Expected display:** one horizontal row moves every 500 ms. It is a diagnostic,
not an IP-address or uptime display.

| Backpack marking | Pico connection | J2 pin |
| --- | --- | --- |
| `+` / VCC | 3V3, physical pin 36 | 4 |
| `-` / GND | Common GND | 5 |
| `C` / SCL | GP26, physical pin 31 | 3 |
| `D` / SDA | GP22, physical pin 29 | 2 |
| Vi2c / IO, if fitted | 3V3 | 4 |

- **Power and both pull-ups must be 3.3 V, never 5 V.** Disconnect power safely before rewiring.
- Follow the printed markings, not an assumed connector order. The external module is schematic **A2**, with no PCB footprint.
- Keep other devices off this diagnostic bus. The driver scans `0x70`-`0x77`; the tested board uses `0x72`.
- Check **Ctrl-] s** for address, state and errors. The module shares the Pico's 3.3 V budget and runs at reduced brightness ([wiring guide](https://learn.adafruit.com/led-backpack-displays-on-raspberry-pi-and-beaglebone-black/wiring)).

??? info "Display implementation and host test"

    [matrix_display.cpp](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/matrix_display.cpp)
    runs on core 1 after network work. Each pass performs at most one non-blocking
    FIFO operation; RAM updates use two eight-byte chunks.

    New transactions require a running CPU and no flash pause. If permission
    changes, finish the transaction through STOP before holding the pattern.
    A 5 ms FIFO timeout or transfer NACK disables the display, releases resources
    and records the error. No Z80 bus GPIO or new core-0 handshake is used.

    [matrix_i2c.pio](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/matrix_i2c.pio)
    uses open-drain pins at nominal 100 kHz without clock stretching.

    ```sh
    cmake --build build/host-control --target matrix_display_test -j
    ctest --test-dir build/host-control --output-on-failure -R '^matrix_pio_HT16K33$'
    ```

    The moving row was bench-tested with CP/M and networking. Host tests do not
    qualify physical waveform margins or core-0 latency.

## Network time and CLOCK services

- Wi-Fi enables automatic time sync with `pool.ntp.org`; until then, calendar queries return `CLOCK_UNSYNCED`.
- Set `TZ` to `AEST-10` for UTC+10 without daylight saving. Use the USB menu or [C:ENV literal insert](#update-from-cpm). Direct ENV changes apply on the next request; empty means UTC.
- Look up [time-zone abbreviations and UTC offsets on Wikipedia](https://en.wikipedia.org/wiki/List_of_time_zone_abbreviations), or use this [city-to-POSIX TZ lookup](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv) for complete rules. Enter the **POSIX string**, not the city name; its offset sign is reversed (`-10` means UTC+10).

<span id="openweathermap-weather"></span>

## System environment variables

**One settings store, two ways to edit it:**

- **USB configuration:** press **Ctrl-] c** and choose a menu. It validates and saves the ENV values below.
- **CP/M command line:** run **`C:ENV`** to list settings, or **`C:ENV NAME=VALUE`** to update one.

| Used by | ENV variables | Purpose / values |
| --- | --- | --- |
| Wi-Fi | `WIFI_SSID`, `WIFI_PASSWORD` | Network name and password; empty password selects an open network |
| Remote filesystem | `FS_SERVER` | Server IPv4 address; stored only, remote file access is not implemented |
| Chat connection | `CHAT_PROVIDER`, `CHAT_ENDPOINT`, `CHAT_OPENAI_KEY` | `openai`, `compatible` or `ollama`; full URL for custom/local providers; API key for OpenAI only |
| Chat options | `CHAT_MODEL`, `CHAT_MAX_TOKENS`, `CHAT_TEMPERATURE` | Model name, positive token limit and temperature `0`-`2` |
| Chat thinking | `CHAT_THINK` | Chat menu **7**: `false` (off), `true` (on/medium) or `default` (server choice); CHAT 1.7-c11 compatible/Ollama requests only |
| Weather | `OWM_KEY`, `OWM_LOCATION`, `OWM_UNITS` | API key, city/country or coordinates, and `metric` (default), `imperial` or `standard` |
| Local time | `TZ` | [POSIX timezone string](#network-time-and-clock-services); empty means UTC |

### Update from CP/M

```text
C:ENV -H
C:ENV OWM_UNITS
C:ENV OWM_UNITS=metric
```

These show help, read one value and update it. **ENV listings may reveal
passwords and API keys.** Both interfaces save to plaintext onboard flash.

**Values containing `+` or `-`:** prefer the USB menu, which saves literal
text. The ENV app can interpret ordinary assignments as arithmetic. Use
`C:ENV -I NAME=VALUE` for a literal insert; if the key already exists, delete
it first with `C:ENV -D NAME`. This matters for keys, model names, coordinates
and timezone rules.

### When changes take effect

- **USB menu:** exiting after saved changes safely reboots; save CP/M work first.
- **C:ENV Wi-Fi settings:** reboot or reload the configuration shell.
- **C:ENV timezone/weather:** timezone applies on the next request; weather reloads within one second, without reboot.
- **C:ENV chat settings:** provider/endpoint/key and CHAT 1.7-c11 thinking settings apply on the next request/turn; restart CHAT after changing model, token limit or temperature.

Weather needs Wi-Fi and synchronized time; missing settings do not stop CP/M.
For service-specific setup and errors, see [chat](../system/io-ports.md#chat-application-openai-and-ollama),
[weather](../system/io-ports.md#openweathermap-weather) and the
[ENV command reference](../system/io-ports.md#persistent-environment-api).

## Test plan

**Tick only after recording hardware results.** A successful build or host test is not hardware qualification.

- <input type="checkbox" data-checklist-id="no-client-test" aria-label="USB fallback and headless boot tested"> **Boot:** test USB-only, then no USB/no Wi-Fi. Reconnect USB; confirm BIOS/CCP evidence and no fatal error.
- <input type="checkbox" data-checklist-id="client-status-test" aria-label="Exclusive browser routing tested"> **Ownership:** run browser `DIR`; no duplicate USB output. USB **Ctrl-] s** still works; a second browser is rejected.
- <input type="checkbox" data-checklist-id="tx-order-overflow" aria-label="Ordered output and bounded queues tested"> **Traffic:** sustain output and paste over 128 bytes. Check order, zero drops/faults and no duplicate output when ACKs pause.
- <input type="checkbox" data-checklist-id="rx-ready-controls" aria-label="CP/M keys and status tested"> **Keys:** test Ctrl+C, Ctrl+Z, Ctrl+M/Enter, ESC, arrows, Insert/Delete and Page Up/Down. Ctrl+M is carriage return.
- <input type="checkbox" data-checklist-id="cpm-storage-acceptance" aria-label="Combined terminal and disk operations tested"> **Disks:** repeat [Phase 9 sentinel, A-D, warm-boot and copy tests](phase-9-flash-storage.md#pass-gate) through the browser.
- <input type="checkbox" data-checklist-id="reconnect-races" aria-label="USB fallback and Wi-Fi recovery tested"> **Recovery:** disconnect browser/Wi-Fi during output and writes; verify USB fallback and no stale replay. Suspend the browser computer; verify fallback after 15 seconds. Without a browser, Wi-Fi changes must preserve USB queues.
- **Settings:** reboot and verify persistence. Reject invalid/overlong values, hide secrets and report failed saves.
- **Weather:** run CLOCK with Wi-Fi/time ready; verify conditions, units and forecast while typing and using disks. Drop Wi-Fi: cached values remain readable with increasing age. Test invalid/missing keys and recovery without reboot.
- **Performance:** compare CLOCK redraws and active-play BREAKOUT response. Record bytes/second, key-to-echo latency and maximum network-poll gaps during reads, cached writes and flash commits.

??? info "What host tests establish"

    - Console tests cover concurrent 256 KiB transfers, handover and queue bounds.
    - Terminal tests cover ordering, duplicate suppression, sequence wrap, backpressure, ACK retries, 1 ms batching and the four-frame window.
    - With `PICO_SDK_PATH`, WebSocket tests use the actual server with fake lwIP and real Mbed TLS; weather tests also verify the firmware TLS configuration and bundled CA.
    - Weather tests cover all 15 fields, caching, cancellation, malformed responses and failures.
    - These prove software behavior, not wireless throughput or hardware latency. See [firmware testing](../system/firmware-testing.md).

## Pass gate

- <input type="checkbox" data-checklist-id="gate-responsive" aria-label="Combined Phase 10 operation qualified"> Browser remains responsive at the baseline clock.
- <input type="checkbox" data-checklist-id="gate-rx-ready" aria-label="Readable terminal status"> Status matches queued bytes and active transport.
- <input type="checkbox" data-checklist-id="gate-cpm" aria-label="CP/M acceptance passed"> Interactive commands, warm boot and A-D copies pass.
- <input type="checkbox" data-checklist-id="gate-core-ownership" aria-label="Core ownership preserved"> No HTTP, WebSocket or Wi-Fi work runs on core 0.
- <input type="checkbox" data-checklist-id="gate-counters" aria-label="No unexplained faults"> No unexplained drops, trap faults or storage errors.
- <input type="checkbox" data-checklist-id="gate-storage-final" aria-label="Storage acceptance complete"> Complete [Phase 9 final acceptance](phase-9-flash-storage.md#pass-gate).

## Core ownership and terminal routing

**Developer reference:** expand only when changing or diagnosing firmware.

??? info "Core responsibilities and terminal ownership"

    - **Core 0:** Z80 GPIO/clock/traps, journal recovery, validated SRAM loading, queued supervisor requests and flash quiescence. No USB endpoint calls.
    - **Core 1:** USB, runtime storage, ENV, services, Wi-Fi and HTTP/WebSocket. Disk work runs before network work; networking uses `pico_cyw43_arch_lwip_poll`.
    - **Queues:** 128-byte console RX, 512-byte TX, four supervisor entries and a 4 KiB diagnostic ring (128-byte drain/pass). Short locks and atomic state publication separate cores.
    - **Ownership:** one browser replaces ordinary USB I/O; USB supervisor/setup stays available. Switching transports clears queued input/output. Wi-Fi changes alone do not clear USB-owned queues.
    - **Disconnected:** output is discarded and TX-ready stays set. Intentional discards are distinct from reported queue overflows.
    - **Ports:** `0x00` data; `0x01` bit 0 RX-ready, bit 1 TX-room/disconnected, bit 7 active transport. Storage/service ports and safe stop/flush are unchanged.

### Module boundaries

??? info "Which module owns what?"

    | Module | Responsibility |
    | --- | --- |
    | `network_runtime.cpp` | Wi-Fi lifecycle, CYW43 polling, IPv4 publication and SNTP; no terminal queues |
    | `web_terminal.cpp` | HTTP/WebSocket sessions, heartbeat and flow control; no Wi-Fi configuration |
    | `network_services.c` | Schedules network, weather and chat services |
    | `chat.c`, `chat_stream.c`, `chat_network.cpp` | Bounded chat ABI, HTTP/SSE parsing and asynchronous DNS/TCP/TLS; core boundary is the service mailbox |
    | `terminal_queue.c` | Transport-neutral queues and ownership |
    | `usb_console.c` | Core-1 USB/alarm initialization and bounded stdout routing |

    Wi-Fi disables power saving after association; failure drops the link and
    retries. IPv4 publication refreshes once per second. Chat never touches
    terminal queues, USB input or Z80 GPIO.

### Acknowledged terminal protocol, version 3

??? info "Protocol and buffer limits"

    Use the served browser page, not a raw-byte WebSocket client. Version 1/2
    pages are rejected. Messages start with a one-byte type and four-byte
    little-endian sequence; data sequences start at 1 and wrap modulo 2^32.

    | Type | Direction | Sequence / payload |
    | --- | --- | --- |
    | 5 HELLO | Pico to browser | Version 3 / none |
    | 6 READY | Browser to Pico | Version 3 / none |
    | 1 OUTPUT | Pico to browser | Output sequence / 1-512 bytes |
    | 3 OUTPUT_ACK | Browser to Pico | Last consumed output sequence / none |
    | 2 INPUT | Browser to Pico | Input sequence / 1-64 bytes |
    | 4 INPUT_ACK | Pico to browser | Accepted input sequence / none |

    - **Output:** four frames retain at most 2 KiB. ACKs are cumulative, after xterm consumes bytes; duplicates are not rendered twice. Retry after 250 ms; oldest-frame timeout is 15 seconds.
    - **Batching:** isolated/full frames send immediately. Later partial frames wait up to a fixed 1 ms deadline, then the next service pass. Flash pauses may delay service. Accepted TCP writes are not duplicated after failed flushes.
    - **Input:** one frame waits for RX space; admission/rejected ACKs retry each pass. ACK means queued, not executed. The browser caps pending input at 64 KiB, rejects an overflowing paste entirely and warns when disconnect discards input.
    - **Bounds:** 69-byte incoming messages; queued payload at most 1,024 bytes/16 messages; dispatch at most eight per connection/pass. Overflow disconnects.
    - **Memory:** maximum build buffer 521 bytes; four frames use 2,084 of 5,840 TCP send-buffer bytes. Core-1 stack is 8 KiB in main RAM. These are not total memory-use measurements.
    - **Cleanup:** one-second close grace; callback-safe abort also runs offline. Heartbeat pings every 5 seconds; no valid pong for 15 seconds releases ownership.

### Terminal performance diagnostics

??? info "Read the Ctrl-] s counters"

    | Fields | Meaning |
    | --- | --- |
    | `frames`, `bytes` | Original TCP-accepted output; excludes retries and headers |
    | `retransmits`, `write_blocks`, `flush_failures` | Application retries, enqueue backpressure and explicit burst-flush failures |
    | `timeouts` | Sessions released by heartbeat/output timeout |
    | `window_full_polls`, `window_wait_max_us` | Full-window service passes and longest full interval |
    | `ack_max_us` | Longest first-enqueue to consumption-ACK interval; includes transport and browser scheduling |
    | `net_poll_gap_max_us` | Longest gap between CYW43 polls, including storage/USB work and pauses |
    | `[webbuf] frames`, `retained`, `staged`, `peak` | Finalized frames, retained/staged payload and high-water marks |
    | `[webbatch] avg_bytes`, `max_bytes`, `small_le16`, `full` | Original-frame sizes/counts; excludes retries |
    | `[consolebuf] rx`, `tx`, `peak` | Console occupancy, capacity and high-water marks |

    Compare deltas for the same workload. Small frames are not inherently a
    fault; full windows plus high ACK age suggest consumer/transport pressure.
    `bytes / frames` measures batching, not throughput.

    Occupancy clears on handover; peaks survive it. Web counters reset on
    reboot; console peaks reset on initialization. Counters wrap modulo 2^32;
    microsecond maxima saturate at `UINT32_MAX`. Fields are individual atomic
    snapshots, not one transaction.

    **Pre-batching baseline, 22 Oct 2026:** CLOCK delivered 19 redraws in
    20 seconds (97.5 ms median, 926.2 ms maximum; longest internal gap 863.5 ms).
    Median payload was 7 bytes; browser consumption was 0.3 ms at p95.
    This is not evidence of a post-change speedup.

## Required Integration Order

??? info "Preserve the Phase 9 startup sequence"

    1. Initialize safe GPIOs and console/control queues.
    2. Start core 1; initialize its alarm pool, USB and flash lockout.
    3. With RESET held, recover the journal, load SRAM and independently verify it.
    4. Initialize storage/service queues before enabling workers; enable the trap and 20 MHz run clock.

    Boot/core-1 failure leaves RESET asserted. Core 0 continually services flash
    ownership requests. Stop drains disk/service/provisioning work before holding
    RESET; reboot reloads CP/M. Use dedicated builds for RAM/clock qualification.

### Maintained source

Stage 10 compiles the Phase 9 application with `Z80_STAGE09_AUTOBOOT=1` and
`Z80_STAGE10_NETWORK=1`; it does not maintain a separate loader.

- [Stage 10 build configuration](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/CMakeLists.txt)
- [Browser page and update tool](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/stage10_websocket_terminal/Terminal) (CMake compresses changes automatically)
- [WebSocket server fork](https://github.com/gloveboxes/pico-ws-server) (publish submodule changes to the fork before updating the parent gitlink)

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

</div>
