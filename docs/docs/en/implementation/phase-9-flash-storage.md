# 8.10 Phase 9 - Flash Disk Image Loader and CP/M Storage

<div data-checklist="phase-9" data-checklist-label="Phase 9" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.
Leave Stage 10-dependent tests unticked until completed with that terminal.
The bring-up checkpoint is not the final storage pass gate.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 9 prerequisite passed"> **Prerequisite:** The [Phase 8 pass gate](phase-8-virtual-io.md#pass-gate) must pass.

**Install:** No hardware rework. Keep the board's physical
`PICO_FLASH_SIZE_BYTES` value at 4 MiB; the root `CMakeLists.txt` writes a
`pico_flash_region.ld` include that limits linked firmware to `0x290000`.
Define `PICO_FLASH_ASSUME_CORE1_SAFE=1` and link the libraries listed in
the [flash-storage architecture](../system/operation.md#63-onboard-flash-cpm-disk-storage).
Provision the manifest-backed boot package and all four 320 KiB disk slots
with the verified `picotool` commands there.

**Wiring:** Make no hardware changes. Keep the Phase 8 board intact and
recheck boot inhibit, injected data, and CPU SRAM-control paths if cold-boot
loading checks fail.

**What you are proving:** persistent images survive reboot, load into SRAM
correctly, and recover safely from interrupted writes. SRAM is volatile
working memory; Pico flash holds the boot package and the four persistent
virtual disks. A **cold boot** starts the Pico and reloads SRAM; a CP/M
**warm boot** restarts its command environment without removing power.

<input type="checkbox" data-checklist-id="storage-provisioning" aria-label="Storage provisioned with host backups and Stage 9 firmware loaded"> The maintained build already sets the linker boundary and flash definitions
above; you do not need to edit CMake for normal construction. Use the
[provisioning procedure](../system/firmware-build.md#72-flash-provisioning)
and retain host backups. The full 4 MiB image includes Stage 10 firmware;
after initial provisioning, load the Stage 9 UF2 for this phase, preserving
the storage regions. Do not mistake the disk image files for Pico firmware.

!!! warning "Use disposable disk contents for fault tests"
    Complete normal boot/read/write checks before injecting faults. Tests
    that corrupt images, overwrite all records, or interrupt flash updates
    can destroy files. Preserve the original images and expected old/new
    blocks first. Use the documented one-shot watchdog hooks; do not simulate
    faults by pulling individual live signal wires or shorting pins.

**Fail-closed** means the supervisor deliberately holds the CPU in reset or
reboots into recovery instead of executing an unverified image. Capture the
USB error/status first. Do not bypass verification merely to get a prompt.

!!! note "Stage 9 is a disk-only diagnostic"
    Its stock application implements ports 0x10-0x14 and USB status/fault
    commands, but no CP/M terminal. Do not expect an interactive `A>` prompt
    over USB. The interactive filesystem and Wi-Fi cases below are storage
    acceptance requirements completed with the Stage 10 terminal. Other
    port/fault cases require the stated Z80 test program or test-only setup;
    a startup `PASS` line does not run them automatically.

**Firmware feature:** Recover any valid journal and validate the boot manifest
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

**Application source:** [src/stage09_flash_storage/main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/main.c),
with the shared [disk device](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/disk_device.c),
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

??? example "Stage 9 application - src/stage09_flash_storage/main.c"

    ```c
    {% include "../../../../src/stage09_flash_storage/main.c" %}
    ```

??? example "Flash geometry and boot manifest - src/common/include/z80sbc/flash_layout.h"

    ```c
    {% include "../../../../src/common/include/z80sbc/flash_layout.h" %}
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
8. <input type="checkbox" data-checklist-id="cpm-filesystem-stage10" aria-label="CPM filesystem acceptance completed with Stage 10 terminal"> Boot the packaged image and exercise the filesystem through CP/M itself:
  run `DIR` on A-D and require each sentinel on only its expected drive; run
  `LS` through natural transient exit and BIOS warm boot; run multi-extent
  `C:ATTNC11`; and use `PIP` to copy files across drives. Reboot and compare
  the affected records and directories byte-for-byte with the expected host
  images.
9. <input type="checkbox" data-checklist-id="flash-ownership-captures" aria-label="Flash ownership trap rearming and BUSACK race captures verified"> Use [DSLogic Group D](../hardware/logic-analyzer.md#group-d-sram-boot-inhibit-and-control-propagation)
  during a write to prove BUSREQ#/BUSACK#/CLK ownership and SRAM-control
  propagation. Repeat with [Group C](../hardware/logic-analyzer.md#group-c-trapped-io-and-data-path-interlock)
  to prove the trap remains armed until BUSACK# is LOW and is armed again
  before BUSACK# returns HIGH, with no untrapped I/O edge in either interval.
  Inject an IORQ# falling edge while BUSACK# is LOW and require the handler
  to disable the IRQ without touching CLK or either bus.
10. <input type="checkbox" data-checklist-id="journal-power-cuts" aria-label="All six journal power-cut recovery cases verified"> Add test-only power-cut hooks after journal-data program, header
  program, target erase, partial target program, target verification,
  and header clear. Reboot after every hook and require recovery to
  produce either the complete old block (before valid header) or the
  complete new block (after valid header), never a mixture.
11. <input type="checkbox" data-checklist-id="network-storage-stage10" aria-label="Storage tested in every Wi-Fi state with Stage 10"> Repeat reads and writes with Wi-Fi absent, associating, connected,
  and reconnecting. Disk completion must not depend on network state,
  and WebSocket queue overflow must remain counted rather than block.
12. <input type="checkbox" data-checklist-id="journal-rotation-smoke" aria-label="Journal rotation and endurance smoke test recorded"> Rewrite hot directory blocks repeatedly while tracking journal-pair
  rotation and the flash part's rated erase endurance. Treat this as a
  smoke test, not proof of lifetime.
13. <input type="checkbox" data-checklist-id="safe-execute-faults" aria-label="Safe-execute entry and exit failure recovery verified"> Inject safe-execute entry and exit failures. Require RESET# LOW,
  isolated buses, stopped CLK, and a watchdog reboot without waiting on
  the core-0 release queue; recovery must retain the verified old or new
  disk block.

Stage 9 USB diagnostic keys `1` through `6` arm the six power-cut points
in test 10; keys `7` and `8` arm safe-execute entry and exit failure in
test 13. Each key arms one watchdog reset for the next CP/M write or flush.
Before arming, preserve host copies of the old and intended new 4 KiB block;
after reboot, read back and compare the complete block before proceeding to
the next injection point.

## Bring-up checkpoint

Before adding the Stage 10 terminal:

- <input type="checkbox" data-checklist-id="checkpoint-provisioning" aria-label="Stage 9 checkpoint provisioning verified"> Require verified provisioning.
- <input type="checkbox" data-checklist-id="checkpoint-boot-recovery" aria-label="Stage 9 checkpoint journal recovery and full SRAM boot verification passed"> Require successful journal recovery and full SRAM boot-image verification.
- <input type="checkbox" data-checklist-id="checkpoint-status" aria-label="Stage 9 checkpoint has no fatal storage status"> Require no fatal storage status.
- <input type="checkbox" data-checklist-id="checkpoint-phase8" aria-label="Stage 9 checkpoint Phase 8 bus and IO measurements still pass"> Require the Phase 8 bus/I/O measurements still passing.
- <input type="checkbox" data-checklist-id="checkpoint-outstanding" aria-label="Outstanding interactive network and fault tests recorded"> Record the interactive, network, and fault-injection cases that remain outstanding.

Proceeding to Stage 10 enables those tests; it does not mark them passed.

## Pass gate

Final storage acceptance, including the tests completed with Stage 10:

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
