#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/util/queue.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/flash_layout.h"
#include "../flash_backend.h"

enum { STATUS_PORT = 0x10, DATA_PORT = 0x14, READY = 1, BUSY = 8, ERROR = 0x80 };
static queue_t *requests;
static bool complete_during_add;
static bool reject_add;
static bool backend_success;
static bool flush_due;
static unsigned flush_calls;
static unsigned write_calls;

void queue_init(queue_t *queue, unsigned element_size, unsigned capacity) {
  free(queue->data);
  queue->data = calloc(capacity, element_size);
  assert(queue->data != NULL);
  queue->element_size = element_size;
  queue->capacity = capacity;
  queue->count = 0;
  requests = queue;
}

bool queue_try_add(queue_t *queue, const void *item) {
  assert(z80_flash_disk_status() == BUSY);
  if (reject_add || queue->count == queue->capacity)
    return false;
  memcpy(queue->data + queue->count++ * queue->element_size,
         item, queue->element_size);
  if (complete_during_add)
    z80_flash_core1_service();
  return true;
}

bool queue_try_remove(queue_t *queue, void *item) {
  if (!queue->count)
    return false;
  memcpy(item, queue->data, queue->element_size);
  --queue->count;
  memmove(queue->data, queue->data + queue->element_size,
          queue->count * queue->element_size);
  return true;
}

bool queue_is_empty(queue_t *queue) { return queue->count == 0; }
bool z80_flash_backend_init(void) { return true; }
void z80_flash_backend_core0_service(void) {}
bool z80_flash_backend_read_record(unsigned drive, uint16_t lba,
                                    uint8_t *data, size_t length) {
  (void)drive; (void)lba;
  memset(data, 0, length);
  return backend_success;
}
bool z80_flash_backend_write_record(unsigned drive, uint16_t lba,
                                     const uint8_t *data, size_t length,
                                     uint8_t write_type) {
  assert(drive == 0 && lba == 0 && write_type <= 2);
  assert(length == Z80_FLASH_RECORD_BYTES);
  for (size_t index = 0; index < length; ++index)
    assert(data[index] == (uint8_t)index);
  ++write_calls;
  return backend_success;
}
bool z80_flash_backend_flush(void) { ++flush_calls; return backend_success; }
bool z80_flash_backend_flush_due(void) { return flush_due; }
bool z80_flash_backend_quiescent(void) { return !flush_due; }
void z80_flash_backend_arm_fault(z80_flash_fault_point_t point) { (void)point; }

static void reset_fixture(void) {
  complete_during_add = reject_add = flush_due = false;
  backend_success = true;
  flush_calls = write_calls = 0;
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
    complete_during_add = true;
    submit(command);
    assert(queue_is_empty(requests) && z80_flash_disk_status() == READY);
    assert(flush_calls + write_calls == 1);
    reset_fixture();
    submit(command);
    assert(z80_flash_disk_status() == BUSY);
    z80_flash_core1_service();
    assert(z80_flash_disk_status() == READY);
    reset_fixture();
    reject_add = true;
    submit(command);
    assert(z80_flash_disk_status() == (READY | ERROR));
    assert(flush_calls + write_calls == 0);
  }
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
  test_publication();
  test_fatal_latch();
  free(requests->data);
  puts("PASS: disk publication, enqueue failure and fatal latch");
  return 0;
}