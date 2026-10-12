# I/O Ports and Services

The Pico provides virtual peripherals through Z80 `IN` and `OUT` instructions.
This page owns the overall port allocation. See
[operational boundaries](operation.md) for the implemented terminal, disk
and clock-stop trap contracts.

!!! important "Implemented ports versus proposed services"
    Terminal and disk ports are fixed by the current firmware and CP/M
    interface. Keep their assigned numbers unchanged. Shared services and
    file transfer below are proposals: they are not implemented by the
    current firmware and are not bring-up pass-gate requirements.

## Port allocation

The four monitored address bits, A0/A1/A2/A4, provide **16 distinct ports**:
`0x00-0x07` and `0x10-0x17`, using `address & 0x17`.
Other addresses are aliases, not additional ports. For example, `0x08`
aliases `0x00` and `0x18` aliases `0x10`.
The proposed allocation fits the existing wiring; no extra address buffer
or Pico GPIO is required.

| Port (hex) | Decimal | Function | Allocation |
| --- | ---: | --- | --- |
| `0x00` | 0 | Terminal data, IN/OUT | Fixed |
| `0x01` | 1 | Terminal status | Fixed |
| `0x02` | 2 | Shared service command (OUT) / status (IN) | Suggested |
| `0x03` | 3 | Shared service request bytes (OUT) | Suggested |
| `0x04` | 4 | Shared service response bytes (IN) | Suggested |
| `0x05` | 5 | Stage 10 qualification result | Reserved for diagnostics |
| `0x06` | 6 | Qualification data; Stage 8 also uses this port for test results | Reserved for diagnostics |
| `0x07` | 7 | Spare | Unallocated |
| `0x10` | 16 | Disk command (OUT) / status (IN) | Fixed |
| `0x11` | 17 | Disk drive selection | Fixed |
| `0x12` | 18 | Disk sector address, low byte | Fixed |
| `0x13` | 19 | Disk sector address, high byte | Fixed |
| `0x14` | 20 | Disk data, IN/OUT | Fixed |
| `0x15` | 21 | File-transfer command (OUT) / status (IN) | Suggested |
| `0x16` | 22 | File-transfer filename/data bytes | Suggested |
| `0x17` | 23 | Spare | Unallocated |

This reserves **14 ports and leaves two spare**, while preserving terminal,
disk and diagnostic assignments. A port supports both IN and OUT, so a
command and its status can share one number.

## Shared service interface

Three ports can serve many functions by using command and parameter bytes
to select an operation, rather than allocating one port per function:

| Service | Proposed operations |
| --- | --- |
| Two expiry timers | Select timer 0/1, set duration, start, cancel, query running/expired |
| Two stopwatches | Select stopwatch 0/1, start/reset, read elapsed time |
| Date/time and uptime | Select uptime, UTC/local date/time, or a formatted date |
| Random numbers | Request a result and read its bytes |
| Environment variables | Get, set or delete a named value |
| Reboot | Request reboot with explicit confirmation |

| Port | Role | Example |
| --- | --- | --- |
| `0x02` | OUT selects a command; IN reports transaction status | Start expiry timer 0; report awaiting parameters, busy, complete or error |
| `0x03` | OUT supplies request parameters | Send a duration, a variable name, or a name/value pair |
| `0x04` | IN retrieves response bytes | Read elapsed time, a date/time string, random bytes or an environment value |

Multi-byte durations and results use repeated byte transfers; they do not
need a separate port for each byte. Timers continue running independently
after their start transaction completes.

### Worked examples

!!! note "Illustrative dcc C, not implemented firmware"
    The examples below use dcc's `inp()` and `outp()` functions, declared in
    `<stdlib.h>`. They illustrate a command-first protocol with automatic
    execution after all required parameters arrive. Command/status byte codes
    below are example values only, not a finalized interface. The protocol still
    needs specification and implementation before these examples can run.
    Binary values use four-byte unsigned integers, least significant byte first.

dcc has 16-bit `int` and 32-bit `unsigned long`. Use `unsigned long` for
durations and cast received bytes to that type **before** shifting by 16 or
24 bits. See [direct CP/M and port access](../cpm-dcc/direct-access.md).

#### Common definitions and bounded status polling

Use these definitions with each example below. A new command is sent only
when the interface is ready. Error and polling-limit failures are reported,
not mistaken for a valid result.

```c
#include <stdio.h>
#include <stdlib.h>

#define SERVICE_COMMAND  0x02
#define SERVICE_REQUEST  0x03
#define SERVICE_RESPONSE 0x04

enum {
    READY = 0,
    COMPLETE = 1,
    RESPONSE_READY = 2,
    ERROR = 255,
    START_TIMER_0 = 16,
    GET_UPTIME_MS = 32,
    ENV_GET = 48
};

static int service_ready(void)
{
    if (inp(SERVICE_COMMAND) != READY) {
        puts("Service not ready; finish or cancel the previous request.");
        return 0;
    }
    return 1;
}

static int wait_status(unsigned wanted)
{
    unsigned polls;
    int status;

    for (polls = 0; polls < 10000u; ++polls) {
        status = inp(SERVICE_COMMAND);
        if (status == ERROR) {
            puts("Service reported an error.");
            return 0;
        }
        if (status == wanted)
            return 1;
    }
    puts("Service polling limit reached; recovery is required.");
    return 0;
}
```

The 10,000-read limit prevents an infinite loop; it is **not a calibrated
wall-clock timeout**. A final client needs a defined timeout/recovery policy.
The draft assumes COMPLETE is acknowledged by the next status read, leaving
READY, and that consuming the final response byte also returns the interface
to READY. No further commands should be issued after a failed transaction
until recovery is complete.

#### Start expiry timer 0 for five seconds

Five seconds is 5,000 milliseconds: `0x00001388`. Its four request bytes,
least significant first, are `88 13 00 00`.

```c
int start_timer_0(void)
{
    unsigned long duration_ms = 5000UL;
    unsigned byte;

    if (!service_ready())
        return 0;
    outp(SERVICE_COMMAND, START_TIMER_0);
    for (byte = 0; byte < 4; ++byte) {
        outp(SERVICE_REQUEST, (unsigned)(duration_ms & 0xffUL));
        duration_ms >>= 8;
    }
    return wait_status(COMPLETE);
}
```

Transaction completion means the timer was started, **not that it expired**.
A later `QUERY_TIMER_0` command would return a running/expired result through
`0x04`. Timer 1 uses the same three ports with a different command.

#### Read uptime in milliseconds

This request needs no parameter bytes, so it does not write to `0x03`.
For an illustrative uptime of 5,000 milliseconds:

```c
int show_uptime(void)
{
    unsigned long uptime_ms = 0UL;
    unsigned byte;

    if (!service_ready())
        return 0;
    outp(SERVICE_COMMAND, GET_UPTIME_MS);
    if (!wait_status(RESPONSE_READY))
        return 0;
    for (byte = 0; byte < 4; ++byte) {
        uptime_ms |= (unsigned long)inp(SERVICE_RESPONSE) << (byte * 8);
    }
    printf("Uptime: %lu ms\n", uptime_ms);
    return 1;
}
```

Reassemble the bytes as:

```text
value = byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24)
      = 0x88  | (0x13  << 8) | (0x00  << 16) | (0x00  << 24)
      = 0x00001388 = 5000 milliseconds
```

The same pattern serves a stopwatch or random-number result. A four-byte
millisecond count wraps after about 49.7 days; the final protocol must
document wrap behaviour or choose a wider uptime representation.

#### Get an environment variable

For an illustrative request to read `TZ`:

```c
int show_timezone(void)
{
    char value[256];
    unsigned length;
    unsigned byte;

    if (!service_ready())
        return 0;
    outp(SERVICE_COMMAND, ENV_GET);
    outp(SERVICE_REQUEST, 2);
    outp(SERVICE_REQUEST, 'T');
    outp(SERVICE_REQUEST, 'Z');
    if (!wait_status(RESPONSE_READY))
        return 0;
    length = (unsigned)inp(SERVICE_RESPONSE);
    for (byte = 0; byte < length; ++byte)
        value[byte] = (char)inp(SERVICE_RESPONSE);
    value[length] = '\0';
    printf("TZ=%s\n", value);
    return 1;
}
```

This example assumes a one-byte length prefix, limiting a value to 255
bytes. An absent variable must produce an explicit error, not be confused
with an empty value. Final framing may use wider lengths.
Date/time strings can use the same length-prefixed response pattern.

## Protocol and implementation requirements

Before implementing the services, define:

- Command codes, parameter lengths, result lengths, byte order and units.
- Status codes for ready, collecting parameters, busy, complete,
  response available and error, including how completion/errors are acknowledged.
- One outstanding transaction at a time, with an explicit reject or cancel
  policy for new commands while a request or unread response exists.
- Bounded polling, recovery from incomplete requests and response underflow.
- Explicit errors for unavailable services, missing variables and a wall
  clock that has not synchronized; do not return uptime as valid calendar time.
- A flash allocation and safe update mechanism for persistent environment
  variables, without overwriting firmware, boot images, journal or CP/M disks.
- Deliberate reboot confirmation, followed by safe Z80 stop/reset and
  data-path isolation before restarting the supervisor.

Keep proposed file transfer on its own two ports so it does not share the
service response stream. Define its filename, chunk, completion and error
protocol separately; allocating ports alone does not implement uploads or
downloads.

The [I/O trap](operation.md#61-hardware-wait-assisted-clock-stop-trap-protocol)
must remain bounded. Network transfers and slow service work must run outside
the trap; status polling reports progress instead of holding the Z80 paused
while waiting for a network or flash operation.

## Related wiring and validation

- [Phase 3: control and port monitor](../implementation/phase-3-address-generator.md)
  explains the A0/A1/A2/A4 wiring and why A3 is omitted.
- [Phase 8: virtual I/O](../implementation/phase-8-virtual-io.md) validates
  current traps and intentional aliases, not the proposed services.
- [CP/M direct access](../cpm-dcc/direct-access.md) explains `IN`/`OUT` access
  versus the usual BDOS and BIOS interfaces.
