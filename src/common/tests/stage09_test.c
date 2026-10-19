#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/util/queue.h"
#include "hardware/flash.h"

static int firmware_printf(const char *format, ...);
#define printf firmware_printf
#define main stage09_firmware_main
#include "../../stage09_flash_storage/main.c"
#undef main
#undef printf

static z80_boot_status_t status;
uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
static unsigned load_calls;
static unsigned start_calls;
static unsigned core_launches;
static unsigned disk_writes;
static unsigned flushes;
static unsigned reboots;
static uint8_t disk_last_port;
static uint32_t fake_disk_status;
static uint32_t requested_clock;
static bool held;
static bool connected;
static bool quiescent;
static bool fatal;
static bool interrupts_disabled;
static bool load_ok;
static uint64_t now;
static unsigned usb_room;
static char output[16384];
static size_t output_length;
static uint8_t input[64];
static size_t input_index;
static size_t input_length;

static int firmware_printf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(output + output_length,
                         sizeof(output) - output_length, format, arguments);
  va_end(arguments);
  assert(result >= 0 && (size_t)result < sizeof(output) - output_length);
  output_length += (size_t)result;
  return result;
}
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
unsigned queue_get_level(queue_t *queue) { return queue->count; }
void z80_safe_startup(void) { held = true; }
void z80_cpu_fail_closed(void) { held = true; }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) { return connected; }
uint32_t tud_cdc_write_available(void) { return usb_room; }
int getchar_timeout_us(uint32_t timeout) {
  assert(timeout == 0);
  return input_index < input_length ? input[input_index++] : -1;
}
int putchar_raw(int value) {
  assert(usb_room && output_length + 1 < sizeof(output));
  output[output_length++] = (char)value;
  output[output_length] = '\0';
  --usb_room;
  return value;
}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  assert(pc == 0 && sp == 0 && delay == 0 && !running);
  ++reboots;
}
void multicore_launch_core1(void (*entry)(void)) {
  assert(entry == core1_main);
  ++core_launches;
}
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  requested_clock = hz;
  held = false;
  ++start_calls;
  return true;
}
bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length) {
  assert(image == z80_disk_diagnostic && length < 256);
  return load_ok;
}
uint32_t z80_clock_get_hz(void) { return requested_clock; }
uint32_t clock_get_hz(uint clock) { assert(clock == clk_sys); return 120000000; }
void set_sys_clock_pll(uint32_t vco, uint d1, uint d2) {
  assert(vco == 1200000000 && d1 == 5 && d2 == 2);
}
bool z80_io_trap_enable(z80_io_read_handler_t read_handler,
                        z80_io_write_handler_t write_handler, void *context) {
  assert(read_handler == virtual_read && write_handler == virtual_write);
  assert(context == NULL);
  return true;
}
void z80_io_trap_disable(void) {}
uint32_t z80_io_trap_timeout_count(void) { return 0; }
uint32_t z80_io_trap_control_error_count(void) { return 0; }
uint32_t save_and_disable_interrupts(void) {
  assert(!interrupts_disabled);
  interrupts_disabled = true;
  return 123;
}
void restore_interrupts(uint32_t state) {
  assert(state == 123 && interrupts_disabled);
  interrupts_disabled = false;
}
absolute_time_t make_timeout_time_ms(uint32_t delay) {
  return now + (uint64_t)delay * 1000;
}
bool time_reached(absolute_time_t deadline) { return now >= deadline; }
void sleep_ms(uint32_t delay) { now += delay * 1000u; }
void tight_loop_contents(void) {}
const z80_boot_status_t *z80_flash_boot_status(void) { return &status; }
bool z80_flash_storage_init_observed(z80_boot_observer_t observer,
                                     void *context) {
  assert(observer == observe_boot && context == NULL);
  ++load_calls;
  held = true;
  status = (z80_boot_status_t){
    .phase = Z80_BOOT_LOAD, .completed = 4096, .total = 65536,
  };
  if (!observer(&status, context)) {
    status.error = Z80_BOOT_ERROR_CANCELLED;
    return false;
  }
  if (!load_ok)
    status.error = Z80_BOOT_ERROR_MISMATCH;
  return load_ok;
}
void z80_flash_core0_service(void) {}
void z80_flash_core1_service(void) {}
uint8_t z80_flash_disk_io_read(uint8_t port) {
  disk_last_port = port;
  return 0x5A;
}
void z80_flash_disk_io_write(uint8_t port, uint8_t value) {
  disk_last_port = port;
  ++disk_writes;
  if (port == 0x10 && value == 5)
    ++flushes;
}
uint32_t z80_flash_disk_status(void) { return fake_disk_status; }
bool z80_flash_disk_has_fatal_error(void) { return fatal; }
bool z80_flash_disk_quiescent(void) { return quiescent; }
void z80_flash_disk_arm_fault(z80_flash_fault_point_t point) {
  assert(point <= Z80_FLASH_FAULT_SAFE_EXECUTE_EXIT);
}

static void reset_fixture(void) {
  boot_attempted = image_verified = running = core1_started = false;
  command_pending = usb_was_connected = stop_requested = false;
  flush_submitted = flash_resuming = bios_announced = ccp_announced = false;
  boot_timed_out = previous_cr = false;
  diagnostic_active = false;
  boot_markers = flash_pause_request = flash_paused = 0;
  reported_rx_drops = reported_tx_drops = 0;
  load_calls = start_calls = core_launches = disk_writes = flushes = reboots = 0;
  requested_clock = 0;
  connected = held = quiescent = load_ok = true;
  fatal = interrupts_disabled = false;
  now = 0;
  usb_room = 1024;
  fake_disk_status = DISK_READY;
  output_length = input_index = input_length = 0;
  output[0] = '\0';
  memset(&status, 0, sizeof(status));
  z80_terminal_queue_init();
}

static void command(uint8_t value) {
  receive_usb_byte(USB_COMMAND_ESCAPE);
  receive_usb_byte(value);
}

static void test_control_and_terminal(void) {
  reset_fixture();
  command('g');
  assert(!start_calls && held);
  command('l');
  assert(load_calls == 1 && image_verified && held && !core_launches);
  command('g');
  assert(running && !held && start_calls == 1 && core_launches == 1);
  assert(requested_clock == 20000000);
  command('l');
  assert(load_calls == 1 && running);
  receive_usb_byte('s');
  receive_usb_byte('1');
  receive_usb_byte('\r');
  receive_usb_byte('\n');
  receive_usb_byte(USB_COMMAND_ESCAPE);
  receive_usb_byte(USB_COMMAND_ESCAPE);
  assert(virtual_read(0, NULL) == 's');
  assert(virtual_read(0, NULL) == '1');
  assert(virtual_read(0, NULL) == '\r');
  assert(virtual_read(0, NULL) == USB_COMMAND_ESCAPE);
  assert(!(virtual_read(1, NULL) & 1u));
  command('s');
  assert(!(virtual_read(1, NULL) & 1u));
  for (uint8_t port = 0x10; port <= 0x14; ++port) {
    assert(virtual_read(port, NULL) == 0x5A && disk_last_port == port);
    virtual_write(port, 0xA5, NULL);
    assert(disk_last_port == port);
  }
  assert(disk_writes == 5);
  virtual_write(Z80_BOOT_REPORT_PORT, Z80_BOOT_REPORT_BIOS, NULL);
  virtual_write(Z80_BOOT_REPORT_PORT, Z80_BOOT_REPORT_CCP, NULL);
  service_run();
  assert(bios_announced && ccp_announced);
  assert(strstr(output, "not yet a CP/M PASS"));
  assert(strstr(output, "Observed BIOS handoff"));
  command('r');
  assert(!reboots && running);
}

static void test_usb_backpressure_and_disconnect(void) {
  reset_fixture();
  command('l');
  command('g');
  virtual_write(0, 'A', NULL);
  usb_room = 0;
  usb_was_connected = true;
  size_t previous_length = output_length;
  service_usb();
  assert(output_length == previous_length);
  connected = false;
  service_usb();
  assert(!(virtual_read(1, NULL) & 0x80u));
  connected = true;
  usb_room = 1;
  service_usb();
  assert(output[output_length - 1] == 'A');
  assert(virtual_read(1, NULL) & 0x80u);
  for (unsigned index = 0; index < 129; ++index)
    receive_usb_byte('X');
  service_usb();
  assert(z80_terminal_rx_drop_count() == 1);
  assert(strstr(output, "FAIL: terminal overflow"));
}

static void test_stop_and_failure(void) {
  reset_fixture();
  command('l');
  command('g');
  command('x');
  assert(running && stop_requested && flash_pause_request);
  quiescent = false;
  service_run();
  assert(running && flushes == 1);
  flash_paused = 1;
  service_run();
  assert(running && flash_resuming && !flash_pause_request);
  flash_paused = 0;
  service_run();
  assert(!flash_resuming && flash_pause_request);
  quiescent = true;
  flash_paused = 1;
  service_run();
  assert(held && !running && !interrupts_disabled && !image_verified);
  command('g');
  assert(start_calls == 1);
  command('r');
  assert(reboots == 1);

  reset_fixture();
  load_ok = false;
  command('l');
  command('g');
  assert(held && !running && !start_calls);
  assert(strstr(output, "SRAM mismatch"));
  reset_fixture();
  input[0] = 'x';
  input_length = 1;
  command('l');
  assert(held && !image_verified && !core_launches);
  assert(strstr(output, "cancelled"));

  reset_fixture();
  command('l');
  command('g');
  now = 10000000;
  quiescent = false;
  service_run();
  assert(boot_timed_out && stop_requested && running);
  size_t length = output_length;
  service_run();
  assert(length == output_length);
  fatal = true;
  service_run();
  assert(held && !running);
}

static void test_disk_diagnostic(void) {
  reset_fixture();
  command('t');
  assert(!running);
  command('l');
  command('t');
  assert(diagnostic_active && running && !image_verified && !core_launches);
  assert(requested_clock == 20000000);
  test_flash[Z80_FLASH_DISK_OFFSET] = 0xA5;
  virtual_write(6, 0xA4, NULL);
  service_run();
  assert(held && !running && diagnostic_count == 1);
  assert(diagnostic_fault_offset == 0 && diagnostic_expected == 0xA5);
  assert(strstr(output, "FAIL: disk bytes"));
  reset_fixture();
  command('l');
  command('t');
  diagnostic_count = DIAGNOSTIC_BYTES;
  virtual_write(5, 0x5A, NULL);
  service_run();
  assert(held && !running && strstr(output, "PASS: disk bytes"));
}

int main(void) {
  test_control_and_terminal();
  test_usb_backpressure_and_disconnect();
  test_stop_and_failure();
  test_disk_diagnostic();
  puts("PASS: Phase 9 staged load, nominal 20 MHz start, real terminal queues, "
       "USB framing/backpressure, boot evidence, cancellation and safe stop");
  return 0;
}
