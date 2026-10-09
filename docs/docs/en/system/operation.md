# 6. Architectural Operational Boundaries

## The three operating states

| State | Who controls the buses? | What you should expect |
| --- | --- | --- |
| Injected loading | Z80 alone drives SRAM addresses and writes | RESET# is released after slow reset clocks; Pico supplies instructions with SRAM reads inhibited and verifies CPU-written RAM. |
| Normal execution | Z80 addresses SRAM directly | Both Pico data paths are isolated; ordinary memory access needs no Pico software. |
| Trapped I/O | Z80 holds its address while the Pico services one byte | HCT32 WAIT and clock stopping hold the cycle; four buffered address bits select the port. |

Runtime flash writes temporarily request the Z80 bus using BUSREQ#/BUSACK#.
This is different from cold boot: a running CPU must acknowledge the request
before flash programming. The Pico never drives memory addresses or writes,
even during a bus grant. The detailed rules below protect that boundary.

## 6.1 Hardware-WAIT-Assisted Clock-Stop Trap Protocol

IORQ# LOW reaches HCT32 pin 4 and makes WAIT# pin 6 LOW while Pico GP9
IO_RELEASE is LOW. The buffered falling edge interrupts core 0, which stops
PWM. The fully static CMOS Z80 tolerates the stopped clock. Firmware samples
A0/A1/A2/A4 and requires exactly one of RD#/WR# active before servicing data.

For OUT, U10 receives the CPU byte; for IN, U9 supplies a preloaded reply.
Only after the path is configured does GP9 rise. Firmware completes the I/O
cycle with slow single clocks, observes IORQ/strobe release, disables both
paths, waits at least 1 us for isolation, lowers GP9, then restores PWM.
Every slow step includes a fixed 1 us LOW settling interval before the control
sample, preventing an extra clock caused by propagation or input-synchronizer
delay. This prevents a later memory read from
re-enabling the Pico output. RD#-qualified U9 OE provides additional strobe
gating, not software-independent ownership arbitration.

WAIT assertion is fixed logic, not IRQ/PIO dependent. HCT32 propagation,
WAIT setup/hold, PWM-to-SIO transitions, and data setup still require captures.
The 500 ms release timeout fails closed through RESET and watchdog recovery.

## 6.2 Terminal I/O over Pico WebSocket

The final terminal is a virtual I/O peripheral implemented by the Pico,
not a Z80-side UART. Z80 software performs ordinary `IN` and `OUT`
instructions against a small terminal port pair; the Pico intercepts
those cycles through the
[Section 6.1 clock-stop trap](#61-hardware-wait-assisted-clock-stop-trap-protocol),
then resumes the
CPU after sampling or supplying the data byte. A browser connects to the
Pico over Wi-Fi and receives the terminal stream through a WebSocket
server running on the Pico 2 W.

Wi-Fi/WebSocket is the intended final and day-one operating terminal.
Before networking is introduced, [Phase 8](../implementation/phase-8-virtual-io.md)
uses USB CDC over the Pico's
existing USB connector as an intermediate terminal transport. USB CDC
consumes no GPIO and appears as `/dev/cu.usbmodem...` on macOS. It lets
the same `0x00`/`0x01` virtual-port contract and bounded queues be tested
without lwIP timing in the loop. Keep diagnostics framed or on a separate
CDC interface so debug text cannot become CP/M input. That firmware
uses ordinary bytes as terminal input and reserves Ctrl-] followed by one
command byte for framed diagnostics. [Phase 10](../implementation/phase-10-websocket.md)
replaces the USB transport
endpoint with Wi-Fi/WebSocket; it does not remove USB diagnostics or make
USB the final user interface.

The WebSocket server must run on the Pico's other core so Wi-Fi, lwIP,
HTTP serving, and WebSocket polling cannot interfere with the timing of
the Z80-facing supervisor path. Core 0 owns GPIO, clock stop/resume, assisted
loading, and the I/O trap. Core 1 owns Wi-Fi association, the
embedded terminal page, WebSocket accept/send/receive, and network
polling. The cores share terminal byte queues, one immutable disk-write
queue, a small request/result pair for Z80 bus ownership, and atomic
terminal/disk status words; they share no raw bus GPIO ownership.

Use the `pico-altair-8800` WebSocket console as the software pattern:
one embedded HTML terminal page, one WebSocket server, and byte queues
between the CPU-facing side and the network-facing side. The trap hook
must never call lwIP, print, sleep, wait for a browser, or block on a
queue. It may only enqueue Z80 output bytes, dequeue already-buffered
browser input bytes, and report terminal status. Core 1 may drop, drain,
or back-pressure network data; it must not take application-level locks or
touch any Z80 bus GPIO. Pico SDK `queue_try_add()`/`queue_try_remove()` do
not wait for space or data, but they do use brief internal spinlock critical
sections for multicore safety. Include that bounded contention in measured
trap-latency qualification rather than describing these queues as lock-free.

The initial terminal decode is intentionally small:

| Port | Direction | Function |
|----:|----|----|
| `0x00` | Z80 `OUT` | Terminal transmit byte from Z80 to browser |
| `0x00` | Z80 `IN` | Terminal receive byte from browser to Z80; returns `0x00` if empty |
| `0x01` | Z80 `IN` | Terminal status: bit 0 = receive byte available, bit 1 = transmit queue has room, bit 7 = WebSocket client connected |

This is enough for ROM monitors, BASIC, CP/M console glue, and small
diagnostics. If later software needs modem-control style signals, add
more virtual status bits before adding hardware.

## 6.3 Onboard Flash CP/M Disk Storage

The Pico's 4 MiB onboard flash stores its firmware, the Z80 boot package, and
four persistent read/write CP/M disks. No external storage, SPI bus, or extra
GPIO is required. The linker limits Pico firmware to the first 2.5625 MiB and
reserves the remainder as follows:

| Region | Flash offset | Size | Contents |
|----|----:|----:|----|
| Journal | `0x290000` | 64 KiB | Eight rotating metadata/data sector pairs |
| Boot | `0x2A0000` | 128 KiB | Header and Z80 memory image |
| Drive A | `0x2C0000` | 320 KiB | CP/M system disk |
| Drive B | `0x310000` | 320 KiB | CP/M data disk |
| Drive C | `0x360000` | 320 KiB | CP/M data disk |
| Drive D | `0x3B0000` | 320 KiB | CP/M data disk |

**Disk format.** Each disk is exactly 327,680 bytes: 80 tracks of 32
sequential 128-byte records. This project-specific format is not IBM 3740
SSSD. Converted Altair source disks are checked into `src/disks/generated/`,
so normal builds need no conversion step. The optional
`src/disks/convert_altair_disks.py` script removes Altair framing and skew,
extracts the CP/M records, and pads the images from 77 to 80 tracks.

**Build and provisioning.** The `z80_cpm_images` target automatically creates
the boot package, four disk images, and combined
`build/cpm/z80romless-flash.bin`. The build does not flash the Pico. For
initial setup, put it in BOOTSEL mode and run:

```sh
cmake --build build --target z80_cpm_images -j
picotool load -v build/cpm/z80romless-flash.bin -t bin -o 0x10000000
picotool reboot
```

Use separate artifacts only for selective updates. The
[firmware build and provisioning page](firmware-build.md) gives their commands
and addresses. Full provisioning, `flash_nuke.uf2`, and mass erase destroy
existing disk contents, so keep backups.

**Runtime behavior.** On cold boot, the Pico recovers any interrupted disk
update, validates `z80boot.pkg`, copies its image to Z80 SRAM, and verifies the
copy, then resets to PC zero before execution. The BIOS transfers one 128-byte disk record at a
time. Core 0 serves reads from memory-mapped flash and queues writes to core 1,
which maintains one 4 KiB cache in RP2350 internal SRAM, separate from the
Z80's external SRAM. Directory writes flush immediately. Other writes flush
on cache eviction, an explicit BIOS request, warm boot, or 250 ms of
inactivity.

A 64 KiB journal makes 4 KiB sector replacement recoverable after power loss.
At startup, firmware replays complete, CRC-valid committed entries and rejects
invalid or out-of-range entries. The
[Phase 9 implementation guide](../implementation/phase-9-flash-storage.md)
defines the journal sequence, multicore lockout, failure handling, and tests.

Flash has finite erase endurance. The cache coalesces writes and skips clean
blocks, but sustained write-heavy workloads should use replaceable or
wear-levelled storage.

## 6.4 System Performance Envelope & Constraints

- **I/O Decode Width:** Four buffered lines A0/A1/A2/A4, mask `0x17`.
  A3/A5-A7 and the high byte alias; terminal 00/01 and disk 10-14 remain distinct.

- **Trap Latency Profile:** HCT32-generated WAIT# covers the interval from
  IORQ# falling until IO_RELEASE reports a configured data path. The
  Pico stops the static CPU clock while servicing the cycle.
  Measure IORQ#-to-WAIT#, WAIT# setup/hold, the final PWM edge, and
  IO_RELEASE-to-WAIT# release and stepped completion at every claimed rate. The design remains
  suitable for low-rate virtual peripherals, not high-speed line tracing.

- **Clock Validation Targets:**

  - *Bring-Up Target:* **1 MHz**, qualified only after a logic-analyzer
    capture proves every I/O cycle stops before the Z80 samples or
    releases its data.

  - *Qualification Range:* **2 MHz – 6 MHz**, tested in the increments
    in the [frequency-qualification plan](../implementation/frequency-qualification.md).
    No rate in this range is guaranteed in advance.

  - *Experimental Range:* Requests **6.5 MHz – 8 MHz** may be attempted in
    requested 500 kHz steps only after 6 MHz passes. At 144 MHz `clk_sys`,
    the 6.5 MHz request still produces 6 MHz; subsequent requests produce
    approximately 6.545455, 7.2, and exactly 8 MHz nominally. These rates are exploratory,
    not design claims, because the 55 ns SRAM and breadboard margin
    dominate despite the faster buffer.

  - *Failure Boundary:* Any WAIT setup/hold failure, SRAM setup failure,
    malformed clock edge, or repeatable memory/I/O error ends
    qualification at the preceding passing step.

  - *20 MHz CPU Rating:* The `Z84C0020PEC` rating applies to the CPU,
    not this breadboard system with no memory wait states. At 20 MHz a clock period
    is 50 ns, shorter than the SRAM's 55 ns rated access time even before
    AHCT244/HCT32 propagation, Z80 setup, and breadboard delay. Reaching 20 MHz requires a
    redesigned control path, hardware-generated memory and I/O wait
    states (or deterministic clock gating), and a PCB-level signal-
    integrity review; changing the Pico PWM frequency is insufficient.

  - *PCB Deferred:* The existing layout implements the previous circuit.
    It provides no qualification evidence or manufacturing package for this
    assisted-loader revision.
