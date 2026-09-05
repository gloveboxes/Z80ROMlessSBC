# 8.10 Phase 9 - Flash Disk Image Loader and CP/M Storage

**Prerequisite:** The [Phase 8 pass gate](phase-8-virtual-io.md#pass-gate) must pass.

**Install:** No hardware rework. Keep the board's physical
`PICO_FLASH_SIZE_BYTES` value at 4 MiB; the root `CMakeLists.txt` writes a
`pico_flash_region.ld` include that limits linked firmware to `0x290000`.
Define `PICO_FLASH_ASSUME_CORE1_SAFE=1` and link the libraries listed in
the [flash-storage architecture](../system/operation.md#63-onboard-flash-cpm-disk-storage).
Provision the manifest-backed boot package and all four 320 KiB disk slots
with the verified `picotool` commands there.

**Wiring:** Make no hardware changes. Keep the Phase 8 board intact and
recheck only the existing ownership and SRAM-control paths if the cold-boot
DMA checks fail.

**What you are proving:** persistent images survive reboot, load into SRAM
correctly, and recover safely from interrupted writes. SRAM is volatile
working memory; Pico flash holds the boot package and the four persistent
virtual disks. A **cold boot** starts the Pico and reloads SRAM; a CP/M
**warm boot** restarts its command environment without removing power.

The maintained build already sets the linker boundary and flash definitions
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

**Firmware feature:** With RESET# held LOW, recover any valid journal,
validate the boot manifest and CRC32, DMA-write its payload to SRAM,
and compare every byte before RESET# release. Do not wait for BUSACK#
during this cold-boot path: RESET# itself selects the Pico's SRAM
controls through the
[GAL equations](../hardware/pin-mapping.md#12-sram-control-source-arbitration-atf22v10bc).
Once running, ports `0x10`-`0x14`
provide command/status, drive, 16-bit LBA, and 128-byte data transfers.
Reads are synchronous XIP copies; writes use the journaled core-1
service and BUSY/READY/ERROR behavior defined in the
[flash-storage architecture](../system/operation.md#63-onboard-flash-cpm-disk-storage).

**Implementation:** [Phase 9 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/main.c),
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

```c
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/util/queue.h"

enum {
  FLASH_JOURNAL_BASE_OFFSET = 0x290000u,
  FLASH_JOURNAL_BYTES = 0x10000u,
  FLASH_JOURNAL_PAIR_BYTES = 2u * FLASH_SECTOR_SIZE,
  FLASH_JOURNAL_PAIR_COUNT = FLASH_JOURNAL_BYTES / FLASH_JOURNAL_PAIR_BYTES,
  FLASH_BOOT_BASE_OFFSET = 0x2A0000u,
  FLASH_BOOT_REGION_BYTES = 0x20000u,
  FLASH_BOOT_PAYLOAD_OFFSET = FLASH_BOOT_BASE_OFFSET + FLASH_SECTOR_SIZE,
  FLASH_DISK_BASE_OFFSET = 0x2C0000u,   // Section 6.3 partition table.
  FLASH_DISK_SLOT_BYTES = 0x50000u,     // 320 KiB per drive.
  FLASH_DISK_SLOT_COUNT = 4u,
  FLASH_DISK_RECORD_BYTES = 128u,
  FLASH_DISK_RECORD_COUNT = 2560u,
  SRAM_SIZE_BYTES = 65536
};

_Static_assert(PICO_FLASH_SIZE_BYTES == 4u * 1024u * 1024u,
  "Pico 2 W physical flash size changed");
_Static_assert(FLASH_DISK_SLOT_BYTES ==
  FLASH_DISK_RECORD_BYTES * FLASH_DISK_RECORD_COUNT,
  "disk geometry does not fill its slot");
_Static_assert(FLASH_BOOT_PAYLOAD_OFFSET + SRAM_SIZE_BYTES <=
  FLASH_BOOT_BASE_OFFSET + FLASH_BOOT_REGION_BYTES,
  "boot payload exceeds its reserved region");
_Static_assert(FLASH_DISK_BASE_OFFSET +
  FLASH_DISK_SLOT_COUNT * FLASH_DISK_SLOT_BYTES == PICO_FLASH_SIZE_BYTES,
  "disk slots must end at physical flash boundary");
_Static_assert(FLASH_JOURNAL_PAIR_COUNT == 8,
  "journal layout no longer matches the partition table");
_Static_assert(PICO_FLASH_ASSUME_CORE1_SAFE,
  "core 0 journal recovery runs only before core 1 is launched");

static const uint8_t *flash_disk_slot_ptr(unsigned drive) {
  uint32_t offset = FLASH_DISK_BASE_OFFSET + drive * FLASH_DISK_SLOT_BYTES;
  return (const uint8_t *)(XIP_BASE + offset);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
  }
  return ~crc;
}

enum { Z80_BOOT_MAGIC = 0x5442385Au, Z80_BOOT_VERSION = 1u }; // "Z8BT".

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t header_bytes;
  uint32_t image_bytes;
  uint32_t image_crc32;
  uint32_t header_crc32;
} z80_boot_manifest_t;

_Static_assert(sizeof(z80_boot_manifest_t) == 20,
  "boot manifest layout must match the host packer");

// Core 0 only, entirely synchronous: a flash read needs no filesystem,
// queue, or core 1 task.
static bool prepare_reset_held_dma(void) {
  isolate_buses();
  gpio_put(PIN_RESET_N, 0);
  for (unsigned int cycle = 0; cycle < 3; ++cycle)
    clock_one_cycle(1); // Bit-banged SIO pulses; set_z80_clock_hz() runs later.
  stop_z80_clock();

  if (!gpio_get(PIN_BUSACK_N) || !gpio_get(PIN_IORQ_N) ||
      !gpio_get(PIN_RD_N) || !gpio_get(PIN_WR_N))
    return false;

  return true;  // RESET# LOW selects the Pico SRAM controls (Section 1.2).
}

static bool boot_image_from_flash(void) {
  const z80_boot_manifest_t *manifest =
    (const z80_boot_manifest_t *)(XIP_BASE + FLASH_BOOT_BASE_OFFSET);
  const uint8_t *source =
    (const uint8_t *)(XIP_BASE + FLASH_BOOT_PAYLOAD_OFFSET);

  if (manifest->magic != Z80_BOOT_MAGIC ||
      manifest->version != Z80_BOOT_VERSION ||
      manifest->header_bytes != sizeof(*manifest) ||
      manifest->image_bytes == 0 || manifest->image_bytes > SRAM_SIZE_BYTES)
    return false;

  uint32_t header_crc = crc32_bytes((const uint8_t *)manifest,
    offsetof(z80_boot_manifest_t, header_crc32));
  if (header_crc != manifest->header_crc32 ||
      crc32_bytes(source, manifest->image_bytes) != manifest->image_crc32)
    return false;

  if (!prepare_reset_held_dma())
    return false;

  for (uint32_t i = 0; i < manifest->image_bytes; ++i)
    dma_write_byte((uint16_t)i, source[i]);

  bool ok = true;
  for (uint32_t i = 0; i < manifest->image_bytes; ++i) {
    if (dma_read_byte((uint16_t)i) != source[i]) {
      printf("boot verify failed near %04lx\n", (unsigned long)i);
      ok = false;
      break;
    }
  }

  if (ok)
    printf("loaded boot bytes=%lu crc32=%08lx\n",
      (unsigned long)manifest->image_bytes,
      (unsigned long)manifest->image_crc32);
  isolate_buses();
  // RESET# remains asserted; the caller releases it only after success.
  return ok;
}

static bool flash_recover_journal(void);

static bool boot_cpm_from_flash(void) {
  return flash_recover_journal() && boot_image_from_flash();
}

enum { FLASH_SERVICE_ACQUIRE_BUS, FLASH_SERVICE_RELEASE_BUS };

typedef struct {
  uint8_t operation;
} flash_service_request_t;

static queue_t flash_service_request_queue;  // Core 1 -> Core 0.
static queue_t flash_service_result_queue;   // Core 0 -> Core 1.

static void flash_service_queues_init(void) {
  queue_init(&flash_service_request_queue, sizeof(flash_service_request_t), 4);
  queue_init(&flash_service_result_queue, sizeof(bool), 4);
}

// Core 0's foreground loop only; never called from io_trap_handler().
static void core0_service_flash_requests(void) {
  flash_service_request_t request;
  if (!queue_try_remove(&flash_service_request_queue, &request))
    return;

  bool ok;
  switch (request.operation) {
    case FLASH_SERVICE_ACQUIRE_BUS:
      ok = request_cpu_bus(500000);
      if (ok)
        disable_io_trap();              // BUSACK# LOW: no new cycle can start.
      break;
    case FLASH_SERVICE_RELEASE_BUS:
      enable_io_trap();                 // Arm before BUSACK# lets the Z80 run.
      ok = release_cpu_bus(500000);
      if (!ok)
        disable_io_trap();              // release_cpu_bus() asserted RESET#.
      break;
    default:
      ok = false;
      break;
  }
  queue_add_blocking(&flash_service_result_queue, &ok);
}

// Core 1 only; blocks its caller, never core 0's foreground loop.
static bool core0_request(uint8_t operation) {
  flash_service_request_t request = { operation };
  queue_add_blocking(&flash_service_request_queue, &request);
  bool ok = false;
  queue_remove_blocking(&flash_service_result_queue, &ok);
  return ok;
}

enum { FLASH_JOURNAL_MAGIC = 0x314C4E4Au }; // "JNL1".

typedef struct {
  uint32_t magic;
  uint32_t sequence;
  uint32_t target_offset;
  uint32_t data_crc32;
  uint32_t header_crc32;
  uint8_t reserved[FLASH_PAGE_SIZE - 20u];
} flash_journal_header_t;

_Static_assert(sizeof(flash_journal_header_t) == FLASH_PAGE_SIZE,
  "journal header must occupy one flash page");

typedef struct {
  uint32_t header_offset;
  uint32_t data_offset;
  uint32_t target_offset;
  uint8_t *data;
  flash_journal_header_t header;
  bool committed;
} flash_write_params_t;

static bool flash_disk_block_offset_valid(uint32_t offset) {
  uint32_t disk_end = FLASH_DISK_BASE_OFFSET +
    FLASH_DISK_SLOT_COUNT * FLASH_DISK_SLOT_BYTES;
  return offset >= FLASH_DISK_BASE_OFFSET &&
    offset <= disk_end - FLASH_SECTOR_SIZE &&
    (offset & (FLASH_SECTOR_SIZE - 1u)) == 0;
}

static _Noreturn void reboot_after_flash_safe_failure(void) {
  gpio_put(PIN_RESET_N, 0);
  isolate_buses();
  stop_z80_clock();
  watchdog_reboot(0, 0, 0);
  while (true)
    tight_loop_contents();
}

static void flash_write_callback(void *param) {
  flash_write_params_t *p = (flash_write_params_t *)param;
  flash_range_erase(p->header_offset, FLASH_JOURNAL_PAIR_BYTES);
  flash_range_program(p->data_offset, p->data, FLASH_SECTOR_SIZE);
  flash_range_program(p->header_offset, (const uint8_t *)&p->header,
    FLASH_PAGE_SIZE);
  flash_range_erase(p->target_offset, FLASH_SECTOR_SIZE);
  flash_range_program(p->target_offset, p->data, FLASH_SECTOR_SIZE);

  p->committed = memcmp((const void *)(XIP_BASE + p->target_offset),
    p->data, FLASH_SECTOR_SIZE) == 0;
  if (p->committed) {
    flash_range_erase(p->header_offset, FLASH_SECTOR_SIZE);
    p->committed = *(const uint32_t *)(XIP_BASE + p->header_offset) ==
      UINT32_MAX;
  }
}

typedef struct {
  uint32_t header_offset;
  uint32_t target_offset;
  uint8_t *data;
  bool restored;
} flash_restore_params_t;

static void flash_restore_callback(void *param) {
  flash_restore_params_t *p = (flash_restore_params_t *)param;
  flash_range_erase(p->target_offset, FLASH_SECTOR_SIZE);
  flash_range_program(p->target_offset, p->data, FLASH_SECTOR_SIZE);
  p->restored = memcmp((const void *)(XIP_BASE + p->target_offset),
    p->data, FLASH_SECTOR_SIZE) == 0;
  if (p->restored) {
    flash_range_erase(p->header_offset, FLASH_SECTOR_SIZE);
    p->restored = *(const uint32_t *)(XIP_BASE + p->header_offset) ==
      UINT32_MAX;
  }
}

// Core 0 only, before core 1 launch and before RESET# release.
static bool flash_recover_journal(void) {
  static flash_journal_header_t headers[FLASH_JOURNAL_PAIR_COUNT];
  static bool valid[FLASH_JOURNAL_PAIR_COUNT];
  static uint8_t recovery_block[FLASH_SECTOR_SIZE];

  for (unsigned pair = 0; pair < FLASH_JOURNAL_PAIR_COUNT; ++pair) {
    uint32_t header_offset = FLASH_JOURNAL_BASE_OFFSET +
      pair * FLASH_JOURNAL_PAIR_BYTES;
    memcpy(&headers[pair], (const void *)(XIP_BASE + header_offset),
      sizeof headers[pair]);
    valid[pair] = headers[pair].magic == FLASH_JOURNAL_MAGIC &&
      headers[pair].header_crc32 == crc32_bytes((const uint8_t *)&headers[pair],
        offsetof(flash_journal_header_t, header_crc32)) &&
      flash_disk_block_offset_valid(headers[pair].target_offset);
  }

  for (unsigned recovered = 0; recovered < FLASH_JOURNAL_PAIR_COUNT;
      ++recovered) {
    int selected = -1;
    for (unsigned pair = 0; pair < FLASH_JOURNAL_PAIR_COUNT; ++pair) {
      if (valid[pair] && (selected < 0 || headers[pair].sequence <
          headers[(unsigned)selected].sequence))
        selected = (int)pair;
    }
    if (selected < 0)
      break;

    unsigned pair = (unsigned)selected;
    uint32_t header_offset = FLASH_JOURNAL_BASE_OFFSET +
      pair * FLASH_JOURNAL_PAIR_BYTES;
    uint32_t data_offset = header_offset + FLASH_SECTOR_SIZE;
    memcpy(recovery_block, (const void *)(XIP_BASE + data_offset),
      sizeof recovery_block);
    if (crc32_bytes(recovery_block, sizeof recovery_block) !=
        headers[pair].data_crc32)
      return false;

    flash_restore_params_t params = {
      header_offset, headers[pair].target_offset, recovery_block, false
    };
    int rc = flash_safe_execute(flash_restore_callback, &params, 1000);
    if (rc != PICO_OK || !params.restored)
      return false;
    valid[pair] = false;
  }
  return true;
}

typedef struct {
  bool valid;
  bool dirty;
  unsigned drive;
  uint32_t block_offset;
  uint8_t data[FLASH_SECTOR_SIZE];
} flash_disk_cache_t;

static flash_disk_cache_t disk_cache;
static absolute_time_t disk_cache_flush_deadline;

// Core 1 only. Commits the cached erase block, freezing the Z80 while
// flash_safe_execute() pauses XIP fetches on both cores (Section 6.3).
static bool flash_disk_flush_cache(void) {
  if (!disk_cache.dirty)
    return true;

  uint32_t flash_offset = FLASH_DISK_BASE_OFFSET +
    disk_cache.drive * FLASH_DISK_SLOT_BYTES + disk_cache.block_offset;

  if (!core0_request(FLASH_SERVICE_ACQUIRE_BUS))
    return false;

  static uint32_t sequence;
  static unsigned next_pair;
  unsigned pair = next_pair++ % FLASH_JOURNAL_PAIR_COUNT;
  uint32_t header_offset = FLASH_JOURNAL_BASE_OFFSET +
    pair * FLASH_JOURNAL_PAIR_BYTES;

  flash_write_params_t params;
  memset(&params, 0, sizeof params);
  params.header_offset = header_offset;
  params.data_offset = header_offset + FLASH_SECTOR_SIZE;
  params.target_offset = flash_offset;
  params.data = disk_cache.data;
  memset(&params.header, 0xFF, sizeof params.header);
  params.header.magic = FLASH_JOURNAL_MAGIC;
  params.header.sequence = ++sequence;
  params.header.target_offset = flash_offset;
  params.header.data_crc32 = crc32_bytes(disk_cache.data,
    sizeof disk_cache.data);
  params.header.header_crc32 = crc32_bytes((const uint8_t *)&params.header,
    offsetof(flash_journal_header_t, header_crc32));

  int rc = flash_safe_execute(flash_write_callback, &params, 1000);
  if (rc != PICO_OK)
    reboot_after_flash_safe_failure();
  if (params.committed)
    disk_cache.dirty = false;
  bool released = core0_request(FLASH_SERVICE_RELEASE_BUS);
  return released && params.committed;
}

static bool flash_disk_select_cache(unsigned drive, uint16_t lba) {
  uint32_t sector_offset = (uint32_t)lba * FLASH_DISK_RECORD_BYTES;
  uint32_t block_offset = sector_offset & ~(FLASH_SECTOR_SIZE - 1u);
  if (disk_cache.valid && disk_cache.drive == drive &&
      disk_cache.block_offset == block_offset)
    return true;
  if (!flash_disk_flush_cache())
    return false;

  uint32_t flash_offset = FLASH_DISK_BASE_OFFSET +
    drive * FLASH_DISK_SLOT_BYTES + block_offset;
  memcpy(disk_cache.data, (const void *)(XIP_BASE + flash_offset),
    sizeof disk_cache.data);
  disk_cache.drive = drive;
  disk_cache.block_offset = block_offset;
  disk_cache.valid = true;
  return true;
}

static bool flash_disk_write_record(unsigned drive, uint16_t lba,
    const uint8_t *data, uint8_t write_type) {
  if (!flash_disk_select_cache(drive, lba))
    return false;
  size_t offset = ((size_t)lba * FLASH_DISK_RECORD_BYTES) &
    (FLASH_SECTOR_SIZE - 1u);
  if (memcmp(disk_cache.data + offset, data, FLASH_DISK_RECORD_BYTES)) {
    memcpy(disk_cache.data + offset, data, FLASH_DISK_RECORD_BYTES);
    disk_cache.dirty = true;
    disk_cache_flush_deadline = make_timeout_time_ms(250);
  }
  return write_type == 1 ? flash_disk_flush_cache() : true;
}

static bool flash_disk_read_record(unsigned drive, uint16_t lba,
    uint8_t *data) {
  uint32_t sector_offset = (uint32_t)lba * FLASH_DISK_RECORD_BYTES;
  uint32_t block_offset = sector_offset & ~(FLASH_SECTOR_SIZE - 1u);
  uint32_t within_block = sector_offset - block_offset;
  if (disk_cache.valid && disk_cache.drive == drive &&
      disk_cache.block_offset == block_offset) {
    memcpy(data, disk_cache.data + within_block, FLASH_DISK_RECORD_BYTES);
  } else {
    uint32_t flash_offset = FLASH_DISK_BASE_OFFSET +
      drive * FLASH_DISK_SLOT_BYTES + sector_offset;
    memcpy(data, (const void *)(XIP_BASE + flash_offset),
      FLASH_DISK_RECORD_BYTES);
  }
  return true;
}

enum {
  DISK_COMMAND_STATUS_PORT = 0x10,
  DISK_DRIVE_PORT = 0x11,
  DISK_LBA_LOW_PORT = 0x12,
  DISK_LBA_HIGH_PORT = 0x13,
  DISK_DATA_PORT = 0x14,
  DISK_COMMAND_CLEAR = 0,
  DISK_COMMAND_READ = 1,
  DISK_COMMAND_WRITE_NORMAL = 2,
  DISK_COMMAND_WRITE_DIRECTORY = 3,
  DISK_COMMAND_WRITE_UNALLOCATED = 4,
  DISK_COMMAND_FLUSH = 5,
  DISK_STATUS_READY = 1u << 0,
  DISK_STATUS_DATA_READY = 1u << 1,
  DISK_STATUS_DATA_ROOM = 1u << 2,
  DISK_STATUS_BUSY = 1u << 3,
  DISK_STATUS_ERROR = 1u << 7,
  DISK_WRITE_QUEUE_DEPTH = 2
};

typedef struct {
  uint8_t command;
  uint8_t drive;
  uint16_t lba;
  uint8_t data[FLASH_DISK_RECORD_BYTES];
} disk_request_t;

static queue_t disk_write_queue;             // Z80/Core 0 -> Core 1.
static uint32_t disk_status = DISK_STATUS_READY;
static uint32_t disk_fatal_error;
static uint8_t disk_drive;
static uint16_t disk_lba;
static uint8_t disk_write_drive;   // Snapshot of drive/lba at WRITE issue,
static uint16_t disk_write_lba;    // immune to changes during data transfer.
static uint8_t disk_write_type;
static uint16_t disk_data_index;
static uint8_t disk_data[FLASH_DISK_RECORD_BYTES];

static uint32_t disk_status_load(void) {
  return __atomic_load_n(&disk_status, __ATOMIC_ACQUIRE);
}

static void disk_status_store(uint32_t status) {
  __atomic_store_n(&disk_status, status, __ATOMIC_RELEASE);
}

static bool disk_address_valid(void) {
  return disk_drive < FLASH_DISK_SLOT_COUNT &&
    disk_lba < FLASH_DISK_RECORD_COUNT;
}

static void disk_service_init(void) {
  queue_init(&disk_write_queue, sizeof(disk_request_t),
    DISK_WRITE_QUEUE_DEPTH);
  __atomic_store_n(&disk_fatal_error, 0, __ATOMIC_RELEASE);
  disk_status_store(DISK_STATUS_READY);
}

static void disk_start_command(uint8_t command) {
  if (disk_status_load() & DISK_STATUS_BUSY)
    return;

  bool fatal = __atomic_load_n(&disk_fatal_error, __ATOMIC_ACQUIRE);
  if (command == DISK_COMMAND_CLEAR) {
    disk_data_index = 0;
    disk_status_store(DISK_STATUS_READY | (fatal ? DISK_STATUS_ERROR : 0));
    return;
  }
  if (fatal) {
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }

  if (command == DISK_COMMAND_FLUSH) {
    disk_request_t request = { .command = command };
    if (queue_try_add(&disk_write_queue, &request))
      disk_status_store(DISK_STATUS_BUSY);
    else
      disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }
  if (!disk_address_valid()) {
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }

  disk_data_index = 0;
  if (command == DISK_COMMAND_READ) {
    bool ok = flash_disk_read_record(disk_drive, disk_lba, disk_data);
    disk_status_store(DISK_STATUS_READY |
      (ok ? DISK_STATUS_DATA_READY : DISK_STATUS_ERROR));
  } else if (command >= DISK_COMMAND_WRITE_NORMAL &&
      command <= DISK_COMMAND_WRITE_UNALLOCATED) {
    disk_write_drive = disk_drive;
    disk_write_lba = disk_lba;
    disk_write_type = command - DISK_COMMAND_WRITE_NORMAL;
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_DATA_ROOM);
  } else {
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
  }
}

static uint8_t disk_virtual_io_read(uint8_t port) {
  if (port == DISK_COMMAND_STATUS_PORT)
    return (uint8_t)disk_status_load();
  if (port != DISK_DATA_PORT ||
      !(disk_status_load() & DISK_STATUS_DATA_READY))
    return 0;

  uint8_t value = disk_data[disk_data_index++];
  if (disk_data_index == sizeof disk_data)
    disk_status_store(DISK_STATUS_READY);
  return value;
}

static void disk_virtual_io_write(uint8_t port, uint8_t value) {
  uint32_t status = disk_status_load();
  if (port == DISK_COMMAND_STATUS_PORT) {
    disk_start_command(value);
  } else if (status & DISK_STATUS_BUSY) {
    return;
  } else if (port == DISK_DRIVE_PORT) {
    disk_drive = value;
  } else if (port == DISK_LBA_LOW_PORT) {
    disk_lba = (uint16_t)((disk_lba & 0xFF00u) | value);
  } else if (port == DISK_LBA_HIGH_PORT) {
    disk_lba = (uint16_t)((disk_lba & 0x00FFu) | ((uint16_t)value << 8));
  } else if (port == DISK_DATA_PORT &&
      (status & DISK_STATUS_DATA_ROOM)) {
    disk_data[disk_data_index++] = value;
    if (disk_data_index == sizeof disk_data) {
      disk_request_t request = {
        .command = DISK_COMMAND_WRITE_NORMAL + disk_write_type,
        .drive = disk_write_drive,
        .lba = disk_write_lba
      };
      memcpy(request.data, disk_data, sizeof request.data);
      if (queue_try_add(&disk_write_queue, &request))
        disk_status_store(DISK_STATUS_BUSY);
      else
        disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    }
  }
}

// Core 1 only; call on every loop iteration, including while Wi-Fi is down.
static void core1_service_disk_request(void) {
  disk_request_t request;
  if (!queue_try_remove(&disk_write_queue, &request)) {
    if (!disk_cache.dirty || !time_reached(disk_cache_flush_deadline))
      return;
    bool ok = flash_disk_flush_cache();
    if (!ok)
      __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
    if (!ok)
      disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }

  bool ok = request.command == DISK_COMMAND_FLUSH
    ? flash_disk_flush_cache()
    : flash_disk_write_record(request.drive, request.lba, request.data,
        request.command - DISK_COMMAND_WRITE_NORMAL);
  if (!ok)
    __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
  disk_status_store(DISK_STATUS_READY | (ok ? 0 : DISK_STATUS_ERROR));
}
```

The Z80 BIOS sets drive and 16-bit LBA, then writes command 1 for a read.
Write commands 2, 3, and 4 mean normal, directory, and first record of a newly
allocated CP/M block respectively; command 5 explicitly flushes the cache.
A read returns 128 bytes from the data port. A write accepts exactly 128 bytes
there, then changes status to BUSY until core 1 has cached it and completed
any required journaled flush. Command 0 clears a transient protocol/queue
error when not busy; a flash or journal failure remains latched until reboot
and recovery. The BIOS polls READY/DATA_READY/DATA_ROOM/BUSY instead of
assuming Pico timing, but reads each status only once per poll and uses Z80
`INIR`/`OTIR` for payload transfer. It also waits for READY before writing a
command's drive/LBA registers. Idle flushes are serialized separately by
holding BUSACK# from before the flash operation until dirty state is cleared.
No erase, program, blocking queue, or flash-safe call runs inside
`io_trap_handler()`.

**Test plan:**

1. With the Z80 socket populated and the
  [Phase 8 gate](phase-8-virtual-io.md#pass-gate) passing, confirm
  `io_trap_handler()` still passes every Phase 8 IN/OUT test unchanged;
  Phase 9 adds no new pins or rework to disturb it.
2. Confirm the link fails if code crosses `0x10290000`, while a runtime
  print/static assertion still reports the physical C macro as 4 MiB.
3. Provision the boot package and four exact-size disk images with
  `picotool -v`; read every region's first and last page back and compare
  host CRC32 values. Give every image a distinct known directory sentinel
  (`DUMP.COM` on A, `CC2.COM` on B, `ATTNC11.COM` on C, and `DOCTOR.COM`
  on D) so a valid but aliased drive mapping cannot pass.
4. Cold-boot with RESET# held LOW. Require journal recovery and SRAM
  verification to finish without waiting for BUSACK#, then release
  RESET# only after success. Corrupt the manifest, payload, and SRAM
  readback separately and require each case to remain fail-closed.
5. Exercise the decoded port path with real Z80 `IN`/`OUT` instructions,
  not direct calls to the C handlers. Prove that `0x10` selects only
  command/status, `0x11` only the drive, `0x12`/`0x13` the complete LBA,
  and `0x14` only record data; also prove that terminal ports `0x00`/`0x01`
  cannot intercept or alias any disk access.
6. Exercise all 2,560 LBAs on every drive through ports `0x10`-`0x14`,
  alternating the selected drive between commands. Verify exact 128-byte
  transfers, distinct per-drive sentinel records, no cross-drive aliasing,
  invalid drive/LBA rejection, command
  while BUSY rejection, and test-injected queue-full/error clearing
  behavior. After an injected flash or journal failure, require every
  READ/WRITE command to retain READY|ERROR until reboot recovery.
7. Start a write, then change the live drive and LBA registers before core 1
  services its queue. Verify the completed write uses the drive, LBA, write
  type, and 128-byte payload captured when the command began and changes no
  other record.
8. Boot the packaged image and exercise the filesystem through CP/M itself:
  run `DIR` on A-D and require each sentinel on only its expected drive; run
  `LS` through natural transient exit and BIOS warm boot; run multi-extent
  `C:ATTNC11`; and use `PIP` to copy files across drives. Reboot and compare
  the affected records and directories byte-for-byte with the expected host
  images.
9. Use [DSLogic Group D](../hardware/logic-analyzer.md#group-d-sram-ownership-and-control-propagation)
  during a write to prove BUSREQ#/BUSACK#/CLK ownership and SRAM-control
  propagation. Repeat with [Group C](../hardware/logic-analyzer.md#group-c-trapped-io-and-data-path-interlock)
  to prove the trap remains armed until BUSACK# is LOW and is armed again
  before BUSACK# returns HIGH, with no untrapped I/O edge in either interval.
  Inject an IORQ# falling edge while BUSACK# is LOW and require the handler
  to disable the IRQ without touching CLK, SPI0, or either bus.
10. Add test-only power-cut hooks after journal-data program, header
  program, target erase, partial target program, target verification,
  and header clear. Reboot after every hook and require recovery to
  produce either the complete old block (before valid header) or the
  complete new block (after valid header), never a mixture.
11. Repeat reads and writes with Wi-Fi absent, associating, connected,
  and reconnecting. Disk completion must not depend on network state,
  and WebSocket queue overflow must remain counted rather than block.
12. Rewrite hot directory blocks repeatedly while tracking journal-pair
  rotation and the flash part's rated erase endurance. Treat this as a
  smoke test, not proof of lifetime.
13. Inject safe-execute entry and exit failures. Require RESET# LOW,
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

Before adding the Stage 10 terminal, require verified provisioning,
successful journal recovery and full SRAM boot-image verification, no fatal
storage status, and the Phase 8 bus/I/O measurements still passing. Record
the interactive, network, and fault-injection cases that remain outstanding.
Proceeding to Stage 10 enables those tests; it does not mark them passed.

## Pass gate

Final storage acceptance, including the tests completed with Stage 10:

Boot and all four disks match host CRC32 values; A-D expose
their distinct expected sentinel files with no aliasing; real CP/M `DIR`,
transient execution, warm boot, multi-extent loading, and cross-drive copy
complete through the BIOS; queued writes retain their command-time drive/LBA
snapshot; every fault-injection reboot recovers an intact old or new block;
all bounds and manifest failures remain fail-closed; disk service works
without Wi-Fi; the linker protects the storage boundary; and the required
DSLogic Group C/D captures show no untrapped Z80 cycle around a flash write.
