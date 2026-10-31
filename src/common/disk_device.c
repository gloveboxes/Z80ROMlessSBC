#include "z80sbc/flash_disk.h"

#include "pico/stdlib.h"
#include "z80sbc/flash_layout.h"

#include "flash_backend.h"

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
  RECORD_WORDS = Z80_FLASH_RECORD_BYTES / 4,
  PREFETCH_SLOTS = 8,
  PREFETCH_KEY_VALID = 1u << 31,
};
_Static_assert((PREFETCH_SLOTS & (PREFETCH_SLOTS - 1)) == 0,
               "prefetch slots are direct-mapped by LBA");

/* Single-slot core 0 -> core 1 mailbox. Core 0 stores BUSY, then publishes
 * a nonzero pending command (release). While BUSY, core 0 ignores every data,
 * address and command write, so core 1 owns disk_record and the write address
 * until it clears pending and then publishes READY. A record therefore
 * crosses cores without a copy or a queue lock. */
static uint32_t disk_pending;
/* Supervisor stop flush: separate from the Z80 command slot, so it can never
 * replace or be rejected by a Z80 command still queued in the PIO FIFO. */
static uint32_t supervisor_flush;
static uint32_t disk_status = DISK_STATUS_READY;
static uint32_t disk_fatal_error;
static uint32_t disk_write_enabled = 1;
static uint8_t disk_drive;
static uint16_t disk_lba;
static uint8_t disk_write_drive;
static uint16_t disk_write_lba;
static uint8_t disk_write_command;
static uint16_t disk_data_index;
static union {
  uint32_t words[RECORD_WORDS];
  uint8_t bytes[Z80_FLASH_RECORD_BYTES];
} disk_record;

/* Read-ahead keeps sequential CP/M reads out of XIP flash in the trap.
 * After each READ core 0 publishes a hint; core 1 copies the following
 * records into direct-mapped SRAM slots. Each slot is a seqlock with core 1
 * as the only writer: sequence is odd while it is rewritten. Core 0 copies
 * a matching slot with word loads and accepts it only if the sequence did
 * not change, otherwise it reads flash as before. Core 1 invalidates the
 * slot for every record it writes before publishing READY, so a slot never
 * bypasses newer data in the backend's write sector. */
typedef struct {
  uint32_t sequence;
  uint32_t key;
  uint32_t words[RECORD_WORDS];
} prefetch_slot_t;

static prefetch_slot_t prefetch[PREFETCH_SLOTS];
static uint32_t prefetch_hint;
static uint32_t prefetch_hits;
static uint32_t prefetch_misses;

static inline uint32_t record_key(unsigned int drive, uint16_t lba) {
  return PREFETCH_KEY_VALID | (uint32_t)drive << 16 | lba;
}

static bool __not_in_flash_func(prefetch_take)(uint32_t key, uint16_t lba) {
  prefetch_slot_t *slot = &prefetch[lba & (PREFETCH_SLOTS - 1)];
  uint32_t sequence = __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE);
  if ((sequence & 1u) ||
      __atomic_load_n(&slot->key, __ATOMIC_RELAXED) != key)
    return false;
  for (unsigned int index = 0; index < RECORD_WORDS; ++index)
    disk_record.words[index] =
        __atomic_load_n(&slot->words[index], __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  return __atomic_load_n(&slot->sequence, __ATOMIC_RELAXED) == sequence;
}

static void prefetch_publish(prefetch_slot_t *slot, uint32_t key,
                             const uint32_t *words) {
  uint32_t sequence = __atomic_load_n(&slot->sequence, __ATOMIC_RELAXED);
  __atomic_store_n(&slot->sequence, sequence + 1, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  __atomic_store_n(&slot->key, key, __ATOMIC_RELAXED);
  if (words != NULL)
    for (unsigned int index = 0; index < RECORD_WORDS; ++index)
      __atomic_store_n(&slot->words[index], words[index], __ATOMIC_RELAXED);
  __atomic_store_n(&slot->sequence, sequence + 2, __ATOMIC_RELEASE);
}

static void prefetch_invalidate(unsigned int drive, uint16_t lba) {
  prefetch_slot_t *slot = &prefetch[lba & (PREFETCH_SLOTS - 1)];
  if (__atomic_load_n(&slot->key, __ATOMIC_RELAXED) == record_key(drive, lba))
    prefetch_publish(slot, 0, NULL);
}

/* Bounded: at most PREFETCH_SLOTS record copies per call, usually one. */
static void prefetch_service(void) {
  uint32_t hint = __atomic_load_n(&prefetch_hint, __ATOMIC_ACQUIRE);
  if (hint == 0)
    return;
  unsigned int drive = (hint >> 16) & 0xFFu;
  uint32_t lba = hint & 0xFFFFu;
  for (uint32_t ahead = 1; ahead <= PREFETCH_SLOTS; ++ahead) {
    uint32_t next = lba + ahead;
    if (next >= Z80_FLASH_RECORD_COUNT)
      break;
    uint32_t key = record_key(drive, (uint16_t)next);
    prefetch_slot_t *slot = &prefetch[next & (PREFETCH_SLOTS - 1)];
    if (__atomic_load_n(&slot->key, __ATOMIC_RELAXED) == key)
      continue;
    uint32_t words[RECORD_WORDS];
    z80_flash_backend_read_record(drive, (uint16_t)next, (uint8_t *)words);
    prefetch_publish(slot, key, words);
  }
}

static uint32_t __not_in_flash_func(disk_status_load)(void) {
  return __atomic_load_n(&disk_status, __ATOMIC_ACQUIRE);
}

static void __not_in_flash_func(disk_status_store)(uint32_t value) {
  __atomic_store_n(&disk_status, value, __ATOMIC_RELEASE);
}

static void __not_in_flash_func(disk_submit)(uint8_t command) {
  disk_status_store(DISK_STATUS_BUSY);
  __atomic_store_n(&disk_pending, command, __ATOMIC_RELEASE);
}

static void __not_in_flash_func(disk_start_command)(uint8_t command) {
  if (disk_status_load() & DISK_STATUS_BUSY)
    return;

  bool fatal =
      __atomic_load_n(&disk_fatal_error, __ATOMIC_ACQUIRE) != 0;
  if (command == DISK_COMMAND_CLEAR) {
    disk_data_index = 0;
    disk_status_store(DISK_STATUS_READY |
                      (fatal ? DISK_STATUS_ERROR : 0));
    return;
  }
  if (fatal) {
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }

  if (command == DISK_COMMAND_FLUSH) {
    disk_submit(command);
    return;
  }
  if (disk_drive >= Z80_FLASH_DISK_SLOT_COUNT ||
      disk_lba >= Z80_FLASH_RECORD_COUNT) {
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }

  disk_data_index = 0;
  if (command == DISK_COMMAND_READ) {
    uint32_t key = record_key(disk_drive, disk_lba);
    if (prefetch_take(key, disk_lba)) {
      __atomic_store_n(&prefetch_hits, prefetch_hits + 1, __ATOMIC_RELAXED);
    } else {
      z80_flash_backend_read_record(disk_drive, disk_lba, disk_record.bytes);
      __atomic_store_n(&prefetch_misses, prefetch_misses + 1,
                       __ATOMIC_RELAXED);
    }
    __atomic_store_n(&prefetch_hint, key, __ATOMIC_RELEASE);
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_DATA_READY);
  } else if (command >= DISK_COMMAND_WRITE_NORMAL &&
             command <= DISK_COMMAND_WRITE_UNALLOCATED) {
    disk_write_drive = disk_drive;
    disk_write_lba = disk_lba;
    disk_write_command = command;
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_DATA_ROOM);
  } else {
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
  }
}

uint8_t __not_in_flash_func(z80_flash_disk_io_read)(uint8_t port) {
  if (port == DISK_COMMAND_STATUS_PORT)
    return __atomic_load_n(&disk_write_enabled, __ATOMIC_ACQUIRE)
               ? (uint8_t)disk_status_load()
               : DISK_STATUS_BUSY | DISK_STATUS_ERROR;
  if (port != DISK_DATA_PORT ||
      !(disk_status_load() & DISK_STATUS_DATA_READY))
    return 0;

  uint8_t value = disk_record.bytes[disk_data_index++];
  if (disk_data_index == sizeof(disk_record.bytes))
    disk_status_store(DISK_STATUS_READY);
  return value;
}

void __not_in_flash_func(z80_flash_disk_io_write)(uint8_t port, uint8_t value) {
  if (!__atomic_load_n(&disk_write_enabled, __ATOMIC_ACQUIRE))
    return;
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
    disk_lba = (uint16_t)((disk_lba & 0x00FFu) |
                          ((uint16_t)value << 8));
  } else if (port == DISK_DATA_PORT &&
             (status & DISK_STATUS_DATA_ROOM)) {
    disk_record.bytes[disk_data_index++] = value;
    if (disk_data_index == sizeof(disk_record.bytes))
      disk_submit(disk_write_command);
  }
}

void z80_flash_core1_service(void) {
  if (__atomic_load_n(&disk_fatal_error, __ATOMIC_ACQUIRE))
    return;
  uint32_t command = __atomic_load_n(&disk_pending, __ATOMIC_ACQUIRE);
  if (command != 0) {
    bool success;
    if (command == DISK_COMMAND_FLUSH) {
      success = z80_flash_backend_flush();
    } else {
      prefetch_invalidate(disk_write_drive, disk_write_lba);
      success = z80_flash_backend_write_record(
          disk_write_drive, disk_write_lba, disk_record.bytes,
          sizeof(disk_record.bytes),
          (uint8_t)(command - DISK_COMMAND_WRITE_NORMAL));
    }
    if (!success)
      __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&disk_pending, 0, __ATOMIC_RELAXED);
    disk_status_store(DISK_STATUS_READY |
                      (success ? 0 : DISK_STATUS_ERROR));
    if (!success)
      return;
  } else if (__atomic_load_n(&supervisor_flush, __ATOMIC_ACQUIRE)) {
    bool success = z80_flash_backend_flush();
    __atomic_store_n(&supervisor_flush, 0, __ATOMIC_RELEASE);
    if (!success) {
      __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
      disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
      return;
    }
  } else if (z80_flash_backend_flush_due() && !z80_flash_backend_flush()) {
    __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
    disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }
  prefetch_service();
}

bool z80_flash_storage_init(void) {
  return z80_flash_storage_init_observed(NULL, NULL);
}

bool z80_flash_storage_init_observed(z80_boot_observer_t observer,
                                     void *context) {
  __atomic_store_n(&disk_pending, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&supervisor_flush, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&prefetch_hint, 0, __ATOMIC_RELEASE);
  for (unsigned int slot = 0; slot < PREFETCH_SLOTS; ++slot)
    prefetch_publish(&prefetch[slot], 0, NULL);
  __atomic_store_n(&prefetch_hits, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&prefetch_misses, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&disk_fatal_error, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&disk_write_enabled, 1, __ATOMIC_RELEASE);
  disk_status_store(DISK_STATUS_READY);
  return z80_flash_backend_init_observed(observer, context);
}

void z80_flash_core0_service(void) {
  z80_flash_backend_core0_service();
}

uint32_t z80_flash_disk_status(void) {
  return disk_status_load();
}

void z80_flash_disk_prefetch_counts(uint32_t *hits, uint32_t *misses) {
  *hits = __atomic_load_n(&prefetch_hits, __ATOMIC_RELAXED);
  *misses = __atomic_load_n(&prefetch_misses, __ATOMIC_RELAXED);
}

bool z80_flash_disk_has_fatal_error(void) {
  return __atomic_load_n(&disk_fatal_error, __ATOMIC_ACQUIRE) != 0;
}

bool z80_flash_disk_quiescent(void) {
  return !__atomic_load_n(&supervisor_flush, __ATOMIC_ACQUIRE) &&
         disk_status_load() == DISK_STATUS_READY &&
         z80_flash_backend_quiescent();
}

void z80_flash_disk_request_flush(void) {
  __atomic_store_n(&supervisor_flush, 1, __ATOMIC_RELEASE);
}

void z80_flash_disk_set_write_enabled(bool enabled) {
  __atomic_store_n(&disk_write_enabled, enabled, __ATOMIC_RELEASE);
}

void z80_flash_disk_arm_fault(z80_flash_fault_point_t point) {
  z80_flash_backend_arm_fault(point);
}
