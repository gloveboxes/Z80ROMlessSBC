#include "flash_backend.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/flash_layout.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/pins.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

enum {
  FLASH_JOURNAL_MAGIC = 0x314C4E4Au,
  FLASH_JOURNAL_PAIR_BYTES = 2u * FLASH_SECTOR_SIZE,
  FLASH_JOURNAL_PAIR_COUNT =
      Z80_FLASH_JOURNAL_BYTES / FLASH_JOURNAL_PAIR_BYTES,
  FLASH_BOOT_PAYLOAD_OFFSET = Z80_FLASH_BOOT_OFFSET + FLASH_SECTOR_SIZE,
  DISK_CACHE_IDLE_FLUSH_MS = 250,
  Z80_BOOT_MAGIC = 0x5442385Au,
  Z80_BOOT_VERSION = 1u,
  FLASH_SERVICE_ACQUIRE_BUS,
  FLASH_SERVICE_RELEASE_BUS,
};

_Static_assert(PICO_FLASH_SIZE_BYTES == 4u * 1024u * 1024u,
               "Pico 2 W physical flash size changed");
_Static_assert(Z80_FLASH_JOURNAL_OFFSET == Z80_FLASH_LINK_LIMIT,
               "journal must start at the firmware link boundary");
_Static_assert(Z80_FLASH_DISK_SLOT_BYTES ==
                   Z80_FLASH_RECORD_BYTES * Z80_FLASH_RECORD_COUNT,
               "disk geometry does not fill its slot");
_Static_assert(FLASH_BOOT_PAYLOAD_OFFSET + 65536u <=
                   Z80_FLASH_BOOT_OFFSET + Z80_FLASH_BOOT_BYTES,
               "boot payload exceeds its region");
_Static_assert(Z80_FLASH_DISK_OFFSET +
                       Z80_FLASH_DISK_SLOT_COUNT * Z80_FLASH_DISK_SLOT_BYTES ==
                   PICO_FLASH_SIZE_BYTES,
               "disk slots must end at physical flash boundary");
_Static_assert(FLASH_JOURNAL_PAIR_COUNT == 8,
               "journal layout changed unexpectedly");
_Static_assert(PICO_FLASH_ASSUME_CORE1_SAFE,
               "pre-launch recovery requires core 1 safe assumption");

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t header_bytes;
  uint32_t image_bytes;
  uint32_t image_crc32;
  uint32_t header_crc32;
} boot_manifest_t;

typedef struct {
  uint32_t magic;
  uint32_t sequence;
  uint32_t target_offset;
  uint32_t data_crc32;
  uint32_t header_crc32;
  uint8_t reserved[FLASH_PAGE_SIZE - 20u];
} journal_header_t;

typedef struct {
  uint32_t header_offset;
  uint32_t data_offset;
  uint32_t target_offset;
  uint8_t *data;
  journal_header_t header;
  bool committed;
} write_params_t;

typedef struct {
  uint32_t header_offset;
  uint32_t target_offset;
  uint8_t *data;
  bool restored;
} restore_params_t;

typedef struct {
  uint8_t operation;
} service_request_t;

typedef struct {
  bool valid;
  bool dirty;
  unsigned int drive;
  uint32_t block_offset;
  uint8_t data[FLASH_SECTOR_SIZE];
} disk_cache_t;

_Static_assert(sizeof(boot_manifest_t) == 20,
               "boot manifest must match host packer");
_Static_assert(sizeof(journal_header_t) == FLASH_PAGE_SIZE,
               "journal header must occupy one flash page");

static queue_t service_request_queue;
static queue_t service_result_queue;
static disk_cache_t disk_cache;
static uint32_t journal_sequence;
static unsigned int next_journal_pair;
static absolute_time_t disk_cache_flush_deadline;
static uint32_t armed_fault;

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (unsigned int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^
            (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
  }
  return ~crc;
}

static bool disk_block_offset_valid(uint32_t offset) {
  uint32_t disk_end = Z80_FLASH_DISK_OFFSET +
                      Z80_FLASH_DISK_SLOT_COUNT * Z80_FLASH_DISK_SLOT_BYTES;
  return offset >= Z80_FLASH_DISK_OFFSET &&
         offset <= disk_end - FLASH_SECTOR_SIZE &&
         (offset & (FLASH_SECTOR_SIZE - 1u)) == 0;
}

static _Noreturn void reboot_after_flash_failure(void) {
  gpio_put(PIN_RESET_N, 0);
  z80_isolate_buses();
  z80_clock_stop();
  watchdog_reboot(0, 0, 0);
  while (true)
    tight_loop_contents();
}

static bool consume_fault(z80_flash_fault_point_t point) {
  uint32_t expected = (uint32_t)point;
  return __atomic_compare_exchange_n(&armed_fault, &expected,
                                     Z80_FLASH_FAULT_NONE, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void inject_power_cut(z80_flash_fault_point_t point) {
  if (consume_fault(point))
    reboot_after_flash_failure();
}

static void write_callback(void *parameter) {
  write_params_t *params = parameter;
  flash_range_erase(params->header_offset, FLASH_JOURNAL_PAIR_BYTES);
  flash_range_program(params->data_offset, params->data, FLASH_SECTOR_SIZE);
  inject_power_cut(Z80_FLASH_FAULT_AFTER_JOURNAL_DATA);
  flash_range_program(params->header_offset,
                      (const uint8_t *)&params->header, FLASH_PAGE_SIZE);
  inject_power_cut(Z80_FLASH_FAULT_AFTER_JOURNAL_HEADER);
  flash_range_erase(params->target_offset, FLASH_SECTOR_SIZE);
  inject_power_cut(Z80_FLASH_FAULT_AFTER_TARGET_ERASE);
  flash_range_program(params->target_offset, params->data,
                      FLASH_SECTOR_SIZE / 2u);
  inject_power_cut(Z80_FLASH_FAULT_AFTER_PARTIAL_TARGET);
  flash_range_program(params->target_offset + FLASH_SECTOR_SIZE / 2u,
                      params->data + FLASH_SECTOR_SIZE / 2u,
                      FLASH_SECTOR_SIZE / 2u);

  params->committed =
      memcmp((const void *)(XIP_BASE + params->target_offset), params->data,
             FLASH_SECTOR_SIZE) == 0;
  if (params->committed) {
    inject_power_cut(Z80_FLASH_FAULT_AFTER_TARGET_VERIFY);
    flash_range_erase(params->header_offset, FLASH_SECTOR_SIZE);
    params->committed =
        *(const uint32_t *)(XIP_BASE + params->header_offset) == UINT32_MAX;
    if (params->committed)
      inject_power_cut(Z80_FLASH_FAULT_AFTER_HEADER_CLEAR);
  }
}

static void restore_callback(void *parameter) {
  restore_params_t *params = parameter;
  flash_range_erase(params->target_offset, FLASH_SECTOR_SIZE);
  flash_range_program(params->target_offset, params->data, FLASH_SECTOR_SIZE);
  params->restored =
      memcmp((const void *)(XIP_BASE + params->target_offset), params->data,
             FLASH_SECTOR_SIZE) == 0;
  if (params->restored) {
    flash_range_erase(params->header_offset, FLASH_SECTOR_SIZE);
    params->restored =
        *(const uint32_t *)(XIP_BASE + params->header_offset) == UINT32_MAX;
  }
}

static bool recover_journal(void) {
  static journal_header_t headers[FLASH_JOURNAL_PAIR_COUNT];
  static bool valid[FLASH_JOURNAL_PAIR_COUNT];
  static uint8_t recovery_block[FLASH_SECTOR_SIZE];

  for (unsigned int pair = 0; pair < FLASH_JOURNAL_PAIR_COUNT; ++pair) {
    uint32_t header_offset = Z80_FLASH_JOURNAL_OFFSET +
                             pair * FLASH_JOURNAL_PAIR_BYTES;
    memcpy(&headers[pair], (const void *)(XIP_BASE + header_offset),
           sizeof(headers[pair]));
    valid[pair] =
        headers[pair].magic == FLASH_JOURNAL_MAGIC &&
        headers[pair].header_crc32 ==
            crc32_bytes((const uint8_t *)&headers[pair],
                        offsetof(journal_header_t, header_crc32)) &&
        disk_block_offset_valid(headers[pair].target_offset);
  }

  for (unsigned int recovered = 0; recovered < FLASH_JOURNAL_PAIR_COUNT;
       ++recovered) {
    int selected = -1;
    for (unsigned int pair = 0; pair < FLASH_JOURNAL_PAIR_COUNT; ++pair) {
      if (valid[pair] &&
          (selected < 0 || headers[pair].sequence <
                               headers[(unsigned int)selected].sequence))
        selected = (int)pair;
    }
    if (selected < 0)
      break;

    unsigned int pair = (unsigned int)selected;
    uint32_t header_offset = Z80_FLASH_JOURNAL_OFFSET +
                             pair * FLASH_JOURNAL_PAIR_BYTES;
    uint32_t data_offset = header_offset + FLASH_SECTOR_SIZE;
    memcpy(recovery_block, (const void *)(XIP_BASE + data_offset),
           sizeof(recovery_block));
    if (crc32_bytes(recovery_block, sizeof(recovery_block)) !=
        headers[pair].data_crc32)
      return false;

    restore_params_t params = {
      header_offset, headers[pair].target_offset, recovery_block, false,
    };
    int result = flash_safe_execute(restore_callback, &params, 1000);
    if (result != PICO_OK || !params.restored)
      return false;
    valid[pair] = false;
  }
  return true;
}

static bool load_boot_image(void) {
  const boot_manifest_t *manifest =
      (const boot_manifest_t *)(XIP_BASE + Z80_FLASH_BOOT_OFFSET);
  const uint8_t *source =
      (const uint8_t *)(XIP_BASE + FLASH_BOOT_PAYLOAD_OFFSET);

  if (manifest->magic != Z80_BOOT_MAGIC ||
      manifest->version != Z80_BOOT_VERSION ||
      manifest->header_bytes != sizeof(*manifest) ||
      manifest->image_bytes == 0 || manifest->image_bytes > 65536u ||
      crc32_bytes((const uint8_t *)manifest,
                  offsetof(boot_manifest_t, header_crc32)) !=
          manifest->header_crc32 ||
      crc32_bytes(source, manifest->image_bytes) != manifest->image_crc32 ||
      !z80_cpu_prepare_reset_dma())
    return false;

  return z80_sram_load(0, source, manifest->image_bytes) &&
         z80_sram_verify(0, source, manifest->image_bytes);
}

static bool request_core0(uint8_t operation) {
  service_request_t request = {operation};
  queue_add_blocking(&service_request_queue, &request);
  bool result = false;
  queue_remove_blocking(&service_result_queue, &result);
  return result;
}

void z80_flash_backend_core0_service(void) {
  service_request_t request;
  if (!queue_try_remove(&service_request_queue, &request))
    return;

  bool result = false;
  if (request.operation == FLASH_SERVICE_ACQUIRE_BUS) {
    result = z80_cpu_request_bus(500000);
    if (result)
      z80_io_trap_disable();
  } else if (request.operation == FLASH_SERVICE_RELEASE_BUS) {
    z80_io_trap_rearm();
    result = z80_cpu_release_bus(500000);
    if (!result)
      z80_io_trap_disable();
  }

  if (!queue_try_add(&service_result_queue, &result))
    reboot_after_flash_failure();
}

static bool flush_disk_cache(void) {
  if (!disk_cache.dirty)
    return true;

  uint32_t flash_offset = Z80_FLASH_DISK_OFFSET +
                          disk_cache.drive * Z80_FLASH_DISK_SLOT_BYTES +
                          disk_cache.block_offset;
  if (!request_core0(FLASH_SERVICE_ACQUIRE_BUS))
    return false;

  unsigned int pair =
      next_journal_pair++ % FLASH_JOURNAL_PAIR_COUNT;
  uint32_t header_offset = Z80_FLASH_JOURNAL_OFFSET +
                           pair * FLASH_JOURNAL_PAIR_BYTES;
  write_params_t params;
  memset(&params, 0, sizeof(params));
  params.header_offset = header_offset;
  params.data_offset = header_offset + FLASH_SECTOR_SIZE;
  params.target_offset = flash_offset;
    params.data = disk_cache.data;
  memset(&params.header, 0xFF, sizeof(params.header));
  params.header.magic = FLASH_JOURNAL_MAGIC;
    params.header.sequence = ++journal_sequence;
  params.header.target_offset = flash_offset;
    params.header.data_crc32 =
      crc32_bytes(disk_cache.data, sizeof(disk_cache.data));
  params.header.header_crc32 =
      crc32_bytes((const uint8_t *)&params.header,
                  offsetof(journal_header_t, header_crc32));

  int result = consume_fault(Z80_FLASH_FAULT_SAFE_EXECUTE_ENTRY)
                   ? PICO_ERROR_TIMEOUT
                   : flash_safe_execute(write_callback, &params, 1000);
  if (consume_fault(Z80_FLASH_FAULT_SAFE_EXECUTE_EXIT))
    result = PICO_ERROR_TIMEOUT;
  if (result != PICO_OK)
    reboot_after_flash_failure();
  if (params.committed)
    disk_cache.dirty = false;
  bool released = request_core0(FLASH_SERVICE_RELEASE_BUS);
  return released && params.committed;
}

static bool select_disk_cache(unsigned int drive, uint16_t lba) {
  uint32_t sector_offset = (uint32_t)lba * Z80_FLASH_RECORD_BYTES;
  uint32_t block_offset =
      sector_offset & ~(uint32_t)(FLASH_SECTOR_SIZE - 1u);
  if (disk_cache.valid && disk_cache.drive == drive &&
      disk_cache.block_offset == block_offset)
    return true;
  if (!flush_disk_cache())
    return false;

  uint32_t flash_offset = Z80_FLASH_DISK_OFFSET +
                          drive * Z80_FLASH_DISK_SLOT_BYTES + block_offset;
  memcpy(disk_cache.data, (const void *)(XIP_BASE + flash_offset),
         sizeof(disk_cache.data));
  disk_cache.drive = drive;
  disk_cache.block_offset = block_offset;
  disk_cache.valid = true;
  return true;
}

bool z80_flash_backend_read_record(unsigned int drive, uint16_t lba,
                                   uint8_t *data, size_t length) {
  if (drive >= Z80_FLASH_DISK_SLOT_COUNT ||
      lba >= Z80_FLASH_RECORD_COUNT || data == NULL ||
      length != Z80_FLASH_RECORD_BYTES)
    return false;

  uint32_t sector_offset = (uint32_t)lba * Z80_FLASH_RECORD_BYTES;
  uint32_t block_offset =
      sector_offset & ~(uint32_t)(FLASH_SECTOR_SIZE - 1u);
  uint32_t within_block = sector_offset - block_offset;
  if (disk_cache.valid && disk_cache.drive == drive &&
      disk_cache.block_offset == block_offset) {
    memcpy(data, disk_cache.data + within_block, length);
  } else {
    uint32_t flash_offset = Z80_FLASH_DISK_OFFSET +
                            drive * Z80_FLASH_DISK_SLOT_BYTES + sector_offset;
    memcpy(data, (const void *)(XIP_BASE + flash_offset), length);
  }
  return true;
}

bool z80_flash_backend_write_record(unsigned int drive, uint16_t lba,
                                    const uint8_t *data, size_t length,
                                    uint8_t write_type) {
  if (drive >= Z80_FLASH_DISK_SLOT_COUNT ||
      lba >= Z80_FLASH_RECORD_COUNT || data == NULL ||
      length != Z80_FLASH_RECORD_BYTES || write_type > 2u ||
      !select_disk_cache(drive, lba))
    return false;

  uint32_t within_block =
      ((uint32_t)lba * Z80_FLASH_RECORD_BYTES) &
      (FLASH_SECTOR_SIZE - 1u);
  if (memcmp(disk_cache.data + within_block, data, length) != 0) {
    memcpy(disk_cache.data + within_block, data, length);
    disk_cache.dirty = true;
    disk_cache_flush_deadline = make_timeout_time_ms(DISK_CACHE_IDLE_FLUSH_MS);
  }
  return write_type == 1u ? flush_disk_cache() : true;
}

bool z80_flash_backend_flush(void) {
  return flush_disk_cache();
}

bool z80_flash_backend_flush_due(void) {
  return disk_cache.dirty && time_reached(disk_cache_flush_deadline);
}

bool z80_flash_backend_quiescent(void) {
  return !disk_cache.dirty;
}

bool z80_flash_backend_init(void) {
  queue_init(&service_request_queue, sizeof(service_request_t), 4);
  queue_init(&service_result_queue, sizeof(bool), 4);
  if (!recover_journal() || !load_boot_image())
    return false;
  memset(&disk_cache, 0, sizeof(disk_cache));
  journal_sequence = 0;
  next_journal_pair = 0;
  disk_cache_flush_deadline = nil_time;
  __atomic_store_n(&armed_fault, Z80_FLASH_FAULT_NONE, __ATOMIC_RELEASE);
  return flash_safe_execute_core_init();
}

void z80_flash_backend_arm_fault(z80_flash_fault_point_t point) {
  __atomic_store_n(&armed_fault, (uint32_t)point, __ATOMIC_RELEASE);
}