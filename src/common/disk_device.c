#include "z80sbc/flash_disk.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
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
  DISK_WRITE_QUEUE_DEPTH = 2,
};

typedef struct {
  uint8_t command;
  uint8_t drive;
  uint16_t lba;
  uint8_t data[Z80_FLASH_RECORD_BYTES];
} disk_request_t;

static queue_t disk_write_queue;
static uint32_t disk_status = DISK_STATUS_READY;
static uint32_t disk_fatal_error;
static uint8_t disk_drive;
static uint16_t disk_lba;
static uint8_t disk_write_drive;
static uint16_t disk_write_lba;
static uint8_t disk_write_type;
static uint16_t disk_data_index;
static uint8_t disk_data[Z80_FLASH_RECORD_BYTES];

static uint32_t disk_status_load(void) {
  return __atomic_load_n(&disk_status, __ATOMIC_ACQUIRE);
}

static void disk_status_store(uint32_t value) {
  __atomic_store_n(&disk_status, value, __ATOMIC_RELEASE);
}

static bool disk_address_valid(void) {
  return disk_drive < Z80_FLASH_DISK_SLOT_COUNT &&
         disk_lba < Z80_FLASH_RECORD_COUNT;
}

static void disk_start_command(uint8_t command) {
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
    disk_request_t request = {.command = command};
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
    bool success = z80_flash_backend_read_record(
        disk_drive, disk_lba, disk_data, sizeof(disk_data));
    disk_status_store(DISK_STATUS_READY |
                      (success ? DISK_STATUS_DATA_READY : DISK_STATUS_ERROR));
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

uint8_t z80_flash_disk_io_read(uint8_t port) {
  if (port == DISK_COMMAND_STATUS_PORT)
    return (uint8_t)disk_status_load();
  if (port != DISK_DATA_PORT ||
      !(disk_status_load() & DISK_STATUS_DATA_READY))
    return 0;

  uint8_t value = disk_data[disk_data_index++];
  if (disk_data_index == sizeof(disk_data))
    disk_status_store(DISK_STATUS_READY);
  return value;
}

void z80_flash_disk_io_write(uint8_t port, uint8_t value) {
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
    disk_data[disk_data_index++] = value;
    if (disk_data_index == sizeof(disk_data)) {
      disk_request_t request = {
        .command = (uint8_t)(DISK_COMMAND_WRITE_NORMAL + disk_write_type),
        .drive = disk_write_drive,
        .lba = disk_write_lba,
      };
      memcpy(request.data, disk_data, sizeof(request.data));
      if (queue_try_add(&disk_write_queue, &request))
        disk_status_store(DISK_STATUS_BUSY);
      else
        disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    }
  }
}

void z80_flash_core1_service(void) {
  disk_request_t request;
  if (!queue_try_remove(&disk_write_queue, &request)) {
    if (!z80_flash_backend_flush_due())
      return;
    bool success = z80_flash_backend_flush();
    if (!success)
      __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
    if (!success)
      disk_status_store(DISK_STATUS_READY | DISK_STATUS_ERROR);
    return;
  }

  bool success = request.command == DISK_COMMAND_FLUSH
                     ? z80_flash_backend_flush()
                     : z80_flash_backend_write_record(
                           request.drive, request.lba, request.data,
                           sizeof(request.data),
                           request.command - DISK_COMMAND_WRITE_NORMAL);
  if (!success)
    __atomic_store_n(&disk_fatal_error, 1, __ATOMIC_RELEASE);
  disk_status_store(DISK_STATUS_READY |
                    (success ? 0 : DISK_STATUS_ERROR));
}

bool z80_flash_storage_init(void) {
  queue_init(&disk_write_queue, sizeof(disk_request_t),
             DISK_WRITE_QUEUE_DEPTH);
  __atomic_store_n(&disk_fatal_error, 0, __ATOMIC_RELEASE);
  disk_status_store(DISK_STATUS_READY);
  return z80_flash_backend_init();
}

void z80_flash_core0_service(void) {
  z80_flash_backend_core0_service();
}

uint32_t z80_flash_disk_status(void) {
  return disk_status_load();
}

bool z80_flash_disk_has_fatal_error(void) {
  return __atomic_load_n(&disk_fatal_error, __ATOMIC_ACQUIRE) != 0;
}

bool z80_flash_disk_quiescent(void) {
  return queue_is_empty(&disk_write_queue) &&
         disk_status_load() == DISK_STATUS_READY &&
         z80_flash_backend_quiescent();
}

void z80_flash_disk_arm_fault(z80_flash_fault_point_t point) {
  z80_flash_backend_arm_fault(point);
}