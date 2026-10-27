# Phase 9 - USB CP/M Terminal and Flash Storage

<div data-checklist="phase-9" data-checklist-label="Phase 9" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.
Leave Phase 10 network-dependent tests unticked until completed there.
The bring-up checkpoint is not the final storage pass gate.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 9 prerequisite passed"> **Prerequisite:** The [Phase 8 pass gate](phase-8-virtual-io.md#pass-gate) must pass.

For building C applications with DCC, importing application disks from
Altair 8800 AI Edition, and deploying them with VS Code tasks, see
[Related projects](../related-projects.md). Use the supplied applications for
this phase's bring-up tests before adding your own.

**Hardware:** Keep the tested Phase 8 wiring unchanged.

**Goal:** Boot CP/M over USB and verify persistent disks, SRAM loading, and
recovery from interrupted writes.

## Build and provision from VS Code

Open the **Z80ROMlessSBC project folder**, run **Tasks: Run Task** from the
Command Palette, then choose:

| Task | Result |
| --- | --- |
| **Build and flash Pico - Phase 9 firmware** | Builds and force-flashes firmware; preserves disks and ENV |
| **Build and flash Pico - Phase 9 firmware and disks** | Builds and writes the complete image; replaces disks and ENV |

Choose **Diagnostic** for bring-up tests or **Automatic loader** after those
tests pass. Both tasks force-update without checking the previous firmware.
Use firmware and disks for initial installation or a complete BIOS/disk
refresh; use firmware only for routine updates. See
[task requirements](../system/firmware-build.md#deployment-task-requirements).

!!! warning "Before deployment"
    Save CP/M work. Apply external **+5 V before USB** and keep it applied.
    Disk tasks overwrite selected drives without backups.

<input type="checkbox" data-checklist-id="storage-provisioning" aria-label="Storage provisioned with host backups and Phase 9 firmware loaded"> Matching Phase 9 firmware, boot package, and disks installed; host backups retained.

!!! warning "Use disposable disk contents for fault tests"
    Complete normal boot/read/write checks before injecting faults. Tests
    that corrupt images, overwrite all records, or interrupt flash updates
    can destroy files. Preserve the original images and expected old/new
    blocks first. Use the documented one-shot watchdog hooks; do not simulate
    faults by pulling individual live signal wires or shorting pins.

**Fail-closed** means the supervisor deliberately holds the CPU in reset or
reboots into recovery instead of executing an unverified image. Capture the
USB error/status first. Do not bypass verification merely to get a prompt.

!!! important "USB terminal first; 20 MHz after loading"
    Phase 9 implements the same `0x00`/`0x01` terminal contract as in Phases 8
    and 10, using USB CDC rather than Wi-Fi. The diagnostic firmware
    requires separate load/start commands; the automatic loader below performs
    both on Pico reset. In either case, slow, Z80-assisted loading and complete
    readback happen before execution at **20,000,000 Hz,
    50% configured duty** (`120 MHz / 6`). The Z80 clock frequency has been
    scope-measured at 20 MHz on the project prototype. This does not establish
    CP/M, disk-I/O or receiving-pin timing margins. I/O traps and flash writes
    still pause execution. The replacement Phase 10 inherits this tested automatic-loader
    application and its clock settings; combined network operation still
    requires acceptance testing.

## Automatic CP/M loader

Both Phase 9 variants also implement
[ROMLESS-SVC/1 extended ports](../system/io-ports.md): countdown timers,
stopwatches, uptime, utilities, onboard LED control and persistent ENV. The
LED uses Utility payload `[4, 0]` for off, `[4, 1]` for on, and `[5]` to read
its last successfully commanded state. Its wireless-chip driver is initialized
on demand without enabling Wi-Fi networking; USB and 20 MHz operation
are unchanged. Calendar queries explicitly
report CLOCK_UNSYNCED in this USB-only phase. No additional wiring is required;
the interrupt timer remains deferred. Check these services with the updated
Drive C applications after the terminal/disk pass gates. Software regression
tests do not replace physical timing and persistence checks.

After the bring-up tests pass, run **Build and flash Pico - Phase 9 firmware**
and choose **Automatic loader**. Switching variants requires only a firmware update when the
installed boot package and disks already match; the diagnostic variant uses
manual `l`/`g` commands.

Every Pico reset now:

1. Establishes safe GPIO levels, holds the Z80 in reset and stops its clock.
   Launches core 1, which initializes its flash-lockout participation, the
   default alarm pool and USB before acknowledging readiness.
2. Recovers the flash journal and validates the boot manifest and payload CRC.
3. Uses Z80-assisted loading to write the complete 65,536-byte SRAM image,
   then independently reads back and compares every byte.
4. Resets the Z80 again, enables I/O trapping and starts CP/M at 20 MHz.
5. Continues terminal and disk service without waiting for a USB terminal.

Core 0 owns Z80 GPIO, loading, clock generation and I/O traps. Core 1 owns
all USB endpoint access, USB interrupts/background tasks, ANSI input mapping
and terminal pumping, plus runtime storage and service workers. Core 1 starts
before loading, but disk/service workers are enabled only after successful
load/verify and CP/M startup. The earlier single-core bring-up phases are
unchanged.

USB supervisor commands cross a four-entry queue to core 0; they never
manipulate Z80 GPIO from core 1. A full queue explicitly rejects the command
and asks the user to retry. Core 0 publishes running/loading/stopping flags;
manual-loader cancellation and USB connection state are published back from
core 1. The cancellation check remains at a 256-byte loader checkpoint.

Diagnostics from either core use a separate 4 KiB bounded log ring. Core 1
drains at most 128 bytes per pass, subject to CDC room, without holding the
ring lock over USB calls. Connected overflow is reported as `log_drops`;
disconnected logs are discarded and not replayed. CP/M output retains its
separate 512-byte terminal ring. The SDK stdout mutex is disabled for these
targets, so core 0 never waits on USB output serialization.
CP/M USB output submits at most 32 bytes in one nonblocking CDC write per pass,
with one flush attempt when bytes are accepted. It peeks at the terminal ring
and removes only the accepted prefix, leaving short or rejected writes queued
for a later pass. No terminal lock is held during endpoint calls, and single
interactive characters are submitted immediately without a batching delay.
USB remains serviced while stopped or draining for a stop. Flash programming
can temporarily suspend it through SDK lockout. Real enumeration, concurrent
network traffic and timing still require bench acceptance.

### SRAM-resident terminal and disk register path

The local performance hypothesis is that SRAM instruction placement reduces
flash-cache-dependent latency in the core-0 I/O trap. It is not a claim that
flash instruction fetches are the dominant cost. The SDK
`__not_in_flash_func` attribute places the following routines in SRAM:

- The shared Phase 9/10 application's `virtual_read` and `virtual_write`.
- `z80_terminal_io_read`, `z80_terminal_io_write`, and their ring-add and
  connection-state helpers.
- `z80_flash_disk_io_read`, `z80_flash_disk_io_write`, their command/status/address
  helpers, and `z80_flash_backend_read_record`.

Small helpers may be inlined into these SRAM-resident handlers and have no
separate linked symbol. The application still runs on core 0; USB, networking,
and runtime flash programming remain on core 1. Locks, queue bounds, register
semantics, and all hardware-settling waits are unchanged.

This is **not a completely flash-independent call chain**. SDK timing/mux
helpers, failure handling, extended service/timer ports, `queue_try_add`, and
`memcpy` can still execute from flash. Uncached disk records and diagnostic
comparison bytes are still read from flash. SRAM placement does not permit
trapping during flash programming or relax BUSACK/IRQ/multicore lockout.

#### Build verification

The 2026-10-23 comparison used the same local SDK, toolchain, build configuration,
and working-tree sources before and after adding placement attributes. Each
The Phase 9/10 linker map and ELF place the seven top-level routines above in
`.data` at SRAM addresses beginning `0x2000...`; disassembly also places callback
switch tables there. Terminal ring/connection helpers and disk command/status/
address helpers are inlined into their handlers in this build.

| Target | Initialized SRAM `.data`, before | After | Additional SRAM |
| --- | ---: | ---: | ---: |
| Phase 9 diagnostic | 9,256 bytes | 10,472 bytes | 1,216 bytes |
| Phase 9 automatic loader | 9,256 bytes | 10,472 bytes | 1,216 bytes |
| Phase 10 WebSocket terminal | 13,936 bytes | 15,152 bytes | 1,216 bytes |

BSS and stack reservations are unchanged. Total loadable ELF content grows
by 32 bytes for each Phase 9 variant and 40 bytes for Phase 10, including
linker alignment and veneers. SRAM code is included in `.data`; the plain
`size` summary's `data` column alone does not measure its RAM cost. Inspect
ELF sections and the linker map:

```sh
elf=build/src/stage10_websocket_terminal/z80_stage10_websocket_terminal.elf
arm-none-eabi-objdump -h "$elf"
arm-none-eabi-nm -S -n "$elf" |
  grep -E ' (virtual_read|virtual_write|z80_terminal_io_read|z80_terminal_io_write|z80_flash_disk_io_read|z80_flash_disk_io_write|z80_flash_backend_read_record)$'
arm-none-eabi-objdump -d --disassemble=virtual_read "$elf"
arm-none-eabi-objdump -d --disassemble=z80_flash_disk_io_write "$elf"
```

Repeat for both Phase 9 ELFs. Check that the selected functions have SRAM
execution addresses, not merely flash load addresses. Inspect each handler's
disassembly for outlined helpers and veneers: a veneer in SRAM can still jump
to flash. In this build terminal register handlers need no out-of-line calls;
disk record copies and queue submission still reach flash-resident routines.

#### Hardware comparison still required

No before/after trap-latency result was recorded for this change; hardware
deployment and measurement were explicitly deferred. Successful builds,
placement checks and host tests do not establish a measured speedup.

For a comparison, preserve both firmware builds and use identical clock settings,
CP/M images, workload, USB/browser ownership and network load. With the safe
setup in the [logic-analyzer procedure](../hardware/logic-analyzer.md), capture
Group C IN and OUT separately in 16-channel, 100 MHz Buffer Mode. Separate
terminal status/data accesses, disk status/data accesses, read commands, and
write-record submission rather than mixing their latency distributions.

Measure IORQ falling to IO_RELEASE rising as the service-response interval,
and the end of free-running CLK to its resumed free-running edges as the total
Z80 clock pause, including stepped completion. Neither is an exact IRQ-entry/
exit timestamp. Record sample count, sample resolution, median, tail percentile,
maximum, and workload/capture length for each category, both idle and with
concurrent network traffic. Check control errors, drops and trap timeouts before
and after; review the original data setup/hold, isolation, PWM handover and
settling guards on raw captures. The acceptance gate is a repeatable reduction
in the selected latency metric with no correctness/timing regression, not an
assumed improvement based on SRAM addresses alone.

Connect picocom whenever convenient, using the actual USB CDC port name:

```sh
picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/cu.usbmodemXXXX
```

There are **no load/start commands to type**. Output produced without a connected
terminal is discarded, including the CP/M banner and `A>` prompt. If CP/M is
idle at its command prompt, press Return after connecting to obtain a fresh
prompt; reconnecting does not restart a running application.
While a terminal is connected during automatic boot, the supervisor reports
SRAM load and independent verification progress every 4,096 bytes, for example:

```text
[boot] SRAM load: 32768/65536
[boot] SRAM load: 36864/65536
...
[boot] SRAM verify: 32768/65536
...
[boot] verified; RESET held LOW: 65536/65536
```

The last line precedes releasing reset and starting CP/M. Progress is not
retained or replayed when no terminal is connected, and automatic boot never
waits for USB. After CP/M starts, reconnecting preserves the current program
and does not reboot the Z80 or replay progress or a supervisor menu. ANSI key
mapping is the same as in the diagnostic firmware.

Verification or startup failure keeps the CPU stopped; it never runs an
unverified image or retries endlessly. A later terminal connection reports
the stopped state and retained failure details. Optional **Ctrl+] then `s`**
still shows status, and **Ctrl+] then `h`** prints supervisor help.
For a safe software restart, use **Ctrl+] then `x`**, wait for
**Disk quiescent; RESET held LOW, PWM stopped**, then use **Ctrl+] then `r`**.
The Pico reboots and automatically reloads CP/M.
The stop confirmation also prints the full **Ctrl+]** command menu and marks
`r` (reboot), `s` (status), and `h` (help) as available while stopped.
Loading/running/testing (`l`/`g`/`t`) requires a Pico reboot before another run;
fault commands (`0`-`8`) require a running CPU.
This resets USB CDC too, so picocom may exit with **Device not configured**.
Reconnect using the current USB port name; if loading or verification is still
underway, progress appears at the next 4,096-byte boundary before the CP/M banner.

!!! warning "Reset is not a substitute for saving and flushing"
    A Pico reset reloads SRAM and discards the running CP/M program. Save work
    and stop/flush first. Journal recovery protects committed flash updates,
    not unsaved application data or disk changes still held only in RAM.
    Keep external +5 V applied throughout in-board USB programming.

For new/disposable storage, `z80_stage09_cpm_loader_images` builds the complete
matching package in `build/cpm-stage09-loader/`. Its full-flash image contains
the automatic loader and replaces all disk contents when programmed; do not
use it for a firmware-only update.

## Controlled USB workflow

This section uses the original manual diagnostic firmware, not the automatic
loader above.

Do not deploy this firmware while another physical test or soak is running.
No probing, wiring changes or firmware replacement is required to build it.
After that test finishes, follow the
[power and provisioning procedure](../system/firmware-build.md#72-flash-provisioning).
Both the Pico firmware and the boot package/Drive A system tracks must come
from the same new build: the BIOS now emits startup evidence.

For an already provisioned board, selectively update the Phase 9 UF2,
`z80boot.pkg`, and matching `drive_a_cpm63k-z80.img` using Section 7's
addresses. Updating Drive A overwrites its existing files; the development
workflow does not create backups.
For disposable/new storage, the complete `build/cpm-stage09/z80romless-flash.bin`
contains everything. Building either target never flashes the board.

For a terminal-mapping-only firmware update on an already matching Phase 9
installation, flash only `z80_stage09_flash_storage.uf2`. The boot package,
CP/M disks and journal do not need to be rewritten.

Open the Pico's USB CDC port with a terminal, using your actual port name:

```sh
picocom --baud 115200 --databits 8 --parity n --stopbits 1 --flow n /dev/cu.usbmodemXXXX
```

Do not open a second serial owner. Disable local echo. In picocom,
**Ctrl+A then Ctrl+X** exits the host terminal; **Ctrl+] then a command**
controls the supervisor. Ordinary letters, digits and CP/M control keys
are terminal input, not diagnostic commands.

| Sequence | Supervisor action |
| --- | --- |
| Ctrl+] then `l` | Recover journal; validate manifest/CRCs, exact 65,536-byte payload and `JP FC00h`; load and independently read back every SRAM byte; leave RESET LOW |
| Ctrl+] then `g` | Start the verified image from a fresh reset at 20 MHz |
| Ctrl+] then `t` | After loading, replace the SRAM entry code with a read-only Z80 disk diagnostic; compare every byte of all four drives; reboot afterward |
| Ctrl+] then `s` | Print run state, configured clock, BIOS evidence, disk/fatal state, queue drops and trap error counts |
| Ctrl+] then `x` | While running CP/M, complete/flush disk work, pause the flash worker, then hold RESET LOW and stop PWM; during the read-only diagnostic, stop directly |
| Ctrl+] then `r` | Reboot the Pico only while stopped; journal recovery runs on the next `l` |
| Ctrl+] then `0` | Disarm a fault while running |
| Ctrl+] then `1`-`8` | Arm a one-shot journal/flash-safe watchdog fault while running; disposable disks only |
| Ctrl+] then `h` | Print help |
| Ctrl+] then Ctrl+] | Send a literal Ctrl+] to CP/M |

### ANSI keyboard mapping

Phase 9 ports the ESP32 Altair `ansi_input` mapper into the USB receive path.
Picocom forwards the host terminal's ANSI input sequences; the firmware
converts these to the CP/M application's WordStar-style control keys:

| Host key | ANSI input | CP/M byte |
| --- | --- | --- |
| Up | `ESC [ A` | Ctrl+E (`05h`) |
| Down | `ESC [ B` | Ctrl+X (`18h`) |
| Right | `ESC [ C` | Ctrl+D (`04h`) |
| Left | `ESC [ D` | Ctrl+S (`13h`) |
| Insert | `ESC [ 2 ~` | Ctrl+O (`0Fh`) |
| Delete | `ESC [ 3 ~` | Ctrl+G (`07h`) |
| Page Up | `ESC [ 5 ~` | Ctrl+R (`12h`) |
| Page Down | `ESC [ 6 ~` | Ctrl+V (`16h`) |
| Backspace (`7Fh` or `08h`) | Single byte | Ctrl+H (`08h`) |

A standalone Escape is delivered once after a 30 ms grace period, even when
no further serial bytes arrive. Sequences may span USB reads. Ordinary text
and other nonzero control bytes pass through; CRLF still becomes one carriage
return. As in the Altair mapper, NUL means no input, unsupported `ESC [` keys
are consumed, and Escape followed by a non-`[` byte forwards that byte.
This is not a general ANSI keyboard decoder: Home/End, function keys,
modified arrows and application-cursor (`ESC O`) sequences are not mapped.
Use normal cursor-key mode in the host terminal.

Ctrl+] supervisor commands are intercepted before mapping. Starting a
supervisor command, stopping the CPU or changing the USB connection resets
partial ANSI state. Terminal output and Phase 10's WebSocket keyboard
handling are unchanged. Test the keys in an application configured for these
control bindings; individual CP/M programs can assign different meanings.

### 1. Prove loading without executing CP/M

Type **Ctrl+] then `l`**. Loading reports recovery, manifest validation,
load and verify progress every 4 KiB. Cancellation is checked every 256 bytes:
type `x` (plain or after Ctrl+]) to cancel; loss of the USB connection also
cancels at the next checkpoint. Failure/cancellation asserts reset, inhibits
SRAM reads, isolates the data paths and stops PWM. The first fault includes
phase, address, expected/actual bytes and error category.

Success says **65,536 bytes loaded and independently read back; CPU not
running**. Check RESET LOW before proceeding. One load attempt is allowed
per Pico boot; reboot after cancellation/failure or a stopped run instead
of reinitializing live storage queues. Initial journal recovery uses SDK
lockout with the already-running USB core.

### 2. Optional read-only Z80 disk qualification

After `l`, type **Ctrl+] then `t`**. The actual Z80 program selects drives
A-D and LBAs 0-4095, reads exactly 128 bytes per record using `INIR`, and
reports those bytes using `OTIR` on diagnostic port `0x06`. The Pico compares
them in order with the recovered XIP flash image. A PASS requires exactly
**2,097,152 matching bytes**, all 16,384 records, and the final success
report on port `0x05`. The test issues no disk write or flush commands.
It stops at the first mismatch, disk-reported failure, or five-minute timeout.

The byte offset in a mismatch identifies the drive (`offset / 524288`), LBA
within that drive (`(offset % 524288) / 128`) and byte within the record
(`offset % 128`). This is stronger than calling the C handlers directly,
but is not proof of journal durability, filesystem behavior or analogue
timing margins. Progress is printed every 64 KiB.

The diagnostic overwrites part of SRAM, so it cannot subsequently start
CP/M from that image. Use **Ctrl+] `r`**, reconnect if needed, then **Ctrl+]
`l`** to load CP/M again. Flash disk contents remain unchanged.

### 3. Start CP/M and prove it is alive

After a fresh successful `l`, type **Ctrl+] then `g`**. These are separate
milestones:

1. The supervisor reports reset released and the configured clock.
2. The Z80 BIOS reports cold entry (`OUT (05h),B0h`).
3. The BIOS reports handoff to CCP (`OUT (05h),C0h`); warm boot reports this too.
4. The **CP/M banner and `A>` prompt**, followed by successful commands, prove
   the console and operating system are functioning.

The first three are not an automatic CP/M PASS. Missing BIOS/handoff evidence
after ten seconds requests a safe stop; check that the matching BIOS package
was provisioned. Read-only disk testing and a real CP/M prompt remain distinct
acceptance results.

Start with `DIR`, `DIR B:`, `DIR C:` and `DIR D:`; check the distinct sentinels
listed below. Run `LS` and require a natural return through BIOS warm boot to
the prompt. Only then test copies/writes on disposable contents. The file
named `drive_d_blank.img` currently contains the documented `DOCTOR.COM`
sentinel; its historical filename is not evidence that it is empty.

The shared terminal queues hold 128 input and 512 output bytes. While a terminal
is connected, output remains buffered for Z80 throughput: the BIOS waits only
when the output queue is full, and USB output is pumped only when the CDC
transport has space. With no terminal connected, writes are discarded
immediately and transmit-ready stays asserted, so console output cannot stall
the application. Disconnecting clears pending output; reconnecting does not
replay it. Bytes already handed to USB at disconnect are in flight and are not
guaranteed. Intentional disconnected-output discards are not overflow errors.
CRLF input becomes one CP/M carriage return. Actual queue overflow is counted
and printed as FAIL; reduce paste rate and repeat any affected acceptance test.

To stop, use **Ctrl+] then `x`** and wait for **Disk quiescent; RESET held
LOW, PWM stopped**. Do not remove power or request reboot before that
confirmation. A fatal storage error instead holds reset and requires reboot
recovery; preserve the error log first.

**Firmware feature:** On explicit `l`, recover any valid journal and validate the boot manifest
and CRC32 while the CPU is held reset. Then initialize the assisted loader,
release reset, inject instructions to write SRAM, and compare every byte.
Reset again before normal execution. The Pico never drives addresses or SRAM
write control; BUSACK is not required for cold loading. Follow the
[injection protocol](../hardware/address-interface.md).
Once running, ports `0x10`-`0x14`
provide command/status, drive, 16-bit LBA, and 128-byte data transfers.
Reads are synchronous XIP copies; writes use the journaled core-1
service and BUSY/READY/ERROR behavior defined in the
[flash-storage architecture](../system/operation.md#63-onboard-flash-cpm-disk-storage).

Before erasing a target sector, the backend reads back and compares the
complete journal data sector and header page. A failed journal program
therefore leaves the old target intact and latches a storage error. Target
readback and header-clear checks remain required after programming.

**Application source:** [src/stage09_flash_storage/main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/main.c),
the [ANSI input mapper](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/ansi_input.c)
and its [interface](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/ansi_input.h),
with the shared [disk device](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/disk_device.c),
[terminal queues](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_queue.c),
[flash backend](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/flash_backend.c), and
[flash layout](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/flash_layout.h).

## Flash Disk Image Loader (Final Phase 9 Integration)

**Maintained source:** [flash_disk.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/flash_disk.h),
[flash_layout.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/flash_layout.h),
[disk_device.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/disk_device.c), and
[flash_backend.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/flash_backend.c).

Loading a boot image is now a synchronous, core-0-only operation: a
flash read is an ordinary memory access through the XIP-mapped pointer
described in the
[flash-storage architecture](../system/operation.md#63-onboard-flash-cpm-disk-storage),
so bringing the initial image into SRAM needs no
filesystem, blocking I/O call, or core 1 task. Only CP/M's live
disk-sector *writes* still need to run on
core 1 and cross back to core 0's foreground loop, because only they
need to freeze the Z80 around a flash erase/program cycle
using those same [storage ownership rules](../system/operation.md#63-onboard-flash-cpm-disk-storage).

### Maintained Source

These complete files are included from the repository at documentation build
time, not copied into this page.

??? example "Phase 9 application - src/stage09_flash_storage/main.c"

    ```c
    {% include "../../../../src/stage09_flash_storage/main.c" %}
    ```

??? example "ANSI keyboard mapper - src/stage09_flash_storage/ansi_input.c"

    ```c
    {% include "../../../../src/stage09_flash_storage/ansi_input.c" %}
    ```

??? example "Flash geometry and boot manifest - src/common/include/z80sbc/flash_layout.h"

    ```c
    {% include "../../../../src/common/include/z80sbc/flash_layout.h" %}
    ```

??? example "Shared USB/WebSocket terminal queues - src/common/terminal_queue.c"

    ```c
    {% include "../../../../src/common/terminal_queue.c" %}
    ```

??? example "Read-only all-record Z80 program - src/stage09_flash_storage/disk_diagnostic.asm"

    ```asm
    {% include "../../../../src/stage09_flash_storage/disk_diagnostic.asm" %}
    ```

??? example "Disk protocol and core-1 service - src/common/disk_device.c"

    ```c
    {% include "../../../../src/common/disk_device.c" %}
    ```

??? example "Boot loading, cache, journal and bus quiescence - src/common/flash_backend.c"

    ```c
    {% include "../../../../src/common/flash_backend.c" %}
    ```

The Z80 BIOS sets drive and 16-bit LBA, then writes command 1 for a read.
Write commands 2, 3, and 4 mean normal, directory, and first record of a newly
allocated CP/M block respectively; command 5 explicitly flushes the cache.
A read returns 128 bytes from the data port. A write accepts exactly 128 bytes
there, then changes status to BUSY until core 1 has cached it and completed
any required journaled flush. Command 0 clears a transient protocol/queue
error when not busy; a flash or journal failure remains latched until reboot
and recovery, disabling both queued writes and background flush retries.
BUSY is published before enqueueing so immediate core-1 completion cannot
be overwritten by a late BUSY store. The BIOS polls READY/DATA_READY/DATA_ROOM/BUSY instead of
assuming Pico timing, but reads each status only once per poll and uses Z80
`INIR`/`OTIR` for payload transfer. It also waits for READY before writing a
command's drive/LBA registers. Idle flushes are serialized separately by
holding BUSACK# from before the flash operation until dirty state is cleared.
No erase, program, blocking queue, or flash-safe call runs inside
`io_trap_handler()`.

### Native CP/M auto-run

The native image builder assembles
[`autorun.asm`](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/autorun.asm)
and replaces the Altair-only `AUTORUN.COM` in user 0 of generated drive A.
Building images requires Python 3.10+, `z80asm`, and `cpmtools` (`cpmcp` and
`cpmrm`). Install a matching boot package and native drive A image for this
feature; a utility-only copy does not update the BIOS.

Run `AUTORUN`, choose `1` for cold-only, `2` for warm-only, `3` for both, or
`4` to disable, and enter a printable command of up to 80 characters, such
as `C:DATE`. RETURN keeps the current value; `Q` at the option prompt quits
without saving. The configuration always belongs to drive A, even if the
utility is invoked from another drive.

Settings occupy only drive A's last reserved system record: zero-based
track 1, sector 31, LBA 63, image offset `0x1F80`. The 128-byte `ZAR1` record
contains a boot-mode byte, command length/text, and additive checksum. It
does not overlap boot code or CP/M files. Freshly generated drive A images
initialize it disabled, so whole-image replacement resets the setting.
The utility requests a directory-type BIOS write (`C=1`) for journaled
durability and verifies all 128 bytes by reading them back before reporting
success. The BIOS validates the record on cold and warm boot, reports
configuration errors rather than executing invalid data, and copies an
applicable command into the CCP input buffer. Cold-only commands do not
repeat after a transient warm-boots. Warm modes intentionally execute on
every actual BIOS warm boot.

**Recovery:** a pending console key bypasses an applicable auto-run command
for that boot. The key is consumed; after reaching the prompt, run
`AUTORUN` and select `4` to disable a repeating command. Disabled and
inapplicable modes leave pending console input alone.

The directory buffer uses the optimized BDOS padding at `FC80h-FCFFh`;
the builder guards against overlap, and the TPA remains unchanged. The
independent host-core tests execute the real utility and BIOS, exercise all
four modes and the recovery bypass, inject read/write/verification errors,
and compare all disk bytes to prove that configuration changes touch only
the reserved record. These are emulator checks, not physical flash or
20 MHz timing qualification.

**Test plan:**

1. <input type="checkbox" data-checklist-id="phase8-regression" aria-label="Phase 8 IN and OUT regression tests passed unchanged"> With the Z80 socket populated and the
  [Phase 8 gate](phase-8-virtual-io.md#pass-gate) passing, confirm
  `io_trap_handler()` still passes every Phase 8 IN/OUT test unchanged;
  Phase 9 adds no new pins or rework to disturb it.
2. <input type="checkbox" data-checklist-id="linker-boundary" aria-label="Linker boundary and physical flash size checked"> Confirm the link fails if code crosses `0x10290000`, while a runtime
  print/static assertion still reports the physical C macro as 4 MiB.
3. <input type="checkbox" data-checklist-id="image-crcs-sentinels" aria-label="All provisioned image boundary pages CRCs and distinct drive sentinels verified"> Provision the boot package and four exact-size disk images with
  `picotool -v`; read every region's first and last page back and compare
  host CRC32 values. Give every image a distinct known directory sentinel
  (`DUMP.COM` on A, `CC2.COM` on B, `ATTNC11.COM` on C, and `DOCTOR.COM`
  on D) so a valid but aliased drive mapping cannot pass.
4. <input type="checkbox" data-checklist-id="cold-boot-failures" aria-label="Cold boot injection recovery and separate boot corruption faults verified"> Cold-boot with RESET# initially LOW. Require journal recovery and manifest
  validation before releasing reset for injected loading. Capture the Z80
  writing and verifying SRAM under slow clocks without waiting for BUSACK#.
  After verification, require a new reset to PC zero before normal execution.
  Corrupt the manifest, payload, and SRAM readback separately and require
  each case to remain fail-closed.
5. <input type="checkbox" data-checklist-id="disk-port-routing" aria-label="Real Z80 disk and terminal port routing verified"> Exercise the decoded port path with real Z80 `IN`/`OUT` instructions,
  not direct calls to the C handlers. Prove that `0x10` selects only
  command/status, `0x11` only the drive, `0x12`/`0x13` the complete LBA,
  and `0x14` only record data; also prove that terminal ports `0x00`/`0x01`
  cannot intercept or alias any disk access.
6. <input type="checkbox" data-checklist-id="all-drive-lbas-errors" aria-label="Every drive and LBA transfers bounds busy queue and latched errors verified"> Exercise all 2,560 LBAs on every drive through ports `0x10`-`0x14`,
  alternating the selected drive between commands. Verify exact 128-byte
  transfers, distinct per-drive sentinel records, no cross-drive aliasing,
  invalid drive/LBA rejection, command
  while BUSY rejection, and test-injected queue-full/error clearing
  behavior. After an injected flash or journal failure, require every
  READ/WRITE command to retain READY|ERROR until reboot recovery.
7. <input type="checkbox" data-checklist-id="write-snapshot" aria-label="Queued writes preserve command-time drive LBA type and payload"> Start a write, then change the live drive and LBA registers before core 1
  services its queue. Verify the completed write uses the drive, LBA, write
  type, and 128-byte payload captured when the command began and changes no
  other record.
8. <input type="checkbox" data-checklist-id="cpm-filesystem-usb" aria-label="CPM filesystem acceptance completed with Phase 9 USB terminal"> Boot the packaged image using the Phase 9 USB terminal and exercise the filesystem through CP/M itself:
  run `DIR` on A-D and require each sentinel on only its expected drive; run
  `LS` through natural transient exit and BIOS warm boot; run multi-extent
  `C:ATTNC11`; and use `PIP` to copy files across drives. Reboot and compare
  the affected records and directories byte-for-byte with the expected host
  images.
9. <input type="checkbox" data-checklist-id="flash-ownership-captures" aria-label="Flash ownership trap rearming and BUSACK race captures verified"> Use [DSLogic Group D](../hardware/logic-analyzer.md#group-d-sram-boot-inhibit-and-control-propagation)
  during a write to prove BUSREQ#/BUSACK#/CLK ownership and SRAM-control
  propagation. Repeat with [Group C](../hardware/logic-analyzer.md#group-c-trapped-io-and-data-path-exclusion)
  to prove the trap remains armed until BUSACK# is LOW and is armed again
  before BUSACK# returns HIGH, with no untrapped I/O edge in either interval.
  Inject an IORQ# falling edge while BUSACK# is LOW and require the handler
  to disable the IRQ without touching CLK or either bus.
10. <input type="checkbox" data-checklist-id="journal-power-cuts" aria-label="All six journal power-cut recovery cases verified"> Add test-only power-cut hooks after journal-data program, header
  program, target erase, partial target program, target verification,
  and header clear. Reboot after every hook and require recovery to
  produce either the complete old block (before valid header) or the
  complete new block (after valid header), never a mixture.
11. <input type="checkbox" data-checklist-id="network-storage-stage10" aria-label="Storage tested in every Wi-Fi state with Phase 10"> Repeat reads and writes with Wi-Fi absent, associating, connected,
  and reconnecting. Disk completion must not depend on network state,
  and WebSocket queue overflow must remain counted rather than block.
12. <input type="checkbox" data-checklist-id="journal-rotation-smoke" aria-label="Journal rotation and endurance smoke test recorded"> Rewrite hot directory blocks repeatedly while tracking journal-pair
  rotation and the flash part's rated erase endurance. Treat this as a
  smoke test, not proof of lifetime.
13. <input type="checkbox" data-checklist-id="safe-execute-faults" aria-label="Safe-execute entry and exit failure recovery verified"> Inject safe-execute entry and exit failures. Require RESET# LOW,
  isolated buses, stopped CLK, and a watchdog reboot without waiting on
  the core-0 release queue; recovery must retain the verified old or new
  disk block.

Phase 9 **Ctrl+] then `1` through `6`** arms the six power-cut points
in test 10; **Ctrl+] then `7` or `8`** arms safe-execute entry and exit failure in
test 13. Each key arms one watchdog reset for the next CP/M write or flush.
Before arming, preserve host copies of the old and intended new 4 KiB block;
after reboot, read back and compare the complete block before proceeding to
the next injection point.

## Bring-up checkpoint

Before adding the Phase 10 network transport:

- <input type="checkbox" data-checklist-id="checkpoint-provisioning" aria-label="Phase 9 checkpoint provisioning verified"> Require verified provisioning.
- <input type="checkbox" data-checklist-id="checkpoint-boot-recovery" aria-label="Phase 9 checkpoint journal recovery and full SRAM boot verification passed"> Require successful journal recovery and full SRAM boot-image verification.
- <input type="checkbox" data-checklist-id="checkpoint-status" aria-label="Phase 9 checkpoint has no fatal storage status"> Require no fatal storage status.
- <input type="checkbox" data-checklist-id="checkpoint-phase8" aria-label="Phase 9 checkpoint Phase 8 bus and IO measurements still pass"> Require the Phase 8 bus/I/O measurements still passing.
- <input type="checkbox" data-checklist-id="checkpoint-usb" aria-label="Phase 9 USB CPM prompt directory transient and warm boot verified"> Require the USB CP/M prompt, per-drive directories, transient execution and warm boot.
- <input type="checkbox" data-checklist-id="checkpoint-disk-reader" aria-label="Read-only Z80 all-record disk diagnostic verified"> Record the read-only all-record Z80 diagnostic separately from CP/M filesystem tests.
- <input type="checkbox" data-checklist-id="checkpoint-outstanding" aria-label="Outstanding network and fault tests recorded"> Record network and any fault-injection cases that remain outstanding.

Proceeding to Phase 10 enables network concurrency tests; it does not mark
them passed or replace the USB baseline.

## Host validation versus bench evidence

For fresh-checkout prerequisites, native CMake configuration, emulator
environment setup and real Pico builds in one place, see
[Building and Testing Pico C Firmware](../system/firmware-testing.md).

The control suite now executes the actual flash backend with modeled flash
erase/program operations, not just disk-device stubs. It covers exact image
shape, CRCs, cancellation, first SRAM faults, journal staging verification,
all eight watchdog fault points, recovery, cache behavior, rotation and bounds.
The Phase 9 controller tests use the real terminal queues and check framed
commands, a 20 MHz start request, backpressure, overflow, startup reports
and safe stop/flush interleavings. The automatic-loader build runs the same
terminal regressions plus the real `main` initialization path with USB absent,
deferred CP/M output, failed loading/startup, late failure reporting and no
automatic retry.

With the existing independent-core environment configured, the suite also
executes the real packaged BIOS/CP/M through cold boot, all-drive directory
reads and transient/warm boot, plus the complete read-only disk diagnostic:

```sh
cmake --build build/host-control --target flash_backend_test stage09_test stage09_autoboot_test disk_test
ctest --test-dir build/host-control \
  -R '^(flash_backend_recovery|stage09_usb_cpm|stage09_autoboot_cpm|disk_protocol|cpm_bios_z80_execution)$' \
  --output-on-failure
python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
```

Use Python 3.11 or newer for the image tests. The independent-core test
requires the existing `SOAK_EMULATOR_PYTHON` CMake setting and pinned
`src/common/tests/requirements-soak.txt`; it does not prove physical
20 MHz I/O, setup/hold margins or real flash endurance. Bench checklist
items remain unticked until measured.

## Pass gate

Final storage acceptance, including the tests completed with Phase 10:

- <input type="checkbox" data-checklist-id="gate-crcs" aria-label="Phase 9 boot and four disk CRCs match host images"> Boot and all four disks match host CRC32 values.
- <input type="checkbox" data-checklist-id="gate-sentinels" aria-label="Phase 9 all four drive sentinels have no aliasing"> A-D expose their distinct expected sentinel files with no aliasing.
- <input type="checkbox" data-checklist-id="gate-cpm" aria-label="Phase 9 complete CP/M filesystem acceptance passed"> Real CP/M `DIR`, transient execution, warm boot, multi-extent loading, and cross-drive copy complete through the BIOS.
- <input type="checkbox" data-checklist-id="gate-snapshot" aria-label="Phase 9 queued write snapshots passed"> Queued writes retain their command-time drive/LBA snapshot.
- <input type="checkbox" data-checklist-id="gate-recovery" aria-label="Phase 9 every fault reboot recovers an intact old or new block"> Every fault-injection reboot recovers an intact old or new block.
- <input type="checkbox" data-checklist-id="gate-fail-closed" aria-label="Phase 9 bounds and manifest failures remain fail closed"> All bounds and manifest failures remain fail-closed.
- <input type="checkbox" data-checklist-id="gate-offline" aria-label="Phase 9 disk service works without Wi-Fi"> Disk service works without Wi-Fi.
- <input type="checkbox" data-checklist-id="gate-linker" aria-label="Phase 9 linker protects storage boundary"> The linker protects the storage boundary.
- <input type="checkbox" data-checklist-id="gate-captures" aria-label="Phase 9 Group C and D captures show no untrapped cycle around writes"> The required DSLogic Group C/D captures show no untrapped Z80 cycle around a flash write.

</div>
