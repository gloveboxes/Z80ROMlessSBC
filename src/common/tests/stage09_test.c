#include <assert.h>
#include <setjmp.h>
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
static bool trap_ok;
static bool run_ok;
static bool exit_main;
static jmp_buf main_exit;
static z80_boot_error_t load_error;
static uint64_t now;
static unsigned service_reads;
static unsigned service_writes;
static unsigned usb_room;
static char output[16384];
static size_t output_length;
static uint8_t input[64];
static size_t input_index;
static size_t input_length;

void z80_io_ports_init(void) {}
bool z80_io_ports_handles(uint8_t port) {
  port &= 0x17;
  return (port >= 2 && port <= 4) || port == 0x17;
}
uint8_t z80_io_port_in(uint8_t port) {
  (void)port;
  ++service_reads;
  return 0x42;
}
void z80_io_port_out(uint8_t port, uint8_t value) {
  (void)port; (void)value;
  ++service_writes;
}
void z80_io_ports_core1_service(void) {}
bool z80_io_ports_quiescent(void) { return true; }
bool z80_io_ports_reboot_requested(void) { return false; }

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
  assert(boot_attempted && image_verified && held);
  ++core_launches;
}
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  requested_clock = hz;
  ++start_calls;
  if (run_ok)
    held = false;
  return run_ok;
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
  return trap_ok;
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
absolute_time_t get_absolute_time(void) { return now; }
bool time_reached(absolute_time_t deadline) { return now >= deadline; }
void sleep_ms(uint32_t delay) { now += delay * 1000u; }
void tight_loop_contents(void) {
  if (exit_main)
    longjmp(main_exit, 1);
}
const z80_boot_status_t *z80_flash_boot_status(void) { return &status; }
bool z80_flash_storage_init_observed(z80_boot_observer_t observer,
                                     void *context) {
  assert(observer == observe_boot && context == NULL);
  assert(held && !start_calls && !core_launches);
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
    status.error = load_error;
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
  boot_failure = NULL;
  diagnostic_active = false;
  boot_markers = flash_pause_request = flash_paused = 0;
  reported_rx_drops = reported_tx_drops = 0;
  load_calls = start_calls = core_launches = disk_writes = flushes = reboots = 0;
  requested_clock = 0;
  connected = held = quiescent = load_ok = trap_ok = run_ok = true;
  fatal = interrupts_disabled = exit_main = false;
  load_error = Z80_BOOT_ERROR_MISMATCH;
  now = 0;
  usb_room = 1024;
  fake_disk_status = DISK_READY;
  output_length = input_index = input_length = 0;
  output[0] = '\0';
  memset(&status, 0, sizeof(status));
  ansi_input_reset();
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
  if (!Z80_STAGE09_AUTOBOOT) {
    assert(strstr(output, "not yet a CP/M PASS"));
    assert(strstr(output, "Observed BIOS handoff"));
  }
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

static void usb_input(const char *bytes) {
  input_length = strlen(bytes);
  assert(input_length <= sizeof(input));
  memcpy(input, bytes, input_length);
  input_index = 0;
  service_usb();
  assert(input_index == input_length);
}

static void test_ansi_terminal_input(void) {
  static const struct {
    const char *sequence;
    uint8_t expected;
  } mappings[] = {
    {"\x1b[A", 0x05}, {"\x1b[B", 0x18},
    {"\x1b[C", 0x04}, {"\x1b[D", 0x13},
    {"\x1b[2~", 0x0f}, {"\x1b[3~", 0x07},
    {"\x1b[5~", 0x12}, {"\x1b[6~", 0x16},
    {"\x7f", 0x08}, {"\x08", 0x08},
  };
  reset_fixture();
  command('l');
  command('g');
  service_usb();
  for (size_t index = 0; index < sizeof(mappings) / sizeof(mappings[0]);
       ++index) {
    usb_input(mappings[index].sequence);
    assert(virtual_read(1, NULL) & 1u);
    assert(virtual_read(0, NULL) == mappings[index].expected);
    assert(!(virtual_read(1, NULL) & 1u));
  }

  usb_input("\x1b");
  now = (ANSI_INPUT_ESC_GRACE_MS - 1u) * 1000;
  usb_input("");
  assert(!(virtual_read(1, NULL) & 1u));
  now += 1000;
  usb_input("");
  assert(virtual_read(0, NULL) == 0x1b);
  usb_input("");
  assert(!(virtual_read(1, NULL) & 1u));

  usb_input("\x1b");
  now += 1000;
  usb_input("[");
  now += 1000;
  usb_input("3");
  assert(!(virtual_read(1, NULL) & 1u));
  usb_input("~");
  assert(virtual_read(0, NULL) == 0x07);
  memset(input, 'a', 31);
  memcpy(input + 31, "\x1b[A", 3);
  input_index = 0;
  input_length = 34;
  service_usb();
  assert(input_index == 32);
  for (unsigned index = 0; index < 31; ++index)
    assert(virtual_read(0, NULL) == 'a');
  assert(!(virtual_read(1, NULL) & 1u));
  service_usb();
  assert(input_index == input_length);
  assert(virtual_read(0, NULL) == 0x05);
  assert(!(virtual_read(1, NULL) & 1u));
  usb_input("\x1b[Z\x1b[2!Q");
  assert(virtual_read(0, NULL) == 'Q');
  assert(!(virtual_read(1, NULL) & 1u));

  usb_input("DIR\r\n\x03\x13\x11\x1a");
  static const uint8_t expected[] = {'D', 'I', 'R', '\r', 3, 19, 17, 26};
  for (size_t index = 0; index < sizeof(expected); ++index)
    assert(virtual_read(0, NULL) == expected[index]);
  assert(!(virtual_read(1, NULL) & 1u));

  usb_input("\x1b[");
  command('s');
  usb_input("A");
  assert(virtual_read(0, NULL) == 'A');
  assert(!(virtual_read(1, NULL) & 1u));

  usb_input("\x1b[");
  connected = false;
  service_usb();
  connected = true;
  usb_input("B");
  assert(virtual_read(0, NULL) == 'B');
  assert(!(virtual_read(1, NULL) & 1u));

  now = (uint64_t)(UINT32_MAX - 10u) * 1000;
  usb_input("\x1b");
  now += ANSI_INPUT_ESC_GRACE_MS * 1000;
  usb_input("");
  assert(virtual_read(0, NULL) == 0x1b);
  assert(!(virtual_read(1, NULL) & 1u));
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
  const char *stopped = strstr(output, "Disk quiescent; RESET held LOW");
  assert(stopped != NULL);
  assert(strstr(stopped, "l=load/verify only, g=run verified CP/M, s=status"));
  assert(strstr(stopped, "t=read-only all-drive disk test"));
  assert(strstr(stopped, "x=flush then hold RESET, r=reboot when stopped, h=help"));
  assert(strstr(stopped, "0=disarm, 1-8=arm one-shot flash fault"));
  assert(strstr(stopped, "Stopped: Ctrl-] r=reboot, s=status, h=help."));
  assert(strstr(stopped, "Loading/running/testing (l/g/t) requires a Pico reboot"));
  assert(strstr(stopped, "fault commands (0-8) require a running CPU"));
  assert(strstr(stopped, "ANSI arrows=") == NULL);
  assert(strstr(stopped, "standalone Esc waits") == NULL);
  size_t stopped_length = output_length;
  command('x');
  assert(strstr(output + stopped_length, "Stopped: Ctrl-] r=reboot, s=status, h=help."));
  assert(held && !running && flushes == 1 && !reboots);
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
  assert(held && !core_launches);
  if (Z80_STAGE09_AUTOBOOT) {
    assert(image_verified && input_index == 0);
  } else {
    assert(!image_verified);
    assert(strstr(output, "cancelled"));
  }

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

static void run_firmware_once(void) {
  exit_main = true;
  if (setjmp(main_exit) == 0)
    stage09_firmware_main();
  exit_main = false;
}

static void test_autoboot_progress(void) {
  if (!Z80_STAGE09_AUTOBOOT)
    return;
  reset_fixture();
  input[0] = 'x';
  input_length = 1;
  for (unsigned phase = Z80_BOOT_LOAD; phase <= Z80_BOOT_VERIFY; ++phase) {
    for (uint32_t completed = 0; completed <= 65536; completed += 256) {
      status = (z80_boot_status_t){
        .phase = phase, .completed = completed, .total = 65536,
      };
      size_t previous_length = output_length;
      assert(observe_boot(&status, NULL));
      assert(held && !start_calls && !core_launches && input_index == 0);
      if (completed % 4096 != 0) {
        assert(output_length == previous_length);
        continue;
      }
      char expected[128];
      snprintf(expected, sizeof(expected), "[boot] SRAM %s: %lu/65536\n",
               phase == Z80_BOOT_LOAD ? "load" : "verify",
               (unsigned long)completed);
      assert(strcmp(output + previous_length, expected) == 0);
    }
  }
  connected = false;
  size_t previous_length = output_length;
  status.completed = 4096;
  assert(observe_boot(&status, NULL));
  assert(output_length == previous_length);
  connected = true;
  status.completed = 8192;
  assert(observe_boot(&status, NULL));
  assert(strcmp(output + previous_length,
                "[boot] SRAM verify: 8192/65536\n") == 0);
  status.phase = Z80_BOOT_VERIFIED;
  status.completed = 65536;
  assert(observe_boot(&status, NULL));
  assert(strstr(output, "[boot] verified; RESET held LOW: 65536/65536\n"));
}

static void test_startup_without_usb(void) {
  reset_fixture();
  connected = false;
  input[0] = 'x';
  input_length = 1;
  run_firmware_once();
  assert(input_index == 0);
  if (!Z80_STAGE09_AUTOBOOT) {
    assert(!load_calls && !start_calls && held && !core_launches);
    return;
  }
  assert(load_calls == 1 && start_calls == 1 && core_launches == 1);
  assert(running && image_verified && !held && requested_clock == 20000000);
  assert(output_length == 0);
  input_length = 0;
  static const char banner[] = "64K CP/M 2.2\r\nA>";
  for (size_t index = 0; index < sizeof(banner) - 1; ++index)
    virtual_write(0, (uint8_t)banner[index], NULL);
  virtual_write(Z80_BOOT_REPORT_PORT, Z80_BOOT_REPORT_BIOS, NULL);
  virtual_write(Z80_BOOT_REPORT_PORT, Z80_BOOT_REPORT_CCP, NULL);
  now = 60000000;
  service_usb();
  service_run();
  assert(bios_announced && ccp_announced && !boot_timed_out && running);
  assert(output_length == 0 && !(virtual_read(1, NULL) & 0x80u));
  connected = true;
  service_usb();
  assert(strcmp(output, banner) == 0);
  assert(virtual_read(1, NULL) & 0x80u);
  connected = false;
  service_usb();
  connected = true;
  service_usb();
  assert(strcmp(output, banner) == 0);
  assert(load_calls == 1 && start_calls == 1);
}

static void test_autoboot_failures(void) {
  if (!Z80_STAGE09_AUTOBOOT)
    return;
  static const z80_boot_error_t errors[] = {
    Z80_BOOT_ERROR_JOURNAL, Z80_BOOT_ERROR_MANIFEST,
    Z80_BOOT_ERROR_PAYLOAD_CRC, Z80_BOOT_ERROR_LOADER,
    Z80_BOOT_ERROR_WRITE, Z80_BOOT_ERROR_READ,
    Z80_BOOT_ERROR_MISMATCH, Z80_BOOT_ERROR_FLASH_INIT,
  };
  for (size_t index = 0; index < sizeof(errors) / sizeof(errors[0]); ++index) {
    reset_fixture();
    connected = load_ok = false;
    load_error = errors[index];
    run_firmware_once();
    assert(held && !running && !image_verified);
    assert(load_calls == 1 && !start_calls && !core_launches);
    assert(status.error == errors[index]);
    assert(boot_failure != NULL && strstr(output, "FAIL"));
    output[0] = '\0';
    output_length = 0;
    connected = true;
    service_usb();
    assert(strstr(output, "FAIL: automatic CP/M boot stopped"));
    assert(strstr(output, boot_error_name(errors[index])));
    assert(strstr(output, "phase=2 completed=4096"));
    service_run();
    service_usb();
    assert(load_calls == 1 && !start_calls);
  }
  for (unsigned failure = 0; failure < 2; ++failure) {
    reset_fixture();
    connected = false;
    trap_ok = failure != 0;
    run_ok = failure == 0;
    run_firmware_once();
    assert(held && !running && !image_verified);
    assert(load_calls == 1 && core_launches == 1);
    assert(start_calls == failure);
    output[0] = '\0';
    output_length = 0;
    connected = true;
    service_usb();
    assert(strstr(output, "FAIL: automatic CP/M boot stopped"));
    assert(strstr(output, "failure=trap/clock start"));
  }

  reset_fixture();
  connected = false;
  run_firmware_once();
  now = 10000000;
  service_run();
  assert(boot_timed_out && stop_requested && running && flash_pause_request);
  flash_paused = 1;
  service_run();
  assert(held && !running);
  output[0] = '\0';
  output_length = 0;
  connected = true;
  service_usb();
  assert(strstr(output, "FAIL: automatic CP/M boot stopped"));
  assert(strstr(output, "failure=BIOS/CCP evidence timeout"));
  assert(load_calls == 1 && start_calls == 1);

  reset_fixture();
  connected = false;
  run_firmware_once();
  fatal = true;
  service_run();
  assert(held && !running);
  output[0] = '\0';
  output_length = 0;
  connected = true;
  service_usb();
  assert(strstr(output, "FAIL: automatic CP/M boot stopped"));
  assert(strstr(output, "failure=fatal storage error"));
}

int main(void) {
  for (unsigned address = 0; address < 256; ++address) {
    uint8_t port = (uint8_t)(address & 0x17);
    if (z80_io_ports_handles(port)) {
      unsigned old_writes = service_writes;
      unsigned old_reads = service_reads;
      virtual_write(port, 0xa5, NULL);
      assert(service_writes == old_writes + 1);
      assert(virtual_read(port, NULL) == 0x42);
      assert(service_reads == old_reads + 1);
    }
  }
  test_startup_without_usb();
  test_autoboot_progress();
  test_autoboot_failures();
  test_control_and_terminal();
  test_usb_backpressure_and_disconnect();
  test_ansi_terminal_input();
  test_stop_and_failure();
  test_disk_diagnostic();
  puts("PASS: Phase 9 startup policy, nominal 20 MHz start, real terminal queues, "
       "USB framing/backpressure, ANSI key mapping/Escape timing, "
       "boot evidence, cancellation and safe stop");
  return 0;
}
