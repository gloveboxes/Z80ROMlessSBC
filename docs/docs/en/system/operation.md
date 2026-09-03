# 6. Architectural Operational Boundaries

## 6.1 Hardware-WAIT-Assisted Clock-Stop Trap Protocol

When the Z80 executes an I/O instruction, IORQ# LOW reaches ATF22V10
pin 13 and drives WAIT# LOW through GAL pin 20 while DATA_ENABLE remains
LOW. In parallel, the buffered IORQ# falling edge trips a Pico interrupt.
The handler disables the hardware PWM clock slice after GPIO
synchronization and interrupt-entry latency. Because the Z84C00 is fully
static, the clock can then remain stopped indefinitely in either a HIGH
or LOW state.

After resolving RD#/WR#, the handler configures the appropriate data
path. The data-bus helper raises DATA_ENABLE only after direction and
data are stable; the GAL then releases WAIT#. The handler resumes CLK,
waits until both IORQ# and the active RD#/WR# control are HIGH, and only
then lowers DATA_ENABLE to isolate the bus and re-arm WAIT# for the next
cycle. Hardware WAIT# removes dependence on interrupt latency alone, but
the maximum qualified frequency remains measured because GAL delay, Z80
WAIT setup/hold, clock phase, and breadboard signal integrity still need
logic-analyzer evidence.

PIO is deliberately not placed in the WAIT# assertion path. Raw IORQ#
already reaches the GAL directly, so WAIT# is asserted after the GAL's
combinational propagation delay without GPIO synchronization, PIO sampling,
or processor interrupt latency. A PIO state machine could notify firmware or
gate a PIO-generated clock, but it would not make this existing assertion path
faster and C would still perform the MCP23S17 and data-bus service. The design
therefore keeps the GAL as the timing-critical interlock and uses the RP2350
interrupt only after the Z80 has been held safely.

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
the Z80-facing supervisor path. Core 0 owns GPIO, MCP23S17 SPI, clock
stop/resume, DMA, and the I/O trap. Core 1 owns Wi-Fi association, the
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

Boot images and CP/M disk images live in a reserved region of the
Pico's own onboard flash, not on any external card or bus. This needs
zero extra GPIO and zero extra parts: the same physical flash chip
that already holds the Pico's own firmware is memory-mapped and
directly readable by both cores at any time, so no SPI bus, socket, or
level-shifted chip-select is involved. GP27 and GP28 keep their
original job monitoring Z80 RD# and WR# through the
[SN74LVC244 input buffer](../hardware/bus-isolation.md#53-sn74lvc244an-5-v-to-33-v-input-buffer);
nothing needs
reclaiming for storage.

**Capacity budget.** The Pico 2 W's onboard flash is 4 MiB. Reserve the
top 1.4375 MiB for a power-fail journal, a distinct Z80 boot package,
and four complete 320 KiB disks, leaving 2.5625 MiB for Pico firmware.
That still comfortably covers this project's code plus the Wi-Fi
firmware/CLM blob embedded by `pico_cyw43_arch`.

| Region | Flash offset (from flash base) | Size | Contents |
|----|----:|----:|----|
| Journal | `0x290000` | 64 KiB | Eight rotating metadata/data sector pairs |
| Boot | `0x2A0000` | 128 KiB | Manifest plus a maximum 64 KiB Z80 RAM image |
| Drive A | `0x2C0000` | 320 KiB | CP/M system disk |
| Drive B | `0x310000` | 320 KiB | CP/M data disk |
| Drive C | `0x360000` | 320 KiB | CP/M data disk |
| Drive D | `0x3B0000` | 320 KiB | CP/M data disk |

Each 320 KiB slot is exactly eighty 4 KiB flash erase sectors with no
partial sector left over, so every slot boundary is also a sector
boundary.

**Disk-image contract.** Here, "320 KiB" means exactly 327,680 bytes:
2,560 linear 128-byte CP/M records. The default BIOS presents those as
80 logical tracks of 32 records, numbered 1-32, with no skew. This is a
project-specific logical geometry, not the IBM 3740 8-inch SSSD format
(77 tracks x 26 records x 128 bytes = 256,256 bytes); existing images
in another 8-inch format must be converted rather than copied into a
slot unchanged. The matching CP/M 2.2 DPB is `SPT=32`, `BSH=4`,
`BLM=15`, `EXM=0`, `DSM=155`, `DRM=63`, `AL0=0x80`, `AL1=0x00`,
`CKS=0`, and `OFF=2`. Keep these values in the Z80 BIOS and host image
builder from one shared generated definition.

**Disk-image conversion.** From the repository root, run:

```sh
python3 src/disks/convert_altair_disks.py
python3 src/cpm/build_images.py --output-dir build/cpm
```

- The first script removes the Altair sector framing and skew, extracts the
128-byte CP/M records, pads the images from 77 to 80 tracks, and writes them to
`src/disks/generated/`. 
- The second assembles the board BIOS and optimized
CCP/BDOS, replaces Drive A's system tracks, and writes the provisionable images
to `build/cpm/`. Drives B-D pass through unchanged. Other source formats need
a converter that produces the 320 KiB layout defined above.

**Reservation mechanism.** The RP2350 default linker script includes a
file named `pico_flash_region.ld`. After `pico_sdk_init()`, the root
`CMakeLists.txt` writes that file into the build directory with a `FLASH`
origin of `0x10000000` and length of `0x290000`. The linker therefore rejects
code or `const` data that grows into storage, while the
`PICO_FLASH_SIZE_BYTES` C macro remains the board's physical 4 MiB and
`hardware_flash` accepts physical offsets through `0x3FFFFF`. Pico SDK 2.2
can also generate the linker fragment from a same-named CMake variable, but
this project writes the fragment explicitly so the linker limit and physical
device size have distinct names. Never pass
`-DPICO_FLASH_SIZE_BYTES=0x290000` to the compiler: that would replace the
physical-size contract. Add
`static_assert(PICO_FLASH_SIZE_BYTES == 4u * 1024u * 1024u, "Pico 2 W flash size changed")`
to the storage module and inspect the link map in CI to require
`__flash_binary_end <= 0x10290000`.

The build must also link `pico_flash`, `hardware_flash`,
`hardware_watchdog`, `pico_multicore`, and `pico_util`. Define
`PICO_FLASH_ASSUME_CORE1_SAFE=1`: core 0 uses `flash_safe_execute()`
only for journal recovery before core 1 is launched, while later core-1
writes still lock out core 0 normally. Core 0 must never erase/program
flash after core 1 starts. The essential CMake ordering is:

```cmake
pico_sdk_init()

file(WRITE "${CMAKE_BINARY_DIR}/pico_flash_region.ld"
  "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 0x290000\n")

target_compile_definitions(z80_flash_disk PUBLIC
  PICO_FLASH_ASSUME_CORE1_SAFE=1)
target_link_libraries(z80_flash_disk PUBLIC
  pico_flash hardware_flash hardware_watchdog pico_multicore pico_util)
```

**Provisioning.** Build each disk as a flat, exactly-320-KiB binary and
build the boot package described below, then write them outside the
running firmware. Use `-v` so `picotool` verifies every load:

```sh
picotool load -v -o 0x102A0000 -t bin build/cpm/z80boot.pkg
picotool load -v -o 0x102C0000 -t bin build/cpm/drive_a_cpm63k-z80.img
picotool load -v -o 0x10310000 -t bin build/cpm/drive_b_bdsc.img
picotool load -v -o 0x10360000 -t bin build/cpm/drive_c_escape.img
picotool load -v -o 0x103B0000 -t bin build/cpm/drive_d_blank.img
```

(`0x10000000` is the Pico's XIP flash base address, so these absolute
addresses are the flash-offset column above plus that base.) Reject any
disk file whose host-side size is not exactly 327,680 bytes. If an
RP2350 partition table is later embedded, declare these as data
partitions and load them by partition ID; do not casually add
`--ignore-partitions`. Firmware-only UF2 updates normally leave these
addresses untouched, but `flash_nuke.uf2`, mass erase, or a replacement
partition table destroys them, so keep host backups. There is no
runtime bulk-image upload path in this design.

`z80boot.pkg` is little-endian. Its first 20 bytes are, in order,
32-bit magic `0x5442385A` ("Z8BT"), 16-bit version 1, 16-bit header
length 20, 32-bit image length, 32-bit IEEE CRC32 of the image, and
32-bit IEEE CRC32 of the preceding 16 header bytes. Pad the remainder
of the first 4 KiB with `0xFF`; the image begins at package offset
`0x1000` and must be 1-65,536 bytes. The Z80 reset vector is at image
offset zero. Generate this package and the BIOS DPB constants from one
host tool so geometry and integrity metadata cannot drift.

**Read path.** A disk read copies from a single 4 KiB SRAM cache when it
matches the selected drive and flash block; otherwise it copies directly
from the memory-mapped flash region. There is no SPI transaction, DMA
request, or cross-core handshake. The READY status uses release/acquire
ordering, so core 0 cannot inspect the cache while core 1 is changing it.

**Write and recovery path.** CP/M still transfers 128-byte records, but the
Pico coalesces them in one 4 KiB erase-block cache. The BIOS passes the
standard CP/M `WRITE` classification from register C: normal writes and the
first record of a newly allocated CP/M block may remain dirty in SRAM;
directory writes flush immediately. Selecting another flash block flushes
the previous block first, 250 ms without another changed record triggers an
idle flush, and warm boot issues an explicit flush before it reloads CCP/BDOS.
Thus CP/M cannot persist directory metadata ahead of its referenced data, a
completed overwrite cannot remain indefinitely only in SRAM, and sequential
writes to one track need at most one journaled flash update instead of as many
as 32. An unchanged record does not dirty the cache.

The trap copies each complete write request into `disk_write_queue` and
reports BUSY; it never erases flash itself. When a flush is required, core 1
asks core 0 to acquire BUSACK# while trapping remains enabled, and core 0
disables the trap only after BUSACK# is LOW. Core 1 then uses a bounded
`flash_safe_execute()` call, which parks the registered core-0 victim and
disables core-1 interrupts.

Each update rotates through one of eight 8 KiB journal pairs:

1. Erase the pair, then program the replacement 4 KiB data block.
2. Program a one-page header containing sequence, target offset, and
  CRC32. Until this valid header exists, the target remains untouched.
3. Erase/program the target block and verify all 4 KiB through XIP.
4. Erase the journal header only after verification succeeds.

At cold boot, core 0 scans valid journal headers before loading the Z80
image and restores them in sequence order. Therefore power loss before
step 2 leaves the old target intact; power loss during or after step 3
leaves a valid replacement block from which boot recovery can finish.
After a write, core 0 arms the trap while BUSACK# is still LOW and only
then releases BUSREQ#, eliminating any interval in which the Z80 can
run without I/O trapping. A bus-acquisition failure leaves the trap
enabled; a release failure asserts RESET# and disables the trap.
IORQ#, RD#, and WR# all tri-state whenever BUSACK# is asserted (Z8400
datasheet), so `io_trap_handler()` itself first confirms BUSACK# is
still HIGH before touching anything; a stray edge while the Pico holds
the bus disables the IORQ# interrupt and is ignored. The fitted
[5 V-side 10 kOhm pull-ups](../hardware/inventory.md#03-capacitors-and-resistors)
keep the LVC244 inputs defined throughout
the grant; Pico-side pulls cannot bias an input on the other side of the
buffer.
A successful write or recovery requires both a `PICO_OK` safe-execute
result and callback verification of the flash contents. A runtime
safe-execute failure asserts RESET#, isolates the buses, stops CLK, and
forces a watchdog reboot before attempting another core-0 request:
SDK lockout-exit failure can leave core 0 parked, so continuing to its
release queue would deadlock instead of recovering.

Flash write endurance is finite and chip-specific; confirm the Pico 2 W's
actual onboard flash part's rated erase-cycle endurance before relying on
this for a write-heavy workload. The erase-block cache substantially reduces
wear for sequential CP/M writes, but this partition remains a poor fit for a
disk used as constant scratch/swap space.

**Core assignment.** Reads happen inline in `io_trap_handler()` on core 0.
Writes and flushes are deferred to core 1, the same task that owns the
[WebSocket terminal](#62-terminal-io-over-pico-websocket). A cache-only write returns READY without a
flash operation; after 250 ms of write inactivity, core 1 flushes it. Every
required flush asks core 0's foreground loop to freeze the Z80, performs the
journaled update, clears the cache's dirty state, and only then releases the
Z80. A successful idle flush does not modify protocol status, so it cannot
consume or overwrite a foreground command state. Wi-Fi association is a
bounded polling state machine, so storage remains available with no access
point. Flash access never touches SPI0, the MCP23S17, or any Z80 bus GPIO.

## 6.4 System Performance Envelope & Constraints

- **I/O Decode Width:** Strictly limited to **8-bit** decoding
  (monitoring address lines A0-A7 via the lower expander port).

- **Trap Latency Profile:** GAL-generated WAIT# covers the interval from
  IORQ# falling until DATA_ENABLE reports a configured data path. The
  Pico still stops the static CPU clock for unrestricted SPI servicing.
  Measure IORQ#-to-WAIT#, WAIT# setup/hold, the final PWM edge, and
  DATA_ENABLE-to-WAIT# release at every claimed rate. The design remains
  suitable for low-rate virtual peripherals, not high-speed line tracing.

- **Clock Validation Targets:**

  - *Bring-Up Target:* **1 MHz**, qualified only after a logic-analyzer
    capture proves every I/O cycle stops before the Z80 samples or
    releases its data.

  - *Qualification Range:* **2 MHz – 6 MHz**, tested in the increments
    in the [frequency-qualification plan](../implementation/frequency-qualification.md).
    No rate in this range is guaranteed in advance.

  - *Experimental Range:* **6.5 MHz – 8 MHz** may be attempted in
    500 kHz steps only after 6 MHz passes. These rates are exploratory,
    not design claims, because the 55 ns SRAM and breadboard margin
    dominate despite the faster buffer.

  - *Failure Boundary:* Any WAIT setup/hold failure, SRAM setup failure,
    malformed clock edge, or repeatable memory/I/O error ends
    qualification at the preceding passing step.

  - *20 MHz CPU Rating:* The `Z84C0020PEC` rating applies to the CPU,
    not this no-wait-state breadboard system. At 20 MHz a clock period
    is 50 ns, shorter than the conservative 79.5 ns component-delay sum
    for the worst-case GAL + AHCT244 + SRAM select-to-data path. That sum
    excludes Z80 setup and breadboard delay. Reaching 20 MHz requires a
    redesigned control path, hardware-generated memory and I/O wait
    states (or deterministic clock gating), and a PCB-level signal-
    integrity review; changing the Pico PWM frequency is insufficient.

  - *Future PCB:* A PCB should make 6-8 MHz more credible by reducing
    stubs, contact resistance, loop area, and uncontrolled return paths.
    It cannot remove the 55 ns SRAM or GAL delays. A true zero-wait
    20 MHz PCB needs roughly 10-15 ns SRAM plus faster decode/control
    logic; alternatively it can apply hardware WAIT# to memory cycles.
