#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/pins.h"
#include "z80sbc/terminal.h"

void stdio_init_all(void);
int getchar_timeout_us(uint32_t timeout_us);
static int firmware_printf(const char *format, ...);
#define printf firmware_printf
#ifdef TEST_STAGE08
#define main stage08_firmware_main
#include "../../stage08_virtual_io/main.c"
#else
#define main stage10_firmware_main
#include "../../stage10_websocket_terminal/main.c"
#endif
#undef main
#undef printf

static bool levels[32];
static uint64_t now_us;
static unsigned disk_writes;
static unsigned terminal_writes;
static uint8_t last_port;
static char message[160];

static int firmware_printf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  return result;
}
void gpio_init(uint pin) { (void)pin; }
void gpio_put(uint pin, bool value) { levels[pin] = value; }
bool gpio_get(uint pin) { return levels[pin]; }
void gpio_set_dir(uint pin, bool output) { (void)pin; (void)output; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void busy_wait_us_32(uint32_t delay) { now_us += delay; }
absolute_time_t make_timeout_time_ms(uint32_t delay) {
  return now_us + (uint64_t)delay * 1000u;
}
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }
void tight_loop_contents(void) { ++now_us; }
void stdio_init_all(void) {}
int getchar_timeout_us(uint32_t timeout_us) { (void)timeout_us; return -1; }
void multicore_launch_core1(void (*entry)(void)) { (void)entry; }
void z80_safe_startup(void) {}
bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length) {
  (void)image; (void)length; return true;
}
bool z80_cpu_prepare_loader(void) { return true; }
bool z80_sram_load(uint16_t address, const uint8_t *data, uint32_t length) {
  (void)address; (void)data; (void)length; return true;
}
bool z80_sram_verify(uint16_t address, const uint8_t *data, uint32_t length) {
  (void)address; (void)data; (void)length; return true;
}
bool z80_cpu_release_reset_and_run(uint32_t hz) { (void)hz; return true; }
bool z80_cpu_request_bus(uint32_t timeout) { (void)timeout; return true; }
bool z80_cpu_release_bus(uint32_t timeout) { (void)timeout; return true; }
void z80_cpu_fail_closed(void) {}
bool z80_clock_set_hz(uint32_t hz) { (void)hz; return true; }
uint32_t z80_clock_get_hz(void) { return 1000000; }
bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  *value = (uint8_t)address; return true;
}
bool z80_io_trap_enable(z80_io_read_handler_t read_handler,
                        z80_io_write_handler_t write_handler, void *context) {
  (void)read_handler; (void)write_handler; (void)context; return true;
}
void z80_io_trap_disable(void) {}
void z80_io_trap_rearm(void) {}
uint32_t z80_io_trap_timeout_count(void) { return 0; }
uint32_t z80_io_trap_control_error_count(void) { return 0; }
bool z80_flash_storage_init(void) { return true; }
void z80_flash_core0_service(void) {}
void z80_flash_core1_service(void) {}
bool z80_flash_disk_quiescent(void) { return true; }
uint8_t z80_flash_disk_io_read(uint8_t port) { last_port = port; return 0xA5; }
void z80_flash_disk_io_write(uint8_t port, uint8_t value) {
  (void)value; last_port = port; ++disk_writes;
}
uint32_t z80_flash_disk_status(void) { return 0; }
bool z80_flash_disk_has_fatal_error(void) { return false; }
bool z80_terminal_init(void) { return true; }
void z80_terminal_core1_service(void) {}
uint8_t z80_terminal_io_read(uint8_t port) { last_port = port; return 0x5A; }
void z80_terminal_io_write(uint8_t port, uint8_t value) {
  (void)value; last_port = port; ++terminal_writes;
}
uint32_t z80_terminal_rx_drop_count(void) { return 0; }
uint32_t z80_terminal_tx_drop_count(void) { return 0; }
bool z80_terminal_client_connected(void) { return false; }

static uint8_t masked_port(uint8_t address) {
  levels[PIN_PORT_A0] = (address & 1u) != 0;
  levels[PIN_PORT_A1] = (address & 2u) != 0;
  levels[PIN_PORT_A2] = (address & 4u) != 0;
  levels[PIN_PORT_A4] = (address & 16u) != 0;
  return z80_port_bus_sample();
}

#ifdef TEST_STAGE08
absolute_time_t get_absolute_time(void) { return now_us; }
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
  return (int64_t)to - (int64_t)from;
}
bool stdio_usb_connected(void) { return true; }
int putchar_raw(int value) { return value; }
void queue_init(queue_t *queue, unsigned element_size, unsigned capacity) {
  queue->element_size = element_size;
  queue->capacity = capacity;
  queue->count = 0;
}
bool queue_try_add(queue_t *queue, const void *item) {
  (void)queue; (void)item; return true;
}
bool queue_try_remove(queue_t *queue, void *item) {
  (void)queue; (void)item; return false;
}
unsigned queue_get_level(queue_t *queue) { return queue->count; }

static void self_test_run(bool corrupt) {
  io_mode = IO_MODE_SELF_TEST;
  self_test_write_index = self_test_errors = self_test_complete = 0;
  self_test_pending = true;
  size_t offset = 0;
  for (size_t i = 0; i < sizeof(SELF_TEST_PORTS); ++i) {
    assert(self_test_image[offset++] == 0x3E);
    uint8_t value = self_test_image[offset++];
    assert(value == SELF_TEST_PORTS[i]);
    assert(masked_port(value) != TEST_RESULT_PORT);
    assert(self_test_image[offset++] == 0xD3);
    virtual_write(masked_port(self_test_image[offset++]),
                  value ^ (corrupt && i == 0), NULL);
  }
  for (size_t i = 0; i < sizeof(SELF_TEST_PORTS); ++i) {
    assert(self_test_image[offset++] == 0xDB);
    uint8_t value = virtual_read(masked_port(self_test_image[offset++]), NULL);
    assert(self_test_image[offset] == 0x32 &&
           self_test_image[offset + 3] == 0x3A);
    offset += 6;
    assert(self_test_image[offset++] == 0xFE);
    assert(value == self_test_image[offset++]);
    assert(self_test_image[offset++] == 0xC2);
    offset += 2;
  }
  assert(self_test_image[offset++] == 0x3E);
  uint8_t result = self_test_image[offset++];
  assert(result == 0x5A && self_test_image[offset++] == 0xD3);
  virtual_write(masked_port(self_test_image[offset++]), result, NULL);
  assert(self_test_image[offset] == 0x76 && self_test_complete);
  assert(corrupt ? self_test_errors > 0 : self_test_errors == 0);
  service_self_test();
  assert(strstr(message, corrupt ? "FAIL:" : "PASS:") != NULL);
}

int main(void) {
  assert(masked_port(TEST_RESULT_PORT) == TEST_RESULT_PORT);
  build_self_test_image();
  self_test_run(false);
  self_test_run(true);
  io_mode = IO_MODE_RAM_CHECKER;
  ram_failures = 0;
  size_t end = sizeof(RAM_CHECK_PROGRAM);
  assert(RAM_CHECK_PROGRAM[end - 3] == 0xD3);
  virtual_write(masked_port(RAM_CHECK_PROGRAM[end - 2]),
                RAM_CHECK_PROGRAM[end - 4], NULL);
  assert(ram_failures == 1);
  hour_test_active = true;
  hour_test_deadline = now_us;
  hour_progress.observed = true;
  hour_progress.last_count = ram_heartbeats;
  hour_progress.deadline = UINT64_MAX;
  service_hour_test();
  assert(!hour_test_active && strstr(message, "FAIL: one-hour") != NULL);
  unsigned old_heartbeats = ram_heartbeats;
  virtual_write(masked_port(0xE8), 0x41, NULL);
  assert(ram_heartbeats == old_heartbeats + 1);
  puts("PASS: real Stage 8 self-test aliases, IN expectations and RAM failures");
}
#else
static void address_run(bool corrupt) {
  qualification_mode = QUALIFICATION_ADDRESS;
  qualification_index = qualification_errors = qualification_complete = 0;
  for (size_t i = 0; i < sizeof(address_expected); ++i)
    address_expected[i] = (uint8_t)(i * 7u);
  size_t offset = 0;
  for (size_t i = 0; i < sizeof(address_expected); ++i) {
    assert(address_test_image[offset++] == 0x3A);
    uint16_t address = address_test_image[offset++];
    address |= (uint16_t)address_test_image[offset++] << 8;
    assert(address == QUALIFICATION_ADDRESSES[i]);
    assert(address_test_image[offset++] == 0xD3);
    uint8_t port = masked_port(address_test_image[offset++]);
    virtual_write(port, address_expected[i] ^ (corrupt && i == 0), NULL);
  }
  assert(address_test_image[offset++] == 0x3E);
  uint8_t result = address_test_image[offset++];
  assert(result == 0x5A && address_test_image[offset++] == 0xD3);
  virtual_write(masked_port(address_test_image[offset++]), result, NULL);
  assert(address_test_image[offset++] == 0x76);
  assert(offset == address_test_image_length && qualification_complete);
  if (corrupt)
    assert(qualification_errors > 0);
  else
    assert(qualification_index == sizeof(address_expected) &&
           qualification_errors == 0);
  service_qualification();
  assert(strstr(message, corrupt ? "FAIL:" : "PASS:") != NULL);
}

static void hour_result(bool corrupt) {
  qualification_mode = QUALIFICATION_RAM;
  qualification_errors = 0;
  hour_test_active = true;
  hour_test_deadline = now_us;
  hour_progress.observed = true;
  hour_progress.last_count = ram_heartbeats;
  hour_progress.deadline = UINT64_MAX;
  hour_trap_timeout_baseline = hour_control_error_baseline = 0;
  if (corrupt) {
    size_t end = sizeof(RAM_CHECK_PROGRAM);
    assert(RAM_CHECK_PROGRAM[end - 3] == 0xD3);
    assert(RAM_CHECK_PROGRAM[end - 5] == 0x3E);
    virtual_write(masked_port(RAM_CHECK_PROGRAM[end - 2]),
                  RAM_CHECK_PROGRAM[end - 4], NULL);
    assert(qualification_errors == 1);
  }
  service_qualification();
  assert(!hour_test_active);
  assert(strstr(message, corrupt ? "FAIL: one-hour" : "PASS: one-hour") != NULL);
}

int main(void) {
  assert(masked_port(QUALIFICATION_DATA_PORT) == QUALIFICATION_DATA_PORT);
  assert(masked_port(QUALIFICATION_RESULT_PORT) == QUALIFICATION_RESULT_PORT);
  assert(QUALIFICATION_DATA_PORT != QUALIFICATION_RESULT_PORT);
  build_address_test_image();
  address_run(false);
  address_run(true);
  hour_result(false);
  hour_result(true);
  qualification_mode = QUALIFICATION_NONE;
  for (unsigned address = 0; address < 256; ++address) {
    uint8_t port = masked_port((uint8_t)address);
    assert(port == (address & 0x17));
    unsigned old_disk = disk_writes;
    unsigned old_terminal = terminal_writes;
    virtual_write(port, 0xA5, NULL);
    if (port >= 0x10 && port <= 0x14) {
      assert(disk_writes == old_disk + 1 && terminal_writes == old_terminal);
      assert(virtual_read(port, NULL) == 0xA5 && last_port == port);
    } else if (port != QUALIFICATION_DATA_PORT) {
      assert(terminal_writes == old_terminal + 1 && disk_writes == old_disk);
      assert(virtual_read(port, NULL) == 0x5A && last_port == port);
    }
  }
  qualification_mode = QUALIFICATION_RAM;
  unsigned old_heartbeats = ram_heartbeats;
  virtual_write(masked_port(0xE8), 0x41, NULL);
  assert(ram_heartbeats == old_heartbeats + 1);
  puts("PASS: real Stage 10 images/handlers, RAM failure reporting and port aliases");
}
#endif
