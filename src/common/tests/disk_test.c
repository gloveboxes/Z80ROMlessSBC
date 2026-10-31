#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80sbc/flash_disk.h"
#include "z80sbc/flash_layout.h"
#include "../flash_backend.h"
#include "../disk_device.c"

enum { STATUS_PORT = 0x10, DATA_PORT = 0x14, READY = 1, BUSY = 8, ERROR = 0x80 };
static bool backend_success;
static bool flush_due;
static unsigned flush_calls;
static unsigned write_calls;
static uint8_t last_write_type;
static unsigned read_calls;
static bool has_written;
static uint16_t written_lba;
static uint8_t written[Z80_FLASH_RECORD_BYTES];

bool z80_flash_backend_init(void) { return true; }
bool z80_flash_backend_init_observed(z80_boot_observer_t observer,
                                     void *context) {
  (void)observer; (void)context; return true;
}
void z80_flash_backend_core0_service(void) {}
void z80_flash_backend_read_record(unsigned drive, uint16_t lba,
                                   uint8_t *data) {
  ++read_calls;
  for (unsigned index = 0; index < Z80_FLASH_RECORD_BYTES; ++index)
    data[index] = drive == 0 && has_written && lba == written_lba
                      ? written[index]
                      : (uint8_t)(drive + lba + index);
}
bool z80_flash_backend_write_record(unsigned drive, uint16_t lba,
                                     const uint8_t *data, size_t length,
                                     uint8_t write_type) {
  assert(drive == 0 && write_type <= 2);
  assert(length == Z80_FLASH_RECORD_BYTES);
  for (size_t index = 0; index < length; ++index)
    assert(data[index] == (uint8_t)index);
  ++write_calls;
  last_write_type = write_type;
  has_written = true;
  written_lba = lba;
  memcpy(written, data, length);
  return backend_success;
}
bool z80_flash_backend_flush(void) { ++flush_calls; return backend_success; }
bool z80_flash_backend_flush_due(void) { return flush_due; }
bool z80_flash_backend_quiescent(void) { return !flush_due; }
void z80_flash_backend_arm_fault(z80_flash_fault_point_t point) { (void)point; }

static void reset_fixture(void) {
  flush_due = false;
  backend_success = true;
  flush_calls = write_calls = read_calls = 0;
  has_written = false;
  assert(z80_flash_storage_init());
  z80_flash_disk_io_write(STATUS_PORT, 0);
}

static void submit(uint8_t command) {
  z80_flash_disk_io_write(STATUS_PORT, command);
  if (command >= 2 && command <= 4) {
    for (unsigned index = 0; index < Z80_FLASH_RECORD_BYTES; ++index)
      z80_flash_disk_io_write(DATA_PORT, (uint8_t)index);
  }
}

static void test_publication(void) {
  for (uint8_t command = 2; command <= 5; ++command) {
    reset_fixture();
    submit(command);
    assert(z80_flash_disk_status() == BUSY && !z80_flash_disk_quiescent());
    /* Core 1 owns the record while BUSY: the Z80 cannot overwrite it. */
    z80_flash_disk_io_write(0x11, 3);
    z80_flash_disk_io_write(0x12, 9);
    z80_flash_disk_io_write(DATA_PORT, 0xEE);
    z80_flash_disk_io_write(STATUS_PORT, 1);
    assert(z80_flash_disk_status() == BUSY);
    z80_flash_core1_service();
    assert(z80_flash_disk_status() == READY && z80_flash_disk_quiescent());
    assert(flush_calls + write_calls == 1);
    if (command != 5)
      assert(last_write_type == command - 2);
    z80_flash_core1_service();
    assert(flush_calls + write_calls == 1);
  }
}

static void test_read(void) {
  reset_fixture();
  z80_flash_disk_io_write(0x11, 1);
  z80_flash_disk_io_write(0x12, 2);
  z80_flash_disk_io_write(STATUS_PORT, 1);
  assert(z80_flash_disk_status() == (READY | 2));
  for (unsigned index = 0; index < Z80_FLASH_RECORD_BYTES; ++index)
    assert(z80_flash_disk_io_read(DATA_PORT) == (uint8_t)(3 + index));
  assert(z80_flash_disk_status() == READY);
  assert(z80_flash_disk_io_read(DATA_PORT) == 0);
  z80_flash_disk_io_write(0x11, 4);
  z80_flash_disk_io_write(STATUS_PORT, 1);
  assert(z80_flash_disk_status() == (READY | ERROR));
}

static void select_record(uint8_t drive, uint16_t lba) {
  z80_flash_disk_io_write(0x11, drive);
  z80_flash_disk_io_write(0x12, (uint8_t)lba);
  z80_flash_disk_io_write(0x13, (uint8_t)(lba >> 8));
}

static void expect_read(uint8_t drive, uint16_t lba, uint8_t base) {
  select_record(drive, lba);
  z80_flash_disk_io_write(STATUS_PORT, 1);
  assert(z80_flash_disk_status() == (READY | 2));
  for (unsigned index = 0; index < Z80_FLASH_RECORD_BYTES; ++index)
    assert(z80_flash_disk_io_read(DATA_PORT) ==
           (uint8_t)(base + (drive ? drive + lba : 0) + index));
  assert(z80_flash_disk_status() == READY);
}

static void test_readahead(void) {
  uint32_t hits, misses;
  reset_fixture();
  expect_read(1, 10, 0);
  assert(read_calls == 1);
  z80_flash_core1_service();
  assert(read_calls == 1 + PREFETCH_SLOTS);
  z80_flash_core1_service();
  assert(read_calls == 1 + PREFETCH_SLOTS);
  for (uint16_t lba = 11; lba <= 18; ++lba) {
    unsigned before = read_calls;
    expect_read(1, lba, 0);
    assert(read_calls == before);
    z80_flash_core1_service();
    assert(read_calls == before + 1);
  }
  z80_flash_disk_prefetch_counts(&hits, &misses);
  assert(hits == 8 && misses == 1);

  /* Another drive with the same LBA must not hit a stale slot. */
  expect_read(2, 19, 0);
  z80_flash_disk_prefetch_counts(&hits, &misses);
  assert(hits == 8 && misses == 2);

  /* A slot being rewritten by core 1 falls back to the backend read. */
  z80_flash_core1_service();
  prefetch[20 & (PREFETCH_SLOTS - 1)].sequence |= 1u;
  unsigned before = read_calls;
  expect_read(2, 20, 0);
  assert(read_calls == before + 1);
  prefetch[20 & (PREFETCH_SLOTS - 1)].sequence += 1u;

  /* The read-ahead window stops at the last record. */
  expect_read(2, Z80_FLASH_RECORD_COUNT - 1, 0);
  before = read_calls;
  z80_flash_core1_service();
  assert(read_calls == before);
}

static void test_write_invalidates_readahead(void) {
  reset_fixture();
  select_record(0, 30);
  z80_flash_disk_io_write(STATUS_PORT, 1);
  for (unsigned index = 0; index < Z80_FLASH_RECORD_BYTES; ++index)
    (void)z80_flash_disk_io_read(DATA_PORT);
  z80_flash_core1_service();
  uint32_t key = record_key(0, 32);
  assert(prefetch[32 & (PREFETCH_SLOTS - 1)].key == key);
  select_record(0, 32);
  submit(2);
  z80_flash_core1_service();
  assert(z80_flash_disk_status() == READY && written_lba == 32);
  /* Never the stale pre-write record: the slot was invalidated before READY
   * and refilled from the backend, which returns the written bytes. */
  unsigned before = read_calls;
  expect_read(0, 32, 0);
  assert(read_calls == before);
}

static void test_supervisor_flush(void) {
  reset_fixture();
  /* A Z80 READ that PIO queued just before the stop flush still works. */
  z80_flash_disk_request_flush();
  assert(!z80_flash_disk_quiescent());
  expect_read(1, 3, 0);
  assert(z80_flash_disk_status() == READY && flush_calls == 0);
  /* A pending Z80 write is committed before the supervisor flush. */
  select_record(0, 0);
  submit(2);
  assert(z80_flash_disk_status() == BUSY);
  z80_flash_core1_service();
  assert(write_calls == 1 && flush_calls == 0 && !z80_flash_disk_quiescent());
  z80_flash_core1_service();
  assert(flush_calls == 1 && z80_flash_disk_quiescent());
  assert(z80_flash_disk_status() == READY);
  z80_flash_core1_service();
  assert(flush_calls == 1);
  /* A failed supervisor flush latches the fatal error. */
  z80_flash_disk_request_flush();
  backend_success = false;
  z80_flash_core1_service();
  assert(z80_flash_disk_has_fatal_error());
  assert(z80_flash_disk_status() == (READY | ERROR));
}

static void test_fatal_latch(void) {
  for (uint8_t command = 2; command <= 6; ++command) {
    reset_fixture();
    backend_success = false;
    flush_due = true;
    if (command != 6)
      submit(command);
    z80_flash_core1_service();
    assert(z80_flash_disk_has_fatal_error());
    assert(z80_flash_disk_status() == (READY | ERROR));
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
      z80_flash_disk_io_write(STATUS_PORT, 0);
      assert(z80_flash_disk_status() == (READY | ERROR));
      submit(5);
      z80_flash_core1_service();
    }
    assert(flush_calls + write_calls == 1);
  }
}

int main(void) {
  reset_fixture();
  submit(2);
  z80_flash_disk_set_write_enabled(false);
  assert(z80_flash_disk_io_read(STATUS_PORT) == (BUSY | ERROR));
  submit(5);
  z80_flash_core1_service();
  assert(write_calls == 1 && flush_calls == 0);
  z80_flash_disk_set_write_enabled(true);
  assert(z80_flash_disk_io_read(STATUS_PORT) == READY);
  test_publication();
  test_read();
  test_readahead();
  test_write_invalidates_readahead();
  test_supervisor_flush();
  test_fatal_latch();
  puts("PASS: disk mailbox, ownership, reads, read-ahead coherence and fatal latch");
  return 0;
}
