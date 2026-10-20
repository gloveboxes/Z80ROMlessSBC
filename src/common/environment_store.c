#include "z80sbc/environment.h"

#include <stdbool.h>
#include <string.h>

#include "hardware/flash.h"
#include "z80sbc/flash_layout.h"
#include "flash_backend.h"

enum { ENV_MAGIC = 0x31564e45, ENV_VERSION = 1 };

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint32_t length;
  uint32_t payload_crc;
  uint32_t header_crc;
} env_header_t;

_Static_assert(sizeof(env_header_t) == 24, "environment header layout changed");
_Static_assert(FLASH_SECTOR_SIZE - FLASH_PAGE_SIZE == Z80_ENV_SNAPSHOT_BYTES,
               "environment snapshot geometry changed");

static uint8_t sector[FLASH_SECTOR_SIZE];

static uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

static const uint8_t *slot_data(unsigned slot) {
  return (const uint8_t *)(XIP_BASE + Z80_FLASH_ENV_OFFSET +
                           slot * FLASH_SECTOR_SIZE);
}

static bool valid_slot(unsigned slot, env_header_t *header) {
  const uint8_t *data = slot_data(slot);
  memcpy(header, data, sizeof(*header));
  return header->magic == ENV_MAGIC && header->version == ENV_VERSION &&
         header->length <= Z80_ENV_SNAPSHOT_BYTES &&
         header->header_crc ==
             crc32(data, offsetof(env_header_t, header_crc)) &&
         header->payload_crc ==
             crc32(data + FLASH_PAGE_SIZE, header->length);
}

static int current_slot(env_header_t *header) {
  env_header_t headers[2];
  bool valid[2] = {valid_slot(0, &headers[0]), valid_slot(1, &headers[1])};
  if (!valid[0] && !valid[1]) {
    for (unsigned slot = 0; slot < 2; ++slot)
      for (size_t i = 0; i < FLASH_SECTOR_SIZE; ++i)
        if (slot_data(slot)[i] != 0xff)
          return -2;
    memset(header, 0, sizeof(*header));
    return -1;
  }
  unsigned slot = !valid[0] ? 1 : !valid[1] ? 0 :
      ((uint32_t)(headers[1].sequence - headers[0].sequence) < 0x80000000u
           ? 1 : 0);
  *header = headers[slot];
  return (int)slot;
}

int z80_env_snapshot_load(uint8_t *data, size_t capacity, size_t *length) {
  env_header_t header;
  int slot = current_slot(&header);
  if (slot == -2)
    return -2;
  if (capacity < header.length)
    return -4;
  *length = header.length;
  if (header.length)
    memcpy(data, slot_data((unsigned)slot) + FLASH_PAGE_SIZE, header.length);
  return 0;
}

int z80_env_snapshot_save(const uint8_t *data, size_t length) {
  if (length > Z80_ENV_SNAPSHOT_BYTES)
    return -4;
  env_header_t previous;
  int slot = current_slot(&previous);
  if (slot == -2)
    return -2;
  if (slot >= 0 && previous.length == length &&
      memcmp(slot_data((unsigned)slot) + FLASH_PAGE_SIZE, data, length) == 0)
    return 0;
  /* Establish a durable empty baseline before the first mutation. */
  if (slot == -1 && length != 0) {
    int status = z80_env_snapshot_save(data, 0);
    if (status != 0)
      return status;
    slot = current_slot(&previous);
    if (slot < 0)
      return -3;
  }
  memset(sector, 0xff, sizeof(sector));
  env_header_t header = {
    .magic = ENV_MAGIC,
    .version = ENV_VERSION,
    .sequence = previous.sequence + 1,
    .length = (uint32_t)length,
    .payload_crc = crc32(data, length),
  };
  header.header_crc =
      crc32((const uint8_t *)&header, offsetof(env_header_t, header_crc));
  memcpy(sector, &header, sizeof(header));
  memcpy(sector + FLASH_PAGE_SIZE, data, length);
  return z80_flash_backend_env_write(slot == 0 ? 1 : 0, sector) ? 0 : -3;
}
