# I/O Ports and Services

The Pico provides virtual peripherals through Z80 `IN` and `OUT` instructions.
This page owns the port allocation and the **ROMLESS-SVC/1** extended-service
protocol, compatible with the ESP32 Altair `io_ports.h` and dcc `DXSVC.H`
client. Extended services are implemented in both Phase 9 variants and Phase 10.
Phase 8 retains its original bring-up diagnostics.

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
| `0x05` | 5 | BIOS cold-entry/CCP-handoff reports; Phase 9 disk-test and Phase 10 qualification results | Reserved for diagnostics |
| `0x06` | 6 | Phase 9 disk-test bytes and Phase 10 qualification data; Phase 8 test results | Reserved for diagnostics |
| `0x07` | 7 | SRAM bank select (OUT bit 0) / capability and selected bank (IN) | PIO-only bank switching at 20 MHz; `IN A,(07h)` uses the trap to read availability and selected bank |
| `0x10` | 16 | Disk command (OUT) / status (IN) | Fixed |
| `0x11` | 17 | Disk drive selection | Fixed |
| `0x12` | 18 | Disk sector address, low byte | Fixed |
| `0x13` | 19 | Disk sector address, high byte | Fixed |
| `0x14` | 20 | Disk data, IN/OUT | Fixed |
| `0x15` | 21 | HTTP(S) file-transfer command (OUT) / status (IN) | Implemented in Phase 10 |
| `0x16` | 22 | File-transfer filename/data bytes | Implemented in Phase 10 |
| `0x17` | 23 | Independent countdown timers and stopwatches, IN/OUT | Implemented |

This assigns or reserves **all 16 ports**. A port supports
both IN and OUT, so a command and its status can share one number.

Bank select returns `B4h`/`B5h` when the loader verified banked SRAM, or `00h`
when unavailable (including when PIO is inactive). GP8 drives A16 through a spare AHCT244 channel. Use the
[banked-memory transfer library and wiring](../hardware/address-interface.md#banked-128-kib-sram)
for C programs: a raw bank select also switches the executing code and stack.

The BIOS emits `B0h` on port `0x05` at cold entry and `C0h` immediately before
handoff to CCP (including warm boot). Phase 9 observes these as execution
milestones, not proof of a working CP/M prompt. Its read-only disk test uses
port `0x06` for compared bytes and `5Ah`/`E1h` on port `0x05` for completion/
failure. These diagnostic meanings are mode-specific and do not change the
fixed terminal or disk ABI.

### HTTP and HTTPS file transfer

Phase 10 supports the existing
[FT.COM protocol](https://github.com/gloveboxes/Altair-8800-AI-Edition/blob/main/Apps/FT/FT.ASM)
on ports 21/22. Phase 9 returns ERROR for downloads.

Set `HTTP_BASE_URL` using interactive `ENV` to preserve URL case: run `ENV`,
choose **A** to add or an entry number to edit, then **Q** to quit. For example:

```text
https://raw.githubusercontent.com/AzureSphereCloudEnabledAltair8800/RetroGames/refs/heads/main/
```

Download text with `FT -G LOVE.BAS`, then run `MBASIC LOVE`. Use
`FT -GB PROGRAM.COM` for binary files. Text mode normalizes CR LF and appends
`^Z`; binary mode preserves bytes. Files use uppercase 8.3 basenames and
128-byte record padding. Omit the filename (`FT -G` or `FT -GB`) to enter a
case-preserving path at the prompt.

Full HTTP/HTTPS URLs need no base setting. Relative paths extend the base;
a leading `/` starts at the server root. For a local server, run
`python3 -m http.server 8000 --bind 0.0.0.0` and set the base to its LAN URL,
for example `http://192.168.1.10:8000/`.

HTTPS requires Wi-Fi, synchronized SNTP time, and a trusted certificate chain
and hostname. The [CA bundle](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/http_ca.pem)
can be replaced with `-DZ80_HTTP_CA_FILE=/absolute/path/to/bundle.pem`.
Plain HTTP needs no synchronized clock.

The port ABI is independent of shared services on ports 2/3/4:

| Operation (decimal ports) | Behavior |
| --- | --- |
| OUT 21, 1 | Reset; collect a new filename/URL |
| OUT 22, byte | Append filename bytes, terminated by NUL |
| OUT 21, 3 | Request the next chunk; status becomes BUSY |
| IN 21 | `0` IDLE, `1` DATA, `2` EOF, `3` BUSY, `4` missing/empty base URL, `255` ERROR |
| IN 22 | Read count (`0` = 256), then that many file bytes |
| OUT 21, 4 | Cancel/close; return to IDLE |

Consume each chunk fully before requesting another. Empty files return EOF;
reset after ERROR. Transfers stream on core 1 without loading the whole file
into RAM. HTTP/1.0 and 1.1, chunked bodies, and up to five redirects are
supported; HTTPS downgrades, compressed or malformed responses fail.
Network stalls time out after 30 seconds, excluding consumer backpressure.
See [http_io.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/http_io.c)
and [http_stream.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/http_stream.c)
for implementation limits.

## Why A4 is monitored but A3 is not

Only A0/A1/A2/A4 are decoded (`address & 0x17`). A3, A5–A7, and the high
address byte are ignored: `0x09` aliases terminal status `0x01`, `0x30`
aliases disk command `0x10`, and `0xFF` aliases timer port `0x17`.
A4 keeps console and disk registers distinct. This applies only to I/O;
A3 still connects Z80 pin 33 to SRAM pin 9, not U7.

Each register has 16 low-byte aliases. A historical Phase 9 `SURVEY` scan
reported 111 active addresses for seven registers (112 aliases, excluding
`0x00`), not 111 peripherals. Current services change that count. Scanning
response ports can cause UNDERFLOW; RESET the shared channel afterward.
Service group `0x30` is a command byte written to `0x02`, not a port address.

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

### Worker ownership

Core 0 handles traps and timers; core 1 runs slow services after disk work.
The trap never performs flash/network work or waits for a service response.
Worker replies are read in place and remain valid until consumed or discarded.
Generation tags reject stale replies after RESET. One shared transaction may
be outstanding at a time.

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
| Weather `0x40` | `[0, field_id]` reads text; `[1]` reads status | Phase 10 cached weather; Phase 9 UNAVAILABLE |
| Chat `0x50` | Reset/append/submit/status/chunk operations below | Phase 10 streaming OpenAI/Ollama; Phase 9 UNAVAILABLE |
| Sense HAT `0x60` | Recognized group without a Pico driver | UNAVAILABLE |

There are **four countdown timers**: IDs 0-2 take 0-65535 milliseconds;
ID 3 takes 0-255 whole seconds. The shared duration field is four bytes for
client compatibility, but out-of-range values are rejected. Timer 3 uses
the ESP32 whole-second boundary rule: `(current_ms / 1000 + duration) * 1000`.
A zero duration expires immediately.

There are **three stopwatches**, IDs 0-2. Reads do not reset them. A latched
32-bit millisecond count wraps after about 49.7 days; seconds wrap after
about 136 years. Internal deadlines and uptime use a 64-bit monotonic clock.

Phase 10 starts SNTP against `pool.ntp.org` after Wi-Fi association. Calendar
operations return CLOCK_UNSYNCED until the first valid synchronization.
Phase 9 has no network time source and returns CLOCK_UNSYNCED for calendar
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

The timer port works while a clock request is BUSY. Phase 10 uses SNTP at
`pool.ntp.org`; calendar calls return CLOCK_UNSYNCED (`0x15`) until synchronized.
Time then advances through Wi-Fi outages. Uptime and timers need no network.

Set local time with POSIX `TZ`, for example `ENV TZ=AEST-10` (UTC+10) or
`ENV TZ=ACST-9:30` (UTC+09:30). POSIX signs are reversed; daylight-saving
rules are supported. Changes apply on the next request. `UTC_OFFSET` is unused.
CLOCK’s weather display requires `OWM_KEY` and `OWM_LOCATION` in Phase 10;
Phase 9 reports weather UNAVAILABLE.

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

CHAT 1.7-c11 reads `CHAT_THINK` each turn for compatible/Ollama providers:
`false` sends `reasoning_effort=none`, `true` sends `medium`, and `default`
omits the field. Model/server support determines the effect. Missing/empty
values default to `false`; invalid values fail submission. OpenAI requests
omit this setting. Older CHAT binaries retain server defaults.

The USB shell also offers **Ctrl-] c → 3 Chat → 7 configure thinking**.
`CHAT_PROVIDER=ollama` aliases `compatible`. Use an installed model name;
the example address/model is configuration, not a required endpoint.

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

#### Streaming and limits

Core 1 owns the 4096-byte response ring and async client. Bounded network
polls and backpressure keep the trap and other services responsive; generation
tokens reject stale callbacks after cancellation.

The decoder requires HTTP 200, uncompressed `text/event-stream`, and a final
`[DONE]`. It accepts content-length, connection-close, and chunked bodies.
Only `choices[0].delta.content` is displayed; other fields are ignored.
JSON/UTF-8 are validated and converted to CP/M-safe 7-bit text. Limits are
4096 header bytes, 4095 bytes per HTTP/SSE line or decoded event, and 16 JSON
nesting levels.

The firmware deadline is 120 seconds, including backpressure. Failures publish
`Chat: ...` after queued text, then EOF; requests are not automatically replayed.
CHATC11 also has a roughly 30-second no-content timeout and an 8191-byte answer
limit, so it may stop first. Diagnostics omit credentials and conversation text.

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

Core 1 waits for Wi-Fi/SNTP, then 5 seconds before fetching. Refreshes repeat
every 20 minutes; initial failures retry every 2 minutes for five retries.
Reconnection triggers a fresh fetch. Cached data survives fetch failures with
an increasing age and ERROR status. A forecast-only failure keeps current
conditions READY and reports a warning in field 12.

HTTPS requires certificate-chain, hostname, and SNTP-based date verification,
using [weather_ca.pem](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/weather_ca.pem).
There is no insecure fallback. Requests have a 15-second deadline, 16 KiB
wire/8 KiB decoded limits, and at most 2 KiB of headers. Content-length,
connection-close, and chunked responses are supported; malformed, oversized,
or compressed bodies fail. TLS/parsing run on core 1, outside Z80 traps.

### System information and reboot

System-info selectors are 0 = hostname `zeta-80-xxxxxx` (the final six
hexadecimal characters of the board ID, in lowercase), 1 = current Wi-Fi
IPv4 address, and 2 = Pico unique board ID. The IPv4 selector returns
UNAVAILABLE without a connection, including Phase 9. Reboot requires the
exact confirmation byte `0xA5`; it flushes queued/cached disk writes, then
core 0 disables traps, holds reset, isolates the data paths and stops the
clock before watchdog reboot. New disk commands are gated before the reboot
job is published, with BUSY+ERROR reported on disk status, so writes cannot
race the final flush. Failed flushes reopen the gate and report STORAGE_WRITE
instead.

### Onboard LED device

Utility group `0x21` controls the Pico 2 W LED in Phases 9/10:

- `[4, 0]` turns it off; `[4, 1]` turns it on. Success returns COMPLETE; ACK it.
- `[5]` returns the last successfully commanded state (`0`/`1`), initially off.
- Invalid state/length returns INVALID_PARAMETER/INVALID_LENGTH; driver failure
  returns UNAVAILABLE without changing the reported state.

The worker initializes the wireless-chip driver lazily on core 1. Phase 9 uses
its LED-only backend; no Wi-Fi connection is required. State is not persisted,
and transport RESET leaves it unchanged. This is asynchronous LED control.

For example, write `21h` to port `02h`, then `02h,00h,04h,01h` to port `03h`
(group, length prefix, set operation, on). Poll for COMPLETE/ERROR, then ACK
success or drain and acknowledge the error.

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

Two 4 KiB sectors at **`0x1FE000` and `0x1FF000`** occupy unused space in the
boot partition, beyond the boot payload ending at `0x1F1000` (exclusive). Firmware, the
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

## Availability and validation

Phase 10 implements file transfer, weather, and chat. Phase 9 reports download
errors and weather/chat UNAVAILABLE. Sense HAT is unavailable in both.
The ESP32 interrupt-timer ABI is unsupported: `0x07` selects SRAM banks, and
this board has no Pico-to-INT# driver or interrupt-vector path.

Host tests cover framing, aliases, recovery, timers, ENV persistence/capacity,
calendar formatting, HTTP/SSE parsing, cancellation, TLS configuration, and
phase routing. They do not establish physical timing, live provider behavior,
or power-cut recovery.

- [Phase 3](../implementation/phase-3-address-generator.md) owns address wiring.
- [Phase 8](../implementation/phase-8-virtual-io.md) owns initial trap checks.
- [Phase 9](../implementation/phase-9-flash-storage.md) owns flash bring-up.
- [DCC documentation](https://davidly.github.io/dcc/) covers port access
  and CP/M BDOS/BIOS APIs.
