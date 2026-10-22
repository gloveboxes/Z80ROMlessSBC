# I/O Ports and Services

The Pico provides virtual peripherals through Z80 `IN` and `OUT` instructions.
This page owns the port allocation and the **ROMLESS-SVC/1** extended-service
protocol, compatible with the ESP32 Altair `io_ports.h` and dcc `DXSVC.H`
client. Extended services are implemented in both Stage 9 variants and Stage 10.
Stage 8 retains its original bring-up diagnostics.

Maintained sources:
[transport and timers](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_ports.c),
[service worker](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_service.c),
[clock and utilities](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_service_platform.c),
[persistent environment store](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/environment_store.c)
and [public constants](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/io_ports.h).
See [operational boundaries](operation.md) for the terminal, disk and
clock-stop trap contracts.

## Port allocation

The four monitored address bits, A0/A1/A2/A4, provide **16 distinct ports**:
`0x00-0x07` and `0x10-0x17`, using `address & 0x17`.
Other addresses are aliases, not additional ports. For example, `0x08`
aliases `0x00` and `0x18` aliases `0x10`. No additional wiring is required.

| Port (hex) | Decimal | Function | Allocation |
| --- | ---: | --- | --- |
| `0x00` | 0 | Terminal data, IN/OUT | Fixed |
| `0x01` | 1 | Terminal status | Fixed |
| `0x02` | 2 | Shared service command (OUT) / status (IN) | Implemented |
| `0x03` | 3 | Shared service length-prefixed request bytes (OUT) | Implemented |
| `0x04` | 4 | Shared service length-prefixed response bytes (IN) | Implemented |
| `0x05` | 5 | BIOS cold-entry/CCP-handoff reports; Stage 9 disk-test and Stage 10 qualification results | Reserved for diagnostics |
| `0x06` | 6 | Stage 9 disk-test bytes and Stage 10 qualification data; Stage 8 test results | Reserved for diagnostics |
| `0x07` | 7 | Spare; ESP32 interrupt timer is deferred on this hardware | Unallocated |
| `0x10` | 16 | Disk command (OUT) / status (IN) | Fixed |
| `0x11` | 17 | Disk drive selection | Fixed |
| `0x12` | 18 | Disk sector address, low byte | Fixed |
| `0x13` | 19 | Disk sector address, high byte | Fixed |
| `0x14` | 20 | Disk data, IN/OUT | Fixed |
| `0x15` | 21 | File-transfer command (OUT) / status (IN) | Reserved, not implemented |
| `0x16` | 22 | File-transfer filename/data bytes | Reserved, not implemented |
| `0x17` | 23 | Independent countdown timers and stopwatches, IN/OUT | Implemented |

This assigns or reserves **15 ports and leaves one spare**. A port supports
both IN and OUT, so a command and its status can share one number.

The BIOS emits `B0h` on port `0x05` at cold entry and `C0h` immediately before
handoff to CCP (including warm boot). Stage 9 observes these as execution
milestones, not proof of a working CP/M prompt. Its read-only disk test uses
port `0x06` for compared bytes and `5Ah`/`E1h` on port `0x05` for completion/
failure. These diagnostic meanings are mode-specific and do not change the
fixed terminal or disk ABI.

### Interpreting CP/M SURVEY results

On the earlier Phase 9 USB CP/M loader, before extended services, `SURVEY`
(System Survey, June 1982) reported 111 active ports. This was the alias set
of the seven terminal/disk registers, not 111 independent peripherals.

Because A3 and A5-A7 are ignored, each register has 16 equivalent addresses
within the low-byte range `0x00-0xFF`. For example, `0x01`, `0x09`, `0x21`
and `0x29` all access terminal status; `0x10`, `0x18`, `0x30` and `0x38`
all access disk command/status. Seven registers occupy 112 low-byte addresses;
the captured scan omitted `0x00`, leaving 111.

Extended services add readable registers, so a new scan need not reproduce
that historical count. An active result does not prove correct peripheral
operation. Scanning the response port can also cause an explicit UNDERFLOW
error; RESET the shared channel before using it afterward.

## Why A4 is monitored but A3 is not

The Pico decodes I/O ports using `address & 0x17`. The `&` operator is
**bitwise AND**: a result bit is 1 only when both corresponding bits are 1.
The hexadecimal mask `0x17` is binary `0001 0111`:

```text
Address bit:  A7 A6 A5 A4 A3 A2 A1 A0
Mask 0x17:     0  0  0  1  0  1  1  1
```

This keeps **A4, A2, A1 and A0**, matching the four address connections to U7.
A3, A5-A7 and the high address byte are deliberately ignored for I/O decoding.

```text
Address 0x08:  0000 1000
Mask    0x17:  0001 0111
               ---------
AND result:    0000 0000 = 0x00
```

Thus `0x08` aliases `0x00`, `0x09` aliases `0x01`, and `0xFF` aliases the
timer port `0x17`. A4 distinguishes console `0x00/01` from disk `0x10-14`.
With a mask of `0x07`, disk command `0x10` would incorrectly alias console data.
Service **group** `0x30` is a byte written to command port `0x02`, not an
OUT to physical port `0x30` (which aliases disk command `0x10`).

This omission applies only to **I/O monitoring**, not memory addressing.
A3 still connects **Z80 pin 33 to SRAM pin 9** in
[Phase 4](../implementation/phase-4-address-bus.md#wiring-address-trunk);
do not connect it to U7.

## Shared service interface

### Framing and recovery

There is one outstanding shared transaction at a time:

1. Write RESET (`0x01`) to port `0x02` to recover an unknown parser state.
2. Write the service group to `0x02`.
3. Write a **16-bit little-endian payload length** to `0x03`, then that many
   payload bytes. Length includes the operation byte and must be 1-2048.
4. Poll status on `0x02` with a bounded client-side polling limit.
5. For RESPONSE_READY, read the **16-bit little-endian response length** from
   `0x04`, then exactly that many bytes. The last byte returns the channel to
   READY. An empty response still has its two length bytes.
6. For COMPLETE, write ACK (`0x00`) to `0x02`.
7. For ERROR, read the length prefix and one-byte error code from `0x04`,
   then ACK. If an overlapping transaction poisoned the parser, RESET is
   required instead.

| Status | Value | Meaning |
| --- | --- | --- |
| READY | `0x00` | Can accept a new command |
| COMPLETE | `0x01` | Successful operation with no response; ACK required |
| RESPONSE_READY | `0x02` | Length-prefixed response can be read |
| COLLECTING | `0x03` | Awaiting the length prefix or remaining payload |
| BUSY | `0x04` | Worker executing the request |
| ERROR | `0xFF` | Length-prefixed error can be read |

Special commands on `0x02` are ACK (`0x00`), RESET (`0x01`) and IDENTIFY
(`0x02`). IDENTIFY needs no request and returns the text `ROMLESS-SVC/1`
with the usual response length prefix. Text replies have **no trailing NUL**;
binary replies are not strings.

RESET invalidates pending replies through a generation identifier, but
**does not undo side effects** or cancel a worker already writing flash.
A new worker request can return BUSY_ERROR until that job finishes. RESET
does not reset running timers or stopwatches. There is no automatic server
timeout for an incomplete request: the client must use bounded polling,
report failure and RESET. Reading status never acknowledges COMPLETE.

Error codes match the ESP32 client:

| Code | Error |
| --- | --- |
| `0x01` / `0x02` | UNKNOWN_GROUP / UNKNOWN_OPERATION |
| `0x03` / `0x04` | INVALID_LENGTH / INVALID_PARAMETER |
| `0x05` / `0x06` | UNAVAILABLE / BUSY_ERROR |
| `0x07` / `0x08` / `0x09` | TIMEOUT (reserved) / INVALID_STATE / UNDERFLOW |
| `0x10` / `0x11` / `0x12` | STORAGE_OPEN / STORAGE_READ / STORAGE_WRITE |
| `0x13` / `0x14` | CAPACITY / NOT_FOUND |
| `0x15` / `0x16` | CLOCK_UNSYNCED / REBOOT_CONFIRMATION |

### Groups and operations

All integer request fields and binary results are little-endian.

| Group | Operations and payloads | Result |
| --- | --- | --- |
| Timers `0x10` | `[0, id, duration_u32]` starts/replaces; `[1, id]` queries | COMPLETE or one byte, 1 running / 0 expired |
| Stopwatches `0x11` | `[0, id]` resets; `[1, id]` reads seconds; `[2, id]` reads milliseconds | COMPLETE or unsigned 32-bit elapsed count |
| Clock `0x20` | `[0]` uptime seconds; `[1]` UTC ISO 8601; `[2]` local ISO 8601; `[3]` long local date; `[4]` DATE-style local date/time | Text |
| Utility `0x21` | `[0]` random; `[1, selector]` system info; `[2, 0xA5]` confirmed reboot; `[3]` version; `[4, state]` set onboard LED; `[5]` read LED state | Two random bytes, text, COMPLETE, or one LED-state byte |
| Environment `0x30` | Operations below | Text or COMPLETE |
| Weather `0x40` | `[0, field_id]` reads text; `[1]` reads status | Phase 10 cached weather; Stage 9 UNAVAILABLE |
| Chat `0x50` | Reset/append/submit/status/chunk operations below | Phase 10 streaming OpenAI/Ollama; Stage 9 UNAVAILABLE |
| Sense HAT `0x60` | Recognized group without a Pico driver | UNAVAILABLE |

There are **four countdown timers**: IDs 0-2 take 0-65535 milliseconds;
ID 3 takes 0-255 whole seconds. The shared duration field is four bytes for
client compatibility, but out-of-range values are rejected. Timer 3 uses
the ESP32 whole-second boundary rule: `(current_ms / 1000 + duration) * 1000`.
A zero duration expires immediately.

There are **three stopwatches**, IDs 0-2. Reads do not reset them. A latched
32-bit millisecond count wraps after about 49.7 days; seconds wrap after
about 136 years. Internal deadlines and uptime use a 64-bit monotonic clock.

Stage 10 starts SNTP against `pool.ntp.org` after Wi-Fi association. Calendar
operations return CLOCK_UNSYNCED until the first valid synchronization.
Stage 9 has no network time source and returns CLOCK_UNSYNCED for calendar
queries. Uptime remains available in both. Local formatting uses persistent
`TZ` in POSIX timezone syntax (for example, `AEST-10`); an absent `TZ` means UTC.

### CLOCK application time compatibility

The ESP32 Altair dcc `Apps/CLOCK` time interface is already provided by these
services; no extra physical ports or separate time driver are needed:

| CLOCK request | Pico interface | Result |
| --- | --- | --- |
| Local time | Clock group `0x20`, payload `[2]` on shared ports 2/3/4 | `YYYY-MM-DDTHH:MM:SS` (19 bytes) |
| Local date | Clock group `0x20`, payload `[3]` | `Monday, 01 January 2024` |
| Uptime | Clock group `0x20`, payload `[0]` | Decimal seconds since Pico startup |
| Start refresh timer | OUT 23: `0x12`, `50`, `0` | Timer 2, 50 ms, low duration byte first |
| Query refresh timer | OUT 23: `0x22`, then IN 23 | `1` while running, `0` when expired |

The timer port remains usable while a shared clock request is BUSY. Calendar
formatting, ENV access, and network time synchronization run on core 1; the
Z80 trap only transfers the framed request/reply or handles the independent
monotonic timer. Calendar replies contain exactly their length-prefixed text,
without a trailing NUL.

For Phase 10, connect Wi-Fi and allow DNS/UDP access to `pool.ntp.org`.
Calendar requests return explicit `CLOCK_UNSYNCED` (`0x15`) errors until SNTP
has supplied a valid time. Once synchronized, time continues advancing from
the monotonic clock through Wi-Fi outages and is corrected by later SNTP
updates. Uptime and the refresh timer do not depend on Wi-Fi or SNTP.

Configure the timezone through the existing ENV service, for example
`ENV TZ=AEST-10` for UTC+10 or `ENV TZ=ACST-9:30` for UTC+09:30. POSIX signs
are opposite to conventional UTC offsets. A full POSIX `TZ` rule can include
daylight-saving transitions. Changes take effect on the next clock request
without rebooting. The Pico remains **TZ-only**: the ESP32 CLOCK help text's
`UTC_OFFSET` setting is not used here.

The unchanged CLOCK source also polls the weather group on every refresh.
Phase 10 now supplies its cached status and field requests through the
[weather service](#openweathermap-weather). Phase 9 remains USB-only and
returns UNAVAILABLE for weather.

The `clock_app_time_services` host integration test drives CLOCK's port
transactions through the real parser, worker, calendar formatter, SNTP
callback hook, and persistent ENV backend. It covers unsynchronized errors,
UTC/local/date/uptime shapes, timezone updates and DST, 50 ms timer operation
during a BUSY request, and the 32-bit millisecond rollover. It does not execute
the CLOCK binary or qualify physical network/hardware operation.

### CHAT application: OpenAI and Ollama

Phase 10 implements the framed ABI used by the ESP32 Altair dcc
`Apps/CHAT/CHATC11.C` application. Use the existing CP/M application and its
`CHAT.SYS` system-prompt file; no BIOS, pin or disk-layout change is needed.
CHAT 1.7-c11 adds application-side thinking control as described below; earlier
versions still work but leave thinking at the server default. Firmware builds
do not install the application on a disk.
Do not use the legacy physical chat ports 120-124: they alias unrelated
peripherals with this board's address decoding.

All requests use group `0x50` through shared ports 2/3/4:

| Payload | Result |
| --- | --- |
| `[0]` | Cancel any previous request and clear request assembly; COMPLETE |
| `[1, json_bytes...]` | Append 1-2047 bytes; COMPLETE, or explicit CAPACITY/BUSY/invalid-input error |
| `[2]` | Cancel the connection and clear queued response/error text; COMPLETE; assembled request is retained |
| `[3]` | Submit the assembled request asynchronously; COMPLETE, or BUSY/empty-request error |
| `[4]` | One status byte: `0` EOF, `1` WAITING, `2` DATA_READY |
| `[5, maximum_u16]` | `[status, count_u16, text_bytes...]`, with status **after** removing the returned bytes |

`maximum` is 1-2045, bounded by the 2048-byte service payload. CHATC11 requests
256 bytes at a time and appends JSON in 2047-byte pieces. The assembled request
limit is **8191 bytes**, excluding its local NUL terminator. Overflow never
silently truncates a request. Appending or resubmitting while active returns
BUSY. Operation 0 cancels before reusing the request buffer, so a new turn
recovers safely after CHATC11 fills its local response buffer or times out.
Operation 2 also cancels immediately. Shared transport RESET only invalidates
the mailbox reply; as with ENV, it does not undo service side effects.

#### Provider configuration

For a local Ollama server with its OpenAI-compatible API:

```text
ENV CHAT_PROVIDER=compatible
ENV CHAT_ENDPOINT=http://192.168.1.26:11434/v1/chat/completions
ENV -I CHAT_MODEL=phi4-mini:3.8b
ENV CHAT_MAX_TOKENS=1024
ENV CHAT_TEMPERATURE=0.7
```

With **CHAT 1.7-c11**, `CHAT_THINK` defaults to `false` for
`CHAT_PROVIDER=compatible` or `ollama`. The application sends
`"reasoning_effort":"none"` to request a direct answer. Set
`ENV CHAT_THINK=true` to send `"reasoning_effort":"medium"` (thinking on for
boolean-control models, medium effort where supported), or
`ENV CHAT_THINK=default` to omit the field and use the server/model default.
Only models that support disabling thinking can honor `none`.

The app re-reads provider/thinking settings before each turn and never adds
the parameter to OpenAI requests. Invalid values or ENV errors stop submission
explicitly. An absent/empty thinking value is seeded as `false` on first use;
the standard service helper can print NOT_FOUND while seeding a missing key.
Other compatible servers that do not support `reasoning_effort` should use
`CHAT_THINK=default` to retain their old request format. Installing the updated
`CHAT.COM` is required for this behavior; the existing Pico firmware needs no
change and continues to forward the application's bounded JSON unchanged.

`CHAT_PROVIDER=ollama` is an alias for `compatible`. Discover installed model
names from a host with `curl http://192.168.1.26:11434/api/tags`; the model must
match a deployed name exactly. CHATC11's built-in `gemma3:1b` default is not
guaranteed to be installed. The example server address is configuration, not
a compiled-in endpoint.

For OpenAI:

```text
ENV CHAT_PROVIDER=openai
ENV -I CHAT_OPENAI_KEY=<your-api-key>
ENV -I CHAT_MODEL=gpt-4o-mini
ENV CHAT_MAX_TOKENS=1024
ENV CHAT_TEMPERATURE=0.7
```

The ENV CLI evaluates `+`/`-` inside ordinary assignments as arithmetic.
Use literal `-I` for API keys and model names; it only inserts absent keys.
To replace an existing key, first use `ENV -D CHAT_MODEL` (or
`ENV -D CHAT_OPENAI_KEY`), then its literal insert command. Do not paste real
keys into logs or source control. ENV is plaintext onboard flash, not
encrypted secret storage.

An absent provider defaults to `openai`. OpenAI always uses
`https://api.openai.com/v1/chat/completions`, ignoring `CHAT_ENDPOINT`.
Only OpenAI requests carry `CHAT_OPENAI_KEY`; a saved OpenAI key is **never**
forwarded to an Ollama/compatible endpoint. Authenticated third-party compatible
providers are not supported. URLs require an explicit path and accept HTTP or
HTTPS, a DNS name/IPv4 address, and an optional port 1-65535. User-info,
fragments, whitespace and control characters are rejected. Limits are 95
host bytes, 103 authority bytes, 159 path bytes, and 255 key bytes.

Core 1 reloads provider/endpoint/key from durable ENV on each submission.
CHATC11 owns the JSON model, history, system prompt, token count, temperature
and thinking parameter; restart CHAT after changing model/token/temperature
settings. Thinking/provider changes are read at the next turn by CHAT 1.7-c11.
HTTPS requires synchronized
SNTP time and verifies certificate dates, chain and hostname using the bundled
[GTS Root R4](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/chat_ca.pem).
There is no insecure TLS fallback. Compatible HTTPS endpoints need a chain
trusted by that root; arbitrary private/self-signed CAs are not supported.
Plain HTTP Ollama works without SNTP but must remain on a trusted LAN.

#### Ownership, bounds and failure behavior

The trap transfers framed bytes through the existing release/acquire atomic
single-request mailbox. It does not access ENV, JSON, sockets or TLS, and never
waits for an answer from the provider. Core 1 owns request assembly, the
4096-byte response ring, configuration, and the independent async HTTP client.
Wi-Fi, terminal sessions and chat do not consume one another's queues or
configure one another. Disk work still runs first on every application loop.

HTTP writes and received wire processing are limited to 512 bytes per service
pass. A received pbuf chain is retained without extra copying, bounded to one
chain of at most 16 KiB; further chains receive lwIP `ERR_MEM` backpressure.
TCP credit advances only for bytes consumed by the parser. A full CP/M response
ring pauses parsing rather than dropping text or blocking the other services.
Accepted TCP writes are never replayed after a failed flush. Callback teardown
is deferred to the service loop, callbacks are detached before abort, and
generation tokens reject stale DNS/connection callbacks after cancellation.

The decoder accepts HTTP/1.0 or 1.1, content-length, connection-close or chunked
bodies, and OpenAI-style single-line SSE JSON records. It requires HTTP 200,
`text/event-stream`, uncompressed content, and a final `[DONE]` marker.
Headers are limited to 4096 bytes, each HTTP/SSE line to 4095 bytes, decoded
text per event to 4095 bytes, and JSON nesting to 16 levels. Only
`choices[0].delta.content` is displayed; role, usage, tools and reasoning fields
are ignored. JSON escapes and UTF-8 are validated and converted to CP/M-safe
7-bit text, preserving newline/tab and mapping common smart punctuation;
other non-ASCII code points become `?`.

The request has a **120-second total deadline**, including backpressure.
Wi-Fi loss/reconfiguration, premature EOF, allocation/write errors, oversized
input, malformed HTTP/JSON and HTTP/provider errors terminate the connection
and publish explicit `Chat: ...` text after already queued content, then EOF.
Configuration failures are reported the same way. Diagnostics do not print
credentials, prompts, provider error bodies or generated answers. CHATC11 itself
has an approximately **30-second no-content timeout** and an 8191-byte local
answer limit; a long-thinking model can hit that client limit before the
firmware deadline. No artificial answer bytes or automatic duplicate POSTs
are generated to hide these limits.

Native `extended_io_services`, `chat_http_sse`, `chat_async_transport` and
`weather_real_tls_config` tests exercise the real mailbox/worker/parser/client
and both trust roots. They cover chunk framing, split input, full queues,
no-duplicate writes, cancellation/restart, failure cleanup, stale callbacks,
and missing or invalid configuration. They do not execute the CP/M binary or
prove live inference, heap/stack headroom, or combined hardware timing.

### OpenWeatherMap weather

Phase 10 supports the ESP32 Altair weather ABI through shared service group
`0x40` on ports 2/3/4. Do not use the old physical ports 46/47/200: these
alias other peripherals on this board.

- Payload `[1]` returns one status byte: `0` NONE, `1` FETCHING, `2` READY,
  or `3` ERROR. Status reads never wait for an HTTP request.
- Payload `[0, field_id]` returns length-prefixed text, without a trailing
  NUL. Units and error text are available before the first successful fetch;
  other fields return UNAVAILABLE until data exists.
- Unknown operations, incorrect lengths and unknown fields return the usual
  explicit service errors. An HTTP failure is exposed by status ERROR and
  field 12, not by inventing weather data or failing every status query.

| Field | Text |
| ---: | --- |
| 0 | City |
| 1 / 2 | Current condition / description |
| 3 / 13 | Current temperature / feels-like temperature |
| 4 / 5 | Humidity percent / wind speed |
| 6 / 7 | Forecast condition / description |
| 8 / 14 | Forecast temperature / feels-like temperature |
| 9 | First forecast block time, as supplied by OWM in UTC |
| 10 | Seconds since the last successful current-data refresh |
| 11 | Temperature units: `C`, `F`, or `K` |
| 12 | Last error or forecast warning; empty after a fully successful refresh |

Temperatures and wind speed are rounded to integer strings as in the
reference driver; wind is m/s for metric/standard and mph for imperial.
Only the first forecast block is requested (`cnt=1`).

Configure the existing durable ENV store:

```text
ENV OWM_KEY=<your-api-key>
ENV OWM_LOCATION=Sydney,AU
ENV OWM_UNITS=metric
```

`OWM_KEY` is limited to 40 bytes, location to 64 bytes. Location accepts a
city/country query or a validated `lat=-33.8&lon=151.2` coordinate pair.
The ENV CLI treats `+` and `-` inside assignments as arithmetic; use its
literal insert mode for coordinates or names containing these characters:
`ENV -D OWM_LOCATION`, then
`ENV -I OWM_LOCATION=lat=-33.8&lon=151.2`.
Units are `metric` (default), `imperial`, or `standard`. Missing keys or invalid
settings produce an explanatory error; no request is sent. Settings are
reloaded at most once per second. Changing them cancels an in-flight request,
clears data belonging to the old configuration, and starts a new fetch without
rebooting. ENV remains plaintext onboard flash; do not publish its contents.
Firmware diagnostics never print the key, request URL or provider response body.

Core 1 waits for Wi-Fi and a synchronized SNTP clock, then allows 5 seconds
for the network to settle before fetching current conditions and forecast.
Successful refreshes repeat every 20 minutes. Initial failures retry every
2 minutes for five retries, then every 20 minutes; failures after a successful
fetch use the 20-minute interval. Reconnection schedules a fresh fetch.
The last successful cache remains readable after failures, with its increasing
age and explicit error status. A failed forecast still publishes valid current
conditions as READY, leaves forecast fields empty, and reports a forecast
warning in field 12 and on USB.

HTTPS uses port 443, required certificate-chain and hostname verification,
and certificate date checks against the SNTP-derived UTC clock. There is no
HTTP or insecure-TLS fallback. The bundled USERTrust RSA public root is
maintained in
[weather_ca.pem](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/weather_ca.pem)
(SHA-256 `E793C9B02FD8AA13E21C31228ACCB08119643B749C898964B1746D46C3D4CBD2`,
valid through 18 January 2038). Update the trust anchor if the provider changes
its chain. Its RSA/SHA-384 signature requires `MBEDTLS_SHA384_C`; enabling
SHA-512 alone does not enable SHA-384 in Mbed TLS 3. Without it, TLS
configuration fails while parsing the CA, before DNS or HTTP starts.
DNS, connection and response work share a 15-second request deadline;
responses are bounded to 16 KiB on the wire and 8 KiB decoded, with at most
2 KiB of HTTP headers. Content-length, connection-close and chunked responses
are supported; malformed, oversized or compressed bodies fail explicitly.

The weather worker uses no Z80 trap callbacks, extra core, or blocking socket.
Network callbacks only collect bounded data; parsing, ENV access and cache
publication happen on core 1 between its normal disk/service/network polls.
Phase 10 uses an explicit 8 KiB core-1 stack in main RAM for TLS; Phase 9
retains its original launch path and reports weather UNAVAILABLE.

### System information and reboot

System-info selectors are 0 = hostname `z80romless-sbc`, 1 = current Wi-Fi
IPv4 address, and 2 = Pico unique board ID. The IPv4 selector returns
UNAVAILABLE without a connection, including Stage 9. Reboot requires the
exact confirmation byte `0xA5`; it flushes queued/cached disk writes, then
core 0 disables traps, holds reset, isolates the data paths and stops the
clock before watchdog reboot. New disk commands are gated before the reboot
job is published, with BUSY+ERROR reported on disk status, so writes cannot
race the final flush. Failed flushes reopen the gate and report STORAGE_WRITE
instead.

### Onboard LED device

Stages 9 and 10 expose the Pico 2 W onboard LED through Utility group `0x21`,
using the existing shared-service ports `0x02`-`0x04`. No wiring changes,
expansion GPIOs, or additional port IDs are needed; `0x07` remains spare
for a future interrupt timer.

- Payload `[4, 0]` turns the LED off; `[4, 1]` turns it on. Success is
  COMPLETE and requires ACK.
- Payload `[5]` returns a one-byte response: `0` off or `1` on. This is the
  last successfully commanded state, not an optical measurement.
- Other state values return INVALID_PARAMETER. Incorrect payload lengths
  return INVALID_LENGTH.
- An initial read after firmware boot initializes the LED off; an initial
  write applies the requested state directly. Its state is not persisted to
  flash, and transport RESET does not undo it.
- Both stages initialize the wireless-chip driver lazily if necessary.
  Stage 9 uses the SDK's LED-only `pico_cyw43_arch_none` backend: no Wi-Fi
  association or network stack is enabled, and the USB terminal and nominal
  20 MHz Z80 operation are unchanged. Stage 10 also works with empty Wi-Fi
  credentials or without a network connection. Driver initialization/write
  failures return UNAVAILABLE; a failed write does not update the reported state.

The worker accesses the wireless chip on core 1, never inside the core 0
Z80 I/O trap. LED control is an asynchronous service, not a cycle-timed output.

For example, to turn the LED on, issue these Z80 I/O writes:

```text
OUT port 0x02, value 0x01   ; RESET shared transport
OUT port 0x02, value 0x21   ; Utility group
OUT port 0x03, value 0x02   ; Payload length, low byte
OUT port 0x03, value 0x00   ; Payload length, high byte
OUT port 0x03, value 0x04   ; Set LED operation
OUT port 0x03, value 0x01   ; On (use 0x00 for off)
```

These are transaction steps, not literal assembly syntax. Poll port `0x02`
with a bounded limit until COMPLETE (`0x01`) or ERROR (`0xFF`); acknowledge
COMPLETE by writing `0x00` to port `0x02`. On ERROR, read and acknowledge
the framed error as described above. RESET is for recovering/starting an
idle channel, not for cancelling another client's active transaction.

### Persistent environment API

Keys are case-insensitive, stored uppercase, and contain 1-16 non-NUL bytes.
Values contain 0-255 non-NUL bytes. Empty and absent values are distinct:
GET of an empty value returns a zero-length response; an absent key returns
NOT_FOUND.

| Operation | Payload | Result |
| --- | --- | --- |
| 0 reset / 1 initialize | `[operation]` | COMPLETE |
| 2 get | `[2, key_length_u8, key_bytes]` | Value text |
| 3 set | `[3, key_length_u8, key_bytes, value_length_u16, value_bytes]` | COMPLETE, durable before success |
| 4 delete | `[4, key_length_u8, key_bytes]` | COMPLETE or NOT_FOUND |
| 5 list | `[5]` | `KEY=VALUE` lines with CRLF |
| 6 count | `[6]` | Decimal count text |
| 7 clear | `[7]` | COMPLETE, durable before success |
| 8 exists | `[8, key_length_u8, key_bytes]` | COMPLETE or NOT_FOUND |
| 9 execute ENV command | `[9, command_text]`, at most 511 command bytes | Text |

The ENV command parser is shared with the maintained host driver. It supports
listing, `NAME`, `NAME=VALUE`, copying another variable, numeric addition/
subtraction, `NAME+N`, `NAME-N`, `-I NAME=VALUE`, `-D NAME`, `-N`, `NOW`
(uptime seconds), and `-H`/`HELP`/`?`. Numeric overflow is an explicit error.
No additional physical ports expose the host's private legacy selectors.

The store supports at most 128 entries, subject to a **3,840-byte serialized
snapshot limit**. Each entry consumes `key length + value length + 2` bytes.
There is room for at least 14 maximum-length key/value pairs. Requests that
exceed storage or the 2,048-byte reply capacity fail explicitly; LIST never
silently truncates. After a failed write, the in-memory state is reloaded
from the newest valid durable snapshot; an error does not undo a commit
that already reached flash.

Two 4 KiB sectors at **`0x2BE000` and `0x2BF000`** occupy unused space in the
boot partition, beyond the boot payload ending at `0x2B1000`. Firmware, the
disk journal, boot package and all four disk offsets remain unchanged.
Each snapshot has a version, sequence number, payload CRC32 and header CRC32.
Updates erase/program the inactive sector, verify the payload, and commit the
header last. The previous committed snapshot remains recoverable after an
interrupted replacement. A durable empty baseline is established before the
first mutation. Repeated unchanged writes do not erase flash.

Flash erase/program uses the disk backend's BUSREQ/BUSACK and flash-safe
multicore handshake. A non-success flash-safe result fails closed through
watchdog reboot. Both sectors erased means a new empty store; neither erased
nor CRC-valid means STORAGE_READ, not a fabricated empty environment.
Flash has finite endurance: use countdown timers and stopwatches, not
repeated persistent writes, for high-frequency counters.

!!! warning "Preserving environment variables during updates"
    Firmware-only and exact boot-package-only updates preserve variables.
    Complete 4 MiB provisioning, mass erase and `flash_nuke.uf2` destroy them.
    Do not write a padded 128 KiB boot-partition image: it would overwrite ENV.
    Full generated images deliberately contain erased environment sectors.

### Worked request bytes

These examples show the actual wire framing; prefer the existing dcc
`svc_operation_send()`, `svc_get_text()` and timer helpers in `DXSVC.H`.
Use bounded client polling, drain error frames, and RESET after failure.

```text
Read uptime:
  OUT 02,20
  OUT 03,01  OUT 03,00       ; payload length = 1
  OUT 03,00                 ; operation = uptime seconds
  IN 02 until 02 or FF       ; RESPONSE_READY or ERROR
  IN 04 twice, then length bytes

Set TZ=UTC0:
  OUT 02,30
  OUT 03,0A  OUT 03,00       ; payload length = 10
  OUT 03,03  OUT 03,02       ; SET, key length
  OUT 03,54  OUT 03,5A       ; "TZ"
  OUT 03,04  OUT 03,00       ; value length = 4
  OUT 03,55  OUT 03,54  OUT 03,43  OUT 03,30  ; "UTC0"
  IN 02 until 01 or FF       ; COMPLETE or ERROR
  OUT 02,00                 ; ACK only after success
```

## Independent timer port

Port `0x17` uses no shared-service framing and remains usable while the
service worker is BUSY. Its timers/stopwatches are the same instances as
groups `0x10/0x11`.

| OUT command | Subsequent transfers |
| --- | --- |
| `0x00` | Reset only this parser; running timers remain unchanged |
| `0x10`-`0x12` | Two OUT bytes, low then high: start millisecond timer 0-2 |
| `0x13` | One OUT byte: start whole-second timer 3 |
| `0x20`-`0x23` | IN returns selected timer's running flag |
| `0x30` | IN returns running bitmask, bits 0-3 |
| `0x40`-`0x42` | Reset stopwatch 0-2 |
| `0x50`-`0x52` | Four IN bytes: latched stopwatch seconds, little-endian |
| `0x60`-`0x62` | Four IN bytes: latched stopwatch milliseconds, little-endian |

The parser initially selects the timer bitmask. An invalid command, an IN
during parameter collection, or stopwatch over-read returns `0xFF` and
requires recovery. To recover an unknown state, perform **IN `0x17`,
then OUT `0x17`, `0x00`**. Multi-byte port-23 operations must not be
interleaved with other users of that port.

## Deferred services and validation

File transfer remains reserved on `0x15/0x16`; it is not implemented.
Chat and Sense HAT requests return explicit UNAVAILABLE errors. Weather is
implemented only in Phase 10; Phase 9 reports UNAVAILABLE.
The ESP32 interrupt timer on `0x07` is deferred: current hardware has
no Pico-to-`INT#` driver, and the `RD#`-gated data path cannot provide an
interrupt vector. No interrupt, GPIO or PCB wiring changes accompany this work.

Core 0 owns traps and timer state. Core 1 executes slow services after disk
service and before network work. A generation-tagged mailbox prevents stale
worker replies from becoming new responses. Pause/drain logic accounts for
pending service work before flash is declared quiescent. Flash and network
work never run inside the I/O trap.

Host tests cover framing, aliases, recovery, timer boundaries and latching,
ENV commands/capacity, unchanged-write suppression, snapshot recovery,
calendar formatting, stage routing and flash-safe ownership. These are
software checks, not bench timing or physical power-cut qualification.

- [Phase 3](../implementation/phase-3-address-generator.md) owns address wiring.
- [Phase 8](../implementation/phase-8-virtual-io.md) owns initial trap checks.
- [Phase 9](../implementation/phase-9-flash-storage.md) owns flash bring-up.
- [CP/M direct access](../cpm-dcc/direct-access.md) explains `IN`/`OUT`
  versus BDOS/BIOS interfaces.
