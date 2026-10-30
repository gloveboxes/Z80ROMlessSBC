#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../flash_backend.c"

uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
static uint8_t sram[65536];
static uint8_t old_block[FLASH_SECTOR_SIZE];
static uint8_t new_record[Z80_FLASH_RECORD_BYTES];
static jmp_buf reboot_target;
static bool expect_reboot;
static bool cpu_held;
static bool clock_stopped;
static bool bus_owned;
static bool trap_enabled;
static bool loader_ok;
static bool corrupt_probe_restore;
static bool core_init_ok;
static bool release_ok;
static int bad_read;
static int failed_read;
static int failed_write;
static unsigned target_erases;
static unsigned corrupt_program;
static bool safe_execute_error;
static uint64_t now;
static int cancel_phase;
static uint32_t last_completed;
static z80_boot_phase_t last_phase;

void queue_init(queue_t *queue, unsigned size, unsigned capacity) {
  free(queue->data);
  queue->data = calloc(capacity, size);
  assert(queue->data);
  queue->element_size = size;
  queue->capacity = capacity;
  queue->count = 0;
}
bool queue_try_add(queue_t *queue, const void *item) {
  if (queue->count == queue->capacity)
    return false;
  memcpy(queue->data + queue->count++ * queue->element_size, item,
         queue->element_size);
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
void queue_add_blocking(queue_t *queue, const void *item) {
  assert(queue_try_add(queue, item));
  assert(queue == &service_request_queue);
  z80_flash_backend_core0_service();
}
void queue_remove_blocking(queue_t *queue, void *item) {
  assert(queue_try_remove(queue, item));
}
void gpio_put(uint pin, bool value) {
  assert(pin == PIN_RESET_N && !value);
  cpu_held = true;
}
void z80_isolate_buses(void) {}
void z80_clock_stop(void) { clock_stopped = true; }
void z80_cpu_fail_closed(void) {
  cpu_held = clock_stopped = true;
}
bool z80_cpu_prepare_loader(void) {
  cpu_held = false;
  return loader_ok;
}
void z80_sram_prepare_banked_memory(const uint8_t *resident_page) {
  assert(!cpu_held);
  assert(boot_status.phase == Z80_BOOT_VERIFY && boot_status.completed == 65536);
  assert(memcmp(sram, test_flash + FLASH_BOOT_PAYLOAD_OFFSET, 65536) == 0);
  if (corrupt_probe_restore)
    sram[0] ^= 1;
  assert(resident_page == test_flash + FLASH_BOOT_PAYLOAD_OFFSET);
}
void z80_io_trap_poll(void) {}
bool z80_sram_write_byte(uint16_t address, uint8_t value) {
  assert(!cpu_held);
  if ((int)address == failed_write)
    return false;
  sram[address] = value;
  return true;
}
bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  assert(!cpu_held);
  if ((int)address == failed_read)
    return false;
  *value = sram[address] ^ ((int)address == bad_read ? 1 : 0);
  return true;
}
bool z80_cpu_request_bus(uint32_t timeout) {
  assert(timeout == 500000 && !bus_owned);
  bus_owned = true;
  return true;
}
bool z80_cpu_release_bus(uint32_t timeout) {
  assert(timeout == 500000 && bus_owned && trap_enabled);
  bus_owned = false;
  return release_ok;
}
void z80_io_trap_disable(void) { trap_enabled = false; }
void z80_io_trap_rearm(void) { trap_enabled = true; }
absolute_time_t make_timeout_time_ms(uint32_t delay) {
  return now + (uint64_t)delay * 1000u;
}
bool time_reached(absolute_time_t deadline) { return now >= deadline; }
void tight_loop_contents(void) { assert(false); }
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  assert(pc == 0 && sp == 0 && delay == 0 && expect_reboot);
  assert(cpu_held && clock_stopped);
  longjmp(reboot_target, 1);
}
bool flash_safe_execute_core_init(void) { return core_init_ok; }
int flash_safe_execute(void (*callback)(void *), void *parameter,
                        uint32_t timeout) {
  assert(timeout == 1000);
  if (safe_execute_error)
    return PICO_ERROR_TIMEOUT;
  callback(parameter);
  return PICO_OK;
}
void flash_range_erase(uint32_t offset, size_t length) {
  assert(offset % FLASH_SECTOR_SIZE == 0);
  assert(length % FLASH_SECTOR_SIZE == 0);
  assert(offset <= sizeof(test_flash) - length);
  if (offset >= Z80_FLASH_ENV_OFFSET && offset < Z80_FLASH_DISK_OFFSET)
    assert(bus_owned && !trap_enabled && length == FLASH_SECTOR_SIZE);
  if (offset >= Z80_FLASH_DISK_OFFSET) {
    assert(bus_owned || cpu_held);
    ++target_erases;
  }
  memset(test_flash + offset, 0xFF, length);
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length) {
  assert(offset % FLASH_PAGE_SIZE == 0 && length % FLASH_PAGE_SIZE == 0);
  assert(offset <= sizeof(test_flash) - length);
  if (offset >= Z80_FLASH_ENV_OFFSET && offset < Z80_FLASH_DISK_OFFSET) {
    assert(bus_owned && !trap_enabled);
    uint32_t base = offset & ~(uint32_t)(FLASH_SECTOR_SIZE - 1);
    if (offset != base)
      assert(test_flash[base] == 0xff);
    else
      assert(test_flash[base + FLASH_PAGE_SIZE] == 0x5a);
  }
  for (size_t index = 0; index < length; ++index) {
    assert((test_flash[offset + index] & data[index]) == data[index]);
    test_flash[offset + index] &= data[index];
  }
  if ((corrupt_program == 1 && offset == Z80_FLASH_JOURNAL_OFFSET +
                                            FLASH_SECTOR_SIZE) ||
      (corrupt_program == 2 && offset == Z80_FLASH_JOURNAL_OFFSET)) {
    test_flash[offset] ^= 1;
    corrupt_program = 0;
  }
  if ((corrupt_program == 3 && offset == Z80_FLASH_ENV_OFFSET +
                                             FLASH_PAGE_SIZE) ||
      (corrupt_program == 4 && offset == Z80_FLASH_ENV_OFFSET)) {
    test_flash[offset] ^= 1;
    corrupt_program = 0;
  }
}

static void put_manifest(uint32_t size) {
  boot_manifest_t manifest = {
    .magic = Z80_BOOT_MAGIC,
    .version = Z80_BOOT_VERSION,
    .header_bytes = sizeof(boot_manifest_t),
    .image_bytes = size,
    .image_crc32 = crc32_bytes(test_flash + FLASH_BOOT_PAYLOAD_OFFSET,
                               size <= 65536 ? size : 0),
  };
  manifest.header_crc32 = crc32_bytes((const uint8_t *)&manifest, 16);
  memcpy(test_flash + Z80_FLASH_BOOT_OFFSET, &manifest, sizeof(manifest));
}

static void reset_fixture(void) {
  memset(test_flash, 0xFF, sizeof(test_flash));
  memset(sram, 0, sizeof(sram));
  for (unsigned index = 0; index < 65536; ++index)
    test_flash[FLASH_BOOT_PAYLOAD_OFFSET + index] =
        (uint8_t)(index ^ (index >> 8));
  memcpy(test_flash + FLASH_BOOT_PAYLOAD_OFFSET, "\xC3\x00\xFC", 3);
  put_manifest(65536);
  memset(old_block, 0xA5, sizeof(old_block));
  memset(new_record, 0x3C, sizeof(new_record));
  memcpy(test_flash + Z80_FLASH_DISK_OFFSET, old_block, sizeof(old_block));
  loader_ok = core_init_ok = release_ok = true;
  expect_reboot = cpu_held = clock_stopped = bus_owned = false;
  trap_enabled = true;
  corrupt_probe_restore = false;
  bad_read = failed_read = failed_write = cancel_phase = -1;
  corrupt_program = target_erases = 0;
  now = 0;
  last_completed = 0;
  last_phase = Z80_BOOT_RECOVERY;
}

static bool observe(const z80_boot_status_t *status, void *context) {
  assert(context == sram);
  assert(status->error == Z80_BOOT_ERROR_NONE && status->total == 65536);
  assert(status->completed <= status->total);
  if (status->phase == last_phase)
    assert(status->completed >= last_completed);
  last_phase = status->phase;
  last_completed = status->completed;
  return (int)status->phase != cancel_phase ||
         (status->phase == Z80_BOOT_LOAD && status->completed < 256);
}

static void assert_boot_error(z80_boot_error_t error) {
  assert(!z80_flash_backend_init_observed(observe, sram));
  assert(z80_flash_boot_status()->error == error);
  assert(cpu_held && clock_stopped);
}

static void test_boot(void) {
  reset_fixture();
  assert(crc32_bytes((const uint8_t *)"123456789", 9) == 0xCBF43926u);
  assert(z80_flash_backend_init_observed(observe, sram));
  assert(last_phase == Z80_BOOT_VERIFIED && last_completed == 65536);
  assert(cpu_held && clock_stopped);
  assert(memcmp(sram, test_flash + FLASH_BOOT_PAYLOAD_OFFSET, 65536) == 0);
  reset_fixture();
  corrupt_probe_restore = true;
  assert_boot_error(Z80_BOOT_ERROR_MISMATCH);
  assert(boot_status.address == 0);
  const uint32_t sizes[] = {0, 1, 65535, 65537};
  for (unsigned index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
    reset_fixture();
    put_manifest(sizes[index]);
    assert_boot_error(Z80_BOOT_ERROR_MANIFEST);
    assert(sram[1] == 0);
  }
  reset_fixture();
  test_flash[Z80_FLASH_BOOT_OFFSET] ^= 1;
  assert_boot_error(Z80_BOOT_ERROR_MANIFEST);
  reset_fixture();
  test_flash[Z80_FLASH_BOOT_OFFSET + 16] ^= 1;
  assert_boot_error(Z80_BOOT_ERROR_MANIFEST);
  reset_fixture();
  test_flash[FLASH_BOOT_PAYLOAD_OFFSET + 65535] ^= 1;
  assert_boot_error(Z80_BOOT_ERROR_PAYLOAD_CRC);
  reset_fixture();
  test_flash[FLASH_BOOT_PAYLOAD_OFFSET] = 0;
  put_manifest(65536);
  assert_boot_error(Z80_BOOT_ERROR_MANIFEST);
  reset_fixture();
  loader_ok = false;
  assert_boot_error(Z80_BOOT_ERROR_LOADER);
  reset_fixture();
  failed_write = 256;
  assert_boot_error(Z80_BOOT_ERROR_WRITE);
  assert(boot_status.address == 256);
  reset_fixture();
  failed_read = 65535;
  assert_boot_error(Z80_BOOT_ERROR_READ);
  reset_fixture();
  bad_read = 65535;
  assert_boot_error(Z80_BOOT_ERROR_MISMATCH);
  assert(boot_status.address == 65535);
  assert((boot_status.expected ^ boot_status.actual) == 1);
  reset_fixture();
  core_init_ok = false;
  assert_boot_error(Z80_BOOT_ERROR_FLASH_INIT);
  for (int phase = Z80_BOOT_RECOVERY; phase <= Z80_BOOT_VERIFIED; ++phase) {
    reset_fixture();
    cancel_phase = phase;
    assert_boot_error(Z80_BOOT_ERROR_CANCELLED);
  }
}

static void stage_write(void) {
  assert(z80_flash_backend_init());
  cpu_held = false;
  assert(z80_flash_backend_write_record(0, 0, new_record,
                                        sizeof(new_record), 0));
  assert(memcmp(test_flash + Z80_FLASH_DISK_OFFSET, old_block,
                 sizeof(old_block)) == 0);
}

static void test_power_cuts(void) {
  for (int point = Z80_FLASH_FAULT_AFTER_JOURNAL_DATA;
       point <= Z80_FLASH_FAULT_SAFE_EXECUTE_EXIT; ++point) {
    reset_fixture();
    stage_write();
    z80_flash_backend_arm_fault((z80_flash_fault_point_t)point);
    expect_reboot = true;
    if (setjmp(reboot_target) == 0) {
      (void)z80_flash_backend_flush();
      assert(false);
    }
    expect_reboot = false;
    assert(armed_fault == Z80_FLASH_FAULT_NONE);
    bus_owned = false;
    assert(z80_flash_backend_init());
    bool old = point == Z80_FLASH_FAULT_AFTER_JOURNAL_DATA ||
               point == Z80_FLASH_FAULT_SAFE_EXECUTE_ENTRY;
    assert(memcmp(test_flash + Z80_FLASH_DISK_OFFSET,
                   old ? old_block : new_record, sizeof(new_record)) == 0);
    assert(memcmp(test_flash + Z80_FLASH_DISK_OFFSET + sizeof(new_record),
                   old_block + sizeof(new_record),
                   sizeof(old_block) - sizeof(new_record)) == 0);
    assert(!disk_cache.dirty && cpu_held);
  }
}

static void test_bad_program_and_recovery(void) {
  for (unsigned fault = 1; fault <= 2; ++fault) {
    reset_fixture();
    stage_write();
    corrupt_program = fault;
    assert(!z80_flash_backend_flush());
    assert(target_erases == 0 && !bus_owned && trap_enabled);
    assert(memcmp(test_flash + Z80_FLASH_DISK_OFFSET, old_block,
                   sizeof(old_block)) == 0);
  }
  reset_fixture();
  stage_write();
  armed_fault = Z80_FLASH_FAULT_AFTER_TARGET_ERASE;
  expect_reboot = true;
  if (setjmp(reboot_target) == 0) {
    (void)z80_flash_backend_flush();
    assert(false);
  }
  expect_reboot = false;
  test_flash[Z80_FLASH_JOURNAL_OFFSET + FLASH_SECTOR_SIZE] ^= 1;
  assert_boot_error(Z80_BOOT_ERROR_JOURNAL);
}

static void test_cache_and_bounds(void) {
  reset_fixture();
  stage_write();
  uint8_t record[Z80_FLASH_RECORD_BYTES];
  assert(z80_flash_backend_read_record(0, 0, record, sizeof(record)));
  assert(memcmp(record, new_record, sizeof(record)) == 0);
  now = 249999;
  assert(!z80_flash_backend_flush_due());
  now = 250000;
  assert(z80_flash_backend_flush_due());
  assert(z80_flash_backend_flush());
  assert(!z80_flash_backend_flush_due() && z80_flash_backend_quiescent());
  assert(target_erases == 1);
  assert(z80_flash_backend_write_record(0, 0, new_record,
                                        sizeof(new_record), 1));
  assert(target_erases == 1);
  for (unsigned index = 0; index < 10; ++index) {
    new_record[0] = (uint8_t)index;
    assert(z80_flash_backend_write_record(3, Z80_FLASH_RECORD_COUNT - 1, new_record,
                                          sizeof(new_record), 1));
    assert(z80_flash_backend_read_record(3, Z80_FLASH_RECORD_COUNT - 1, record, sizeof(record)));
    assert(memcmp(record, new_record, sizeof(record)) == 0);
  }
  assert(next_journal_pair == 11 && journal_sequence == 11);
  assert(!z80_flash_backend_read_record(4, 0, record, sizeof(record)));
  assert(!z80_flash_backend_read_record(0, Z80_FLASH_RECORD_COUNT, record, sizeof(record)));
  assert(!z80_flash_backend_write_record(0, 0, record, 127, 0));
  assert(!z80_flash_backend_write_record(0, 0, record, sizeof(record), 3));
  new_record[0] ^= 1;
  assert(z80_flash_backend_write_record(0, 0, new_record,
                                        sizeof(new_record), 0));
  release_ok = false;
  assert(!z80_flash_backend_flush());
  assert(!trap_enabled);
}

static void test_environment_flash_safety(void) {
  static uint8_t sector[FLASH_SECTOR_SIZE];
  memset(sector, 0x5a, sizeof(sector));
  reset_fixture();
  assert(z80_flash_backend_init());
  assert(z80_flash_backend_env_write(0, sector));
  assert(!bus_owned && trap_enabled);
  assert(memcmp(test_flash + Z80_FLASH_ENV_OFFSET, sector, sizeof(sector)) == 0);
  assert(memcmp(test_flash + Z80_FLASH_DISK_OFFSET, old_block,
                sizeof(old_block)) == 0);
  assert(target_erases == 0);
  assert(z80_flash_backend_env_write(1, sector));
  assert(memcmp(test_flash + Z80_FLASH_ENV_OFFSET + FLASH_SECTOR_SIZE,
                sector, sizeof(sector)) == 0);
  assert(!z80_flash_backend_env_write(2, sector));
  assert(!z80_flash_backend_env_write(0, NULL));
  for (unsigned corruption = 3; corruption <= 4; ++corruption) {
    corrupt_program = corruption;
    assert(!z80_flash_backend_env_write(0, sector));
    assert(!bus_owned && trap_enabled);
    if (corruption == 3)
      assert(test_flash[Z80_FLASH_ENV_OFFSET] == 0xff);
  }
  safe_execute_error = true;
  expect_reboot = true;
  if (setjmp(reboot_target) == 0) {
    (void)z80_flash_backend_env_write(0, sector);
    assert(false);
  }
  safe_execute_error = expect_reboot = false;
  assert(cpu_held && clock_stopped);
}

int main(void) {
  test_boot();
  test_power_cuts();
  test_bad_program_and_recovery();
  test_cache_and_bounds();
  test_environment_flash_safety();
  free(service_request_queue.data);
  free(service_result_queue.data);
  puts("PASS: real backend boot, bounds, cancellation, first fault, cache, "
       "journal rotation, corrupt programming and all eight reboot points");
  return 0;
}
