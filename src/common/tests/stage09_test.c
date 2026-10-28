#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/util/queue.h"
#include "hardware/flash.h"

static int firmware_printf(const char *format, ...);
static unsigned fake_core;
static bool connected;
#define service_usb firmware_service_usb
#define receive_usb_byte firmware_receive_usb_byte
#define printf firmware_printf
#define main stage09_firmware_main
#include "../../stage09_flash_storage/main.c"
#undef main
#undef printf
#undef receive_usb_byte
#undef service_usb

static void drain_control(void) {
  while (queue_get_level(&control_commands))
    service_control();
}

static void service_usb(void) {
  publish_cpu_state();
  fake_core = 1;
  firmware_service_usb();
  fake_core = 0;
  drain_control();
}

static void receive_usb_byte(uint8_t value) {
  publish_cpu_state();
  usb_state = connected ? USB_CONNECTED : 0;
#ifdef Z80_STAGE10_NETWORK
  if (z80_terminal_browser_connected())
    usb_state |= USB_BROWSER;
#endif
  fake_core = 1;
  firmware_receive_usb_byte(value);
  fake_core = 0;
  drain_control();
}

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
static bool quiescent;
static bool fatal;
static bool interrupts_disabled;
static bool load_ok;
static bool trap_ok;
static bool run_ok;
static bool exit_main;
static bool usb_init_ok = true;
static bool flash_init_ok = true;
static uint32_t launch_ready_state = 1;
static unsigned usb_services, disk_services, io_services, network_services;
static jmp_buf main_exit;
static z80_boot_error_t load_error;
static uint64_t now;
uint64_t z80_service_millis(void) { return now / 1000; }
static unsigned service_reads;
static unsigned service_writes;
static unsigned service_probes;
static uint8_t service_read_port, service_write_port, service_write_value;
static unsigned usb_room;
static unsigned disconnect_after_usb_writes;
static unsigned usb_write_calls, usb_write_limit;
static bool watchdog_reset;
static z80_io_trap_reset_fault_t retained_reset_fault;
static uint32_t fake_inactive_irqs;
static char output[16384];
static size_t output_length;
static uint8_t input[64];
static size_t input_index;
static size_t input_length;

#ifdef Z80_STAGE10_NETWORK
static z80_web_reboot_status_t web_reboot_status;
z80_web_reboot_status_t z80_web_terminal_reboot_status(void) { return web_reboot_status; }
bool z80_web_terminal_begin_reboot(void) {
  if (web_reboot_status != Z80_WEB_REBOOT_REQUESTED) return false;
  web_reboot_status = Z80_WEB_REBOOT_FLUSHING;
  return true;
}
void z80_web_terminal_set_reboot_status(z80_web_reboot_status_t value) {
  if (web_reboot_status != Z80_WEB_REBOOT_TIMEOUT)
    web_reboot_status = value;
}
static unsigned terminal_network_services;
static unsigned setup_requests;
static bool setup_reboot_request;
static unsigned matrix_status_requests;
static unsigned matrix_services;
static bool matrix_updates_allowed;
static const char *network_address = "";
size_t z80_io_ports_get_network_address(char *address, size_t capacity) {
  int length = snprintf(address, capacity, "%s", network_address);
  return length > 0 && (size_t)length < capacity ? (size_t)length : 0;
}
void z80_network_setup_init(void) {}
void z80_network_setup_request(void) {
  assert(fake_core == 1);
  ++setup_requests;
}
void z80_network_setup_usb_service(bool usb, bool active) {
  assert(fake_core == 1);
  (void)usb; (void)active;
}
bool z80_network_setup_input(uint8_t value) { (void)value; return false; }
bool z80_network_setup_active(void) { return false; }
bool z80_network_setup_reboot_requested(void) {
  assert(fake_core == 1);
  return setup_reboot_request;
}
void z80_network_setup_reboot_queued(void) {
  assert(fake_core == 1);
  setup_reboot_request = false;
}
bool z80_network_setup_quiescent(void) { return true; }
void z80_network_setup_core1_service(void) {}
void z80_network_terminal_core1_service(void) {
  assert(fake_core == 1 && workers_ready && flash_pause_request);
  ++terminal_network_services;
}
void z80_network_core1_service(void) {
  assert(fake_core == 1 && disk_services && io_services);
  ++network_services;
}
void z80_matrix_print_status(void) {
  assert(fake_core == 0);
  ++matrix_status_requests;
  firmware_printf("[matrix] test status\n");
}
void z80_matrix_core1_service(bool updates_allowed) {
  assert(fake_core == 1 && disk_services && io_services);
  ++matrix_services;
  matrix_updates_allowed = updates_allowed;
}
static bool web_rx_active = true;
z80_web_terminal_stats_t z80_web_terminal_get_stats(void) {
  return (z80_web_terminal_stats_t){
      .retransmits = 1, .flush_failures = 3, .ack_timeouts = 6,
      .pong_timeouts = 2,
      .window_frames = 2, .window_frames_peak = 4,
      .retained_bytes = 1039, .retained_bytes_peak = 2048, .staged_bytes = 15,
      .rx_queue_active = web_rx_active,
      .rx_queue_message_limit = 64, .rx_queue_byte_limit = 1024,
  };
}
#endif

void z80_io_ports_init(void) {}
bool z80_io_ports_handles(uint8_t port) {
  ++service_probes;
  port &= 0x17;
  return (port >= 2 && port <= 4) || (port >= 0x15 && port <= 0x17);
}
uint8_t z80_io_port_in(uint8_t port) {
  assert(fake_core == 0);
  service_read_port = port;
  ++service_reads;
  return 0x42;
}
void z80_io_port_out(uint8_t port, uint8_t value) {
  assert(fake_core == 0);
  service_write_port = port;
  service_write_value = value;
  ++service_writes;
}
void z80_io_ports_core1_service(void) {
  assert(fake_core == 1 && disk_services);
  ++io_services;
}
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
unsigned z80_memory_bank_status(void) { return 0xB4; }
void z80_safe_startup(void) { assert(fake_core == 0); held = true; }
void z80_cpu_fail_closed(void) { assert(fake_core == 0); held = true; }
void stdio_init_all(void) {}
uint get_core_num(void) { return fake_core; }
void z80_usb_console_prepare(void) { assert(fake_core == 0); }
bool z80_usb_console_init(void) { assert(fake_core == 1); return usb_init_ok; }
void z80_usb_console_service(void) {
  assert(fake_core == 1);
  ++usb_services;
}
uint32_t z80_usb_console_log_drops(void) { return 0; }
bool flash_safe_execute_core_init(void) {
  assert(fake_core == 1);
  return flash_init_ok;
}
bool stdio_usb_connected(void) { assert(fake_core == 1); return connected; }
uint32_t tud_cdc_write_available(void) {
  assert(fake_core == 1);
  return usb_room;
}
int getchar_timeout_us(uint32_t timeout) {
  assert(fake_core == 1 && timeout == 0);
  return input_index < input_length ? input[input_index++] : -1;
}
int putchar_raw(int value) {
  assert(fake_core == 1);
  assert(connected && usb_room && output_length + 1 < sizeof(output));
  output[output_length++] = (char)value;
  output[output_length] = '\0';
  --usb_room;
  if (disconnect_after_usb_writes && --disconnect_after_usb_writes == 0)
    connected = false;
  return value;
}
size_t z80_usb_console_write(const uint8_t *data, size_t length) {
  ++usb_write_calls;
  size_t accepted = 0;
  while (connected && usb_room && accepted < length &&
         accepted < usb_write_limit)
    putchar_raw(data[accepted++]);
  return accepted;
}
bool watchdog_caused_reboot(void) { return watchdog_reset; }
z80_io_trap_reset_fault_t z80_io_trap_take_reset_fault(void) {
  z80_io_trap_reset_fault_t fault = retained_reset_fault;
  retained_reset_fault = (z80_io_trap_reset_fault_t){0};
  return fault;
}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  assert(fake_core == 0 && pc == 0 && sp == 0 && delay == 0 && !running);
  ++reboots;
}
void multicore_launch_core1(void (*entry)(void)) {
  assert(entry == core1_main);
  assert(fake_core == 0 && !boot_attempted && held);
  ++core_launches;
  core1_ready = launch_ready_state;
}
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack,
                                      size_t stack_size) {
  assert(((uintptr_t)stack & 7u) == 0 && stack_size == 8192);
  multicore_launch_core1(entry);
}
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  assert(fake_core == 0);
  requested_clock = hz;
  ++start_calls;
  if (run_ok)
    held = false;
  return run_ok;
}
bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length) {
  assert(fake_core == 0);
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
  assert(fake_core == 0 && read_handler == virtual_read &&
         write_handler == virtual_write);
  assert(context == NULL);
  return trap_ok;
}
void z80_io_trap_disable(void) {}
uint32_t z80_io_trap_timeout_count(void) { return 0; }
uint32_t z80_io_trap_control_error_count(void) { return fake_inactive_irqs; }
uint32_t z80_io_trap_inactive_irq_count(void) { return fake_inactive_irqs; }
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
void busy_wait_us_32(uint32_t delay) { now += delay; }
void tight_loop_contents(void) {
  if (exit_main)
    longjmp(main_exit, 1);
}
const z80_boot_status_t *z80_flash_boot_status(void) { return &status; }
bool z80_flash_storage_init_observed(z80_boot_observer_t observer,
                                     void *context) {
  assert(observer == observe_boot && context == NULL);
  assert(fake_core == 0 && held && !start_calls);
  ++load_calls;
  held = true;
  status = (z80_boot_status_t){
    .phase = Z80_BOOT_LOAD, .completed = 4096, .total = 65536,
  };
  fake_core = 1;
  firmware_service_usb();
  fake_core = 0;
  if (!observer(&status, context)) {
    status.error = Z80_BOOT_ERROR_CANCELLED;
    return false;
  }
  if (!load_ok)
    status.error = load_error;
  return load_ok;
}
void z80_flash_core0_service(void) {}
void z80_flash_core1_service(void) {
  assert(fake_core == 1);
  ++disk_services;
}
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
  fake_core = 0;
  runtime_started = loading = startup_failed = false;
  cpu_state = load_cancelled = core1_ready = workers_ready = 0;
  usb_state = USB_CONNECTED;
  previous_cpu_state = 0;
  usb_init_ok = flash_init_ok = true;
  launch_ready_state = 1;
  usb_services = disk_services = io_services = network_services = 0;
  queue_init(&control_commands, sizeof(uint16_t), 4);
  boot_attempted = image_verified = running = core1_started = false;
  command_pending = usb_was_connected = stop_requested = false;
  flush_submitted = flash_resuming = bios_announced = ccp_announced = false;
  boot_timed_out = previous_cr = false;
  reset_was_watchdog = watchdog_reset = false;
  reset_fault = retained_reset_fault = (z80_io_trap_reset_fault_t){0};
  disconnect_after_usb_writes = 0;
  usb_write_calls = 0;
  usb_write_limit = 32;
  fake_inactive_irqs = 0;
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
  z80_terminal_set_connected(connected);
#ifdef Z80_STAGE10_NETWORK
  z80_terminal_usb_connected(connected);
  browser_was_connected = false;
  web_reboot_status = Z80_WEB_REBOOT_IDLE;
  web_reboot_active = false;
  terminal_network_services = 0;
  setup_requests = 0;
  setup_reboot_request = false;
  config_reboot_after_stop = config_reboot_ready = config_reboot_queue_full = false;
  matrix_status_requests = 0;
  matrix_services = 0;
  matrix_updates_allowed = false;
  network_address = "";
#endif
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
  assert(running && !held && start_calls == 1 && workers_ready);
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
  fake_inactive_irqs = 3;
  command('s');
  assert(strstr(output, "control_errors=3 inactive_irqs=3"));
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

static void test_status_uptime(void) {
  static const struct {
    uint64_t seconds;
    const char *expected;
  } cases[] = {
      {0, "00:00:00"},
      {1, "00:00:01"},
      {59, "00:00:59"},
      {60, "00:01:00"},
      {3599, "00:59:59"},
      {3600, "01:00:00"},
      {86399, "23:59:59"},
      {86400, "24:00:00"},
      {360000, "100:00:00"},
      {4320000, "1200:00:00"},
  };
  reset_fixture();
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    now = cases[i].seconds * 1000000 + 999999;
    output_length = 0;
    output[0] = '\0';
    command('s');
    char expected[64];
    snprintf(expected, sizeof(expected), "[system] uptime=%s\n", cases[i].expected);
    assert(strstr(output, expected));
  }
}

static void test_direct_port_dispatch(void) {
  reset_fixture();
  unsigned probes = service_probes;
  for (unsigned repeat = 0; repeat < 4096; ++repeat) {
    unsigned reads = service_reads, writes = service_writes;
    const uint8_t input_byte = 'I';
    z80_terminal_receive(&input_byte, 1);
    assert(virtual_read(1, NULL) & 1u);
    assert(virtual_read(0, NULL) == 'I');
    virtual_write(0, 'O', NULL);
    uint8_t output_byte;
    assert(z80_terminal_supply(&output_byte, 1) == 1 && output_byte == 'O');
    for (uint8_t port = 0x10; port <= 0x14; ++port) {
      assert(virtual_read(port, NULL) == 0x5A && disk_last_port == port);
      virtual_write(port, 0xA5, NULL);
      assert(disk_last_port == port);
    }
    assert(service_reads == reads && service_writes == writes);
  }
  assert(service_probes == probes);
  const uint8_t spare_ports[] = {5, 6, 7};
  unsigned reads = service_reads, writes = service_writes;
  unsigned disk_count = disk_writes;
  for (size_t i = 0; i < sizeof(spare_ports); ++i) {
    assert(virtual_read(spare_ports[i], NULL) == 0xFF);
    virtual_write(spare_ports[i], 0, NULL);
  }
  virtual_write(1, 0, NULL);
  assert(service_reads == reads && service_writes == writes);
  assert(disk_writes == disk_count);
}

static void test_usb_backpressure_and_disconnect(void) {
  reset_fixture();
  command('l');
  command('g');
  for (unsigned index = 0; index < 512; ++index)
    virtual_write(0, 'A', NULL);
  assert(!(virtual_read(1, NULL) & 2u));
  usb_room = 0;
  usb_was_connected = true;
  size_t previous_length = output_length;
  service_usb();
  assert(output_length == previous_length);
  connected = false;
  service_usb();
  assert(!(virtual_read(1, NULL) & 0x80u));
  assert(virtual_read(1, NULL) & 2u);
  for (unsigned index = 0; index < 4096; ++index) {
    assert(virtual_read(1, NULL) & 2u);
    virtual_write(0, 'B', NULL);
  }
  uint8_t value;
  assert(z80_terminal_supply(&value, 1) == 0);
  assert(z80_terminal_tx_drop_count() == 0);
  assert(running && !held);
  connected = true;
  usb_room = 1;
  service_usb();
  assert(virtual_read(1, NULL) & 0x80u);
  assert(usb_room == 1);
  assert(z80_terminal_supply(&value, 1) == 0);
  virtual_write(0, 'C', NULL);
  service_usb();
  assert(output[output_length - 1] == 'C' && usb_room == 0);
  for (unsigned index = 0; index < 513; ++index)
    virtual_write(0, 'D', NULL);
  assert(!(virtual_read(1, NULL) & 2u));
  assert(z80_terminal_tx_drop_count() == 1);
  connected = false;
  service_usb();
  assert(virtual_read(1, NULL) & 2u);
  assert(z80_terminal_supply(&value, 1) == 0);
  connected = true;
  service_usb();
  for (unsigned index = 0; index < 129; ++index)
    receive_usb_byte('X');
  service_usb();
  assert(z80_terminal_rx_drop_count() == 1);
  assert(strstr(output, "FAIL: terminal overflow"));
}

static void test_disconnect_during_output(void) {
  reset_fixture();
  command('l');
  command('g');
  usb_was_connected = true;
  for (unsigned cycle = 0; cycle < 32; ++cycle) {
    output_length = 0;
    output[0] = '\0';
    connected = true;
    usb_room = 1024;
    service_usb();
    for (unsigned index = 0; index < 512; ++index)
      virtual_write(0, 'A', NULL);
    assert(!(virtual_read(1, NULL) & 2u));
    size_t previous_length = output_length;
    disconnect_after_usb_writes = cycle % 32 + 1;
    service_usb();
    assert(!connected);
    assert(output_length - previous_length == cycle % 32 + 1);
    service_usb();
    assert(virtual_read(1, NULL) == 2u);
    for (unsigned index = 0; index < 4096; ++index)
      virtual_write(0, 'B', NULL);
    assert(z80_terminal_tx_drop_count() == 0);
    assert(running && !held && !reboots);
    uint8_t value;
    assert(z80_terminal_supply(&value, 1) == 0);
  }

}

static void test_usb_batched_output(void) {
  reset_fixture();
  command('l');
  command('g');
  usb_was_connected = true;
  for (unsigned i = 0; i < 64; ++i)
    virtual_write(0, (uint8_t)('A' + i % 26), NULL);
  size_t before = output_length;
  service_usb();
  assert(usb_write_calls == 1 && output_length == before + 32);
  assert(z80_terminal_get_buffer_stats().tx_used == 32);
  usb_write_limit = 3;
  service_usb();
  assert(usb_write_calls == 2 && output_length == before + 35);
  assert(z80_terminal_get_buffer_stats().tx_used == 29);
  usb_write_limit = 0;
  service_usb();
  assert(output_length == before + 35);
  assert(z80_terminal_get_buffer_stats().tx_used == 29);
  usb_write_limit = 32;
  service_usb();
  assert(output_length == before + 64);
  for (unsigned i = 0; i < 64; ++i)
    assert(output[before + i] == (char)('A' + i % 26));
  assert(z80_terminal_get_buffer_stats().tx_used == 0);
}

static void usb_input(const char *bytes) {
  input_length = strlen(bytes);
  assert(input_length <= sizeof(input));
  memcpy(input, bytes, input_length);
  input_index = 0;
  service_usb();
  assert(input_index == input_length);
}

static void test_websocket_disconnect_queues(void) {
  reset_fixture();
#ifdef Z80_STAGE10_NETWORK
  z80_terminal_usb_connected(false);
#endif
  z80_terminal_browser_connect();
  z80_terminal_receive((const uint8_t *)"X", 1);
  virtual_write(0, 'A', NULL);
  z80_terminal_browser_disconnect();
  assert(virtual_read(1, NULL) == 2u);
  for (unsigned index = 0; index < 4096; ++index) {
    virtual_write(0, 'B', NULL);
    assert(virtual_read(1, NULL) == 2u);
  }
  z80_terminal_browser_connect();
  uint8_t value;
  assert(z80_terminal_supply(&value, 1) == 0);
  virtual_write(0, 'C', NULL);
  assert(z80_terminal_supply(&value, 1) == 1 && value == 'C');
  assert(z80_terminal_tx_drop_count() == 0);
}

#ifdef Z80_STAGE10_NETWORK
static void test_browser_usb_routing(void) {
  reset_fixture();
  command('l');
  command('g');
  usb_input("U");
  assert(virtual_read(0, NULL) == 'U');
  usb_input("K");
  virtual_write(0, 'S', NULL);
  // A network-only event must not clear the USB owner's queues.
  z80_terminal_browser_disconnect();
  assert(virtual_read(0, NULL) == 'K');
  uint8_t value;
  assert(z80_terminal_usb_supply(&value, 1) == 1 && value == 'S');
  virtual_write(0, 'S', NULL);
  z80_terminal_browser_connect();
  assert(z80_terminal_browser_connected());
  assert(z80_terminal_supply(&value, 1) == 0);
  usb_input("\x1b[");
  z80_terminal_receive((const uint8_t *)"W", 1);
  assert(virtual_read(0, NULL) == 'W');
  assert(!(virtual_read(1, NULL) & 1u));
  size_t old_length = output_length;
  virtual_write(0, 'B', NULL);
  service_usb();
  assert(output_length == old_length);
  assert(z80_terminal_supply(&value, 1) == 1 && value == 'B');
  assert(matrix_status_requests == 0);
  command('s');
  assert(matrix_status_requests == 1);
  assert(strstr(output, "[matrix] test status"));
  assert(strstr(output, "browser=1"));
  assert(strstr(output, "ip=unavailable"));
  assert(strstr(output, "[network] Terminal:") == NULL);
  network_address = "192.0.2.42";
  command('s');
  assert(strstr(output, "browser=1 setup=0 ip=192.0.2.42"));
  assert(strstr(output, "[network] Terminal: http://192.0.2.42:8088/"));
  assert(strstr(output, "[web] retransmits=1 flush_failures=3 ack_timeouts=6 "
                        "pong_timeouts=2"));
  assert(strstr(output, "[webbuf] in_msgs=0/64 peak=0 in_bytes=0/1024 peak=0"));
  assert(strstr(output, "[webbuf] out_frames=2/4 peak=4 retained=1039/2048 "
                        "peak=2048 staged=15/512"));
  assert(strstr(output, "[consolebuf] rx="));
  assert(strstr(output, "/128 peak=") && strstr(output, "/512 peak="));
  network_address = "";
  output_length = 0;
  output[0] = '\0';
  web_rx_active = false;
  command('s');
  assert(strstr(output, "[webbuf] in=inactive"));
  assert(strstr(output, "in_msgs=") == NULL);
  assert(strstr(output, "ip=unavailable"));
  assert(strstr(output, "192.0.2.42") == NULL);
  assert(strstr(output, "[network] Terminal:") == NULL);
  command('n');
  assert(setup_requests == 0);
  command('c');
  assert(setup_requests == 1);
  z80_terminal_receive((const uint8_t *)"stale", 5);
  virtual_write(0, 'X', NULL);
  z80_terminal_browser_disconnect();
  assert(!z80_terminal_browser_connected());
  assert(virtual_read(1, NULL) == (0x80u | 2u));
  usb_input("R");
  assert(virtual_read(0, NULL) == 'R');
  virtual_write(0, 'T', NULL);
  service_usb();
  assert(output[output_length - 1] == 'T');
  z80_terminal_browser_connect();
  connected = false;
  service_usb();
  assert(z80_terminal_client_connected());
  z80_terminal_browser_disconnect();
  assert(!z80_terminal_client_connected());
}

static void test_browser_input_backpressure(void) {
  reset_fixture();
  z80_terminal_browser_connect();
  uint8_t first[64];
  uint8_t second[64];
  memset(first, 'A', sizeof(first));
  memset(second, 'B', sizeof(second));
  assert(z80_terminal_browser_try_receive(first, sizeof(first)));
  assert(z80_terminal_browser_try_receive(second, sizeof(second)));
  assert(!z80_terminal_browser_try_receive((const uint8_t *)"C", 1));
  assert(z80_terminal_rx_drop_count() == 0);
  for (unsigned i = 0; i < sizeof(first); ++i)
    assert(virtual_read(0, NULL) == 'A');
  assert(z80_terminal_browser_try_receive(first, sizeof(first)));
  for (unsigned i = 0; i < sizeof(second); ++i)
    assert(virtual_read(0, NULL) == 'B');
  for (unsigned i = 0; i < sizeof(first); ++i)
    assert(virtual_read(0, NULL) == 'A');
  z80_terminal_browser_disconnect();
  assert(!z80_terminal_browser_try_receive(first, sizeof(first)));
}
#endif

static void test_connected_output_buffer(void) {
  for (unsigned browser = 0; browser < 2; ++browser) {
    reset_fixture();
    if (browser)
      z80_terminal_browser_connect();
    uint8_t expected[512];
    for (unsigned index = 0; index < sizeof(expected); ++index) {
      expected[index] = (uint8_t)index;
      assert(virtual_read(1, NULL) & 2u);
      virtual_write(0, expected[index], NULL);
    }
    assert(!(virtual_read(1, NULL) & 2u));
    for (unsigned index = 0; index < 3; ++index)
      virtual_write(0, 'X', NULL);
    assert(z80_terminal_tx_drop_count() == 3);
    assert(!(virtual_read(1, NULL) & 2u));
    uint8_t actual[512];
    assert(z80_terminal_supply(actual, 32) == 32);
    assert(virtual_read(1, NULL) & 2u);
    virtual_write(0, 'R', NULL);
    assert(z80_terminal_supply(actual + 32, sizeof(actual) - 32) ==
           sizeof(actual) - 32);
    assert(memcmp(actual, expected, sizeof(expected)) == 0);
    uint8_t value;
    assert(z80_terminal_supply(&value, 1) == 1 && value == 'R');
    assert(z80_terminal_supply(actual, sizeof(actual)) == 0);
    assert(z80_terminal_tx_drop_count() == 3);
  }
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
    assert(image_verified && input_index == 1);
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

#ifdef Z80_STAGE10_NETWORK
static void test_matrix_running_gate(void) {
  reset_fixture();
  fake_core = 1;
  core1_service();
  assert(!matrix_services && !network_services);
  workers_ready = 1;
  const uint32_t states[] = {
      0, CPU_RUNNING, CPU_RUNNING | CPU_STOPPING, CPU_STOPPING,
      CPU_LOADING, CPU_RUNNING | CPU_LOADING, CPU_RUNNING,
  };
  for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
    cpu_state = states[i];
    core1_service();
    assert(matrix_services == i + 1 && network_services == i + 1);
    assert(matrix_updates_allowed == (states[i] == CPU_RUNNING));
  }
  unsigned polls = network_services;
  unsigned display_calls = matrix_services;
  flash_pause_request = 1;
  core1_service();
  assert(network_services == polls && matrix_services == display_calls + 1);
  assert(!matrix_updates_allowed && terminal_network_services == 1);
  core1_service();
  assert(network_services == polls && matrix_services == display_calls + 2);
  assert(!matrix_updates_allowed && terminal_network_services == 2);
  fake_core = 0;
}

static void test_web_orderly_reboot(void) {
  reset_fixture();
  command('l'); command('g');
  web_reboot_status = Z80_WEB_REBOOT_REQUESTED;
  service_control();
  assert(web_reboot_status == Z80_WEB_REBOOT_FLUSHING);
  assert(running && stop_requested && !reboots);
  service_run();
  assert(flushes == 1 && !reboots);
  quiescent = true; flash_paused = 1;
  service_run();
  assert(held && !running && web_reboot_status == Z80_WEB_REBOOT_RESTARTING);
  now += 1000000;
  service_control();
  assert(reboots == 1);

  reset_fixture();
  command('l'); command('g');
  web_reboot_status = Z80_WEB_REBOOT_REQUESTED;
  service_control();
  fatal = true;
  service_run(); service_control();
  assert(web_reboot_status == Z80_WEB_REBOOT_STORAGE_ERROR && !reboots);

  reset_fixture();
  command('l'); command('g');
  web_reboot_status = Z80_WEB_REBOOT_REQUESTED;
  service_control();
  now += 30000000;
  service_control();
  assert(web_reboot_status == Z80_WEB_REBOOT_TIMEOUT && !reboots);
  assert(stop_requested && !config_reboot_after_stop);
  quiescent = true; flash_paused = 1;
  service_run();
  now += 2000000; service_control();
  assert(held && !reboots);

  reset_fixture();
  command('l'); command('g');
  web_reboot_status = Z80_WEB_REBOOT_REQUESTED;
  service_control();
  web_reboot_status = Z80_WEB_REBOOT_TIMEOUT; // Core 1 expired the request.
  quiescent = true; flash_paused = 1;
  service_run(); // A stop completes before core 0 handles the expired status.
  assert(config_reboot_ready && held);
  now += 1000000;
  service_control();
  assert(!config_reboot_ready && !config_reboot_after_stop && !reboots);

  reset_fixture();
  web_reboot_status = Z80_WEB_REBOOT_REQUESTED;
  service_control();
  assert(web_reboot_status == Z80_WEB_REBOOT_REJECTED && !reboots);
}

static void test_config_orderly_reboot(void) {
  reset_fixture();
  command('l');
  command('g');
  setup_reboot_request = true;
  service_usb();
  assert(!setup_reboot_request && config_reboot_after_stop);
  assert(running && stop_requested && flash_pause_request && !reboots);
  quiescent = false;
  service_run();
  assert(flushes == 1 && !reboots);
  flash_paused = 1;
  service_run();
  assert(flash_resuming && running && !reboots);
  flash_paused = 0;
  service_run();
  quiescent = true;
  flash_paused = 1;
  service_run();
  assert(held && !running && config_reboot_ready && !reboots);
  assert(strstr(output, "Disks flushed; RESET held LOW, PWM stopped"));
  command('g');
  assert(!running && start_calls == 1);
  now += 999999;
  service_control();
  assert(!reboots);
  ++now;
  service_control();
  assert(reboots == 1);

  reset_fixture();
  command('l');
  command('g');
  setup_reboot_request = true;
  service_usb();
  fatal = true;
  service_run();
  now += 2000000;
  service_control();
  assert(held && !running && !reboots && !config_reboot_ready);
  assert(strstr(output, "automatic reboot cancelled"));

  reset_fixture();
  command('l');
  command('g');
  fatal = true;
  setup_reboot_request = true;
  service_usb();
  assert(!config_reboot_after_stop && !reboots);
  assert(strstr(output, "automatic reboot rejected"));

  reset_fixture();
  command('l');
  command('g');
  for (unsigned i = 0; i < 4; ++i) {
    uint16_t status_command = 's';
    assert(queue_try_add(&control_commands, &status_command));
  }
  setup_reboot_request = true;
  service_usb();
  assert(setup_reboot_request && !config_reboot_after_stop);
  assert(strstr(output, "queue full; retrying orderly reboot"));
  service_usb();
  assert(!setup_reboot_request && config_reboot_after_stop && !reboots);
}
#endif

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

static void test_reset_reporting(void) {
  static const char *const reasons[] = {
    "none", "release-timeout", "invalid-controls",
  };
  for (unsigned reason = 0; reason < 3; ++reason) {
    reset_fixture();
    connected = false;
    watchdog_reset = true;
    retained_reset_fault = (z80_io_trap_reset_fault_t){
      .reason = (z80_io_trap_reset_reason_t)reason,
      .gpio = reason ? 0x12345678 : 0,
    };
    run_firmware_once();
    assert(retained_reset_fault.reason == Z80_IO_TRAP_RESET_NONE);
    output_length = 0;
    output[0] = '\0';
    connected = true;
    service_usb();
    char expected[96];
    snprintf(expected, sizeof(expected),
             "[reset] watchdog=1 io_fault=%s gpio=%08lx\n",
             reasons[reason], (unsigned long)(reason ? 0x12345678 : 0));
    assert(strstr(output, "[reset]") == NULL);
    size_t previous_length = output_length;
    service_usb();
    assert(output_length == previous_length);
    command('s');
    assert(strstr(output + previous_length, expected));
    assert(!reboots);
    connected = false;
    service_usb();
    connected = true;
    output_length = 0;
    output[0] = '\0';
    service_usb();
    assert(strstr(output, "[reset]") == NULL);
    command('s');
    assert(strstr(output, expected));
  }
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
  usb_state = 0;
  size_t previous_length = output_length;
  status.completed = 4096;
  assert(observe_boot(&status, NULL));
  assert(output_length == previous_length);
  connected = true;
  usb_state = USB_CONNECTED;
  status.completed = 8192;
  assert(observe_boot(&status, NULL));
  assert(strcmp(output + previous_length,
                "[boot] SRAM verify: 8192/65536\n") == 0);
  status.phase = Z80_BOOT_VERIFIED;
  status.completed = 65536;
  assert(observe_boot(&status, NULL));
  assert(strstr(output, "[boot] verified; RESET held LOW: 65536/65536\n"));
  assert(!strstr(output, "bank 1 SRAM load complete:"));
  assert(!strstr(output, "bank 1 SRAM verify complete:"));
  assert(!strstr(output, "bank 1 ready:"));
}

static void test_startup_without_usb(void) {
  reset_fixture();
  connected = false;
  input[0] = 'x';
  input_length = 1;
  run_firmware_once();
  assert(input_index == 0);
  if (!Z80_STAGE09_AUTOBOOT) {
    assert(!load_calls && !start_calls && held && core_launches == 1);
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
  assert(virtual_read(1, NULL) & 2u);
  uint8_t value;
  assert(z80_terminal_supply(&value, 1) == 0);
  connected = true;
  service_usb();
  assert(output_length == 0);
  assert(virtual_read(1, NULL) & 0x80u);
  connected = false;
  service_usb();
  connected = true;
  service_usb();
  assert(output_length == 0);
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
    assert(load_calls == 1 && !start_calls && core_launches == 1);
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

static void test_core1_lifecycle_and_pause(void) {
  for (unsigned failure = 0; failure < 3; ++failure) {
    reset_fixture();
    usb_init_ok = failure != 1;
    flash_init_ok = failure != 2;
    fake_core = 1;
    exit_main = true;
    if (setjmp(main_exit) == 0)
      core1_main();
    fake_core = 0;
    exit_main = false;
    assert(core1_ready == (failure ? 2u : 1u));
    assert(usb_services == (failure == 1 ? 0u : 1u));
    assert(!disk_services && !io_services && !network_services);
    assert(held && !load_calls && !start_calls);
  }
  reset_fixture();
  workers_ready = 1;
  fake_core = 1;
  core1_service();
  assert(disk_services == 1 && io_services == 1 && usb_services == 1);
#ifdef Z80_STAGE10_NETWORK
  assert(network_services == 1);
#endif
  flash_pause_request = 1;
  core1_service();
  assert(flash_paused && disk_services == 2 && io_services == 2);
  assert(usb_services == 2);
  core1_service();
  assert(disk_services == 2 && io_services == 2 && usb_services == 3);
  flash_pause_request = 0;
  core1_service();
  assert(!flash_paused && disk_services == 3 && io_services == 3);
  assert(usb_services == 4);
#ifdef Z80_STAGE10_NETWORK
  assert(network_services == 2);
#endif
  fake_core = 0;
}

static void test_supervisor_queue_ownership(void) {
  reset_fixture();
  fake_core = 1;
  static const uint8_t commands[] = {'l', 'g', 'x', 's', 'r'};
  for (size_t i = 0; i < sizeof(commands); ++i) {
    firmware_receive_usb_byte(USB_COMMAND_ESCAPE);
    firmware_receive_usb_byte(commands[i]);
  }
  assert(queue_get_level(&control_commands) == 4);
  assert(!load_calls && !start_calls && !reboots && held);
  assert(strstr(output, "supervisor command queue full; retry command"));
  fake_core = 0;
  drain_control();
  assert(load_calls == 1 && start_calls == 1 && running && stop_requested);
  assert(!reboots);

  reset_fixture();
  loading = true;
  publish_cpu_state();
  fake_core = 1;
  firmware_receive_usb_byte('x');
  assert(load_cancelled == !Z80_STAGE09_AUTOBOOT);
  assert(!queue_get_level(&control_commands) && held);
  fake_core = 0;

  reset_fixture();
  startup_failed = true;
  command('l');
  command('g');
  assert(held && !load_calls && !start_calls && !workers_ready);
  assert(strstr(output, "initialization failed; CPU held reset"));

  reset_fixture();
  launch_ready_state = 2;
  run_firmware_once();
  assert(startup_failed && held && core_launches == 1);
  assert(!load_calls && !start_calls && !workers_ready);
  assert(strstr(output, "core 1 USB/flash-safe initialization; CPU held reset"));
}

static void test_http_port_dispatch(void) {
  reset_fixture();
  unsigned disks = disk_writes, probes = service_probes;
  /* Use FT.ASM's actual ABI, independently of the mock's handles predicate.
   * The previous emulator test called the driver directly and skipped these
   * firmware callbacks, leaving both registers mapped to the default FFh. */
  const uint8_t ports[] = {21, 22};
  const uint8_t values[] = {1, 'L', 'O', 'V', 'E', '.', 'B', 'A', 'S', 0, 3, 4};
  for (size_t i = 0; i < sizeof(ports); ++i) {
    for (size_t j = 0; j < sizeof(values); ++j) {
      unsigned writes = service_writes;
      virtual_write(ports[i], values[j], NULL);
      assert(service_writes == writes + 1);
      assert(service_write_port == ports[i] && service_write_value == values[j]);
    }
    unsigned reads = service_reads;
    assert(virtual_read(ports[i], NULL) == 0x42);
    assert(service_reads == reads + 1 && service_read_port == ports[i]);
  }
  assert(disk_writes == disks && service_probes == probes);
  assert(!io_services && !network_services);
}

int main(void) {
  test_http_port_dispatch();
#ifdef Z80_STAGE10_NETWORK
  test_matrix_running_gate();
  test_config_orderly_reboot();
  test_web_orderly_reboot();
#endif
  test_direct_port_dispatch();
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
  test_status_uptime();
  test_usb_backpressure_and_disconnect();
  test_usb_batched_output();
  test_disconnect_during_output();
  test_reset_reporting();
  test_websocket_disconnect_queues();
#ifdef Z80_STAGE10_NETWORK
  test_browser_usb_routing();
  test_browser_input_backpressure();
#endif
  test_connected_output_buffer();
  test_ansi_terminal_input();
  test_stop_and_failure();
  test_disk_diagnostic();
  test_core1_lifecycle_and_pause();
  test_supervisor_queue_ownership();
  puts("PASS: Phase 9 startup policy, nominal 20 MHz start, real terminal queues, "
       "USB framing/backpressure/mid-output disconnect, retained reset reports, "
       "ANSI key mapping/Escape timing, "
       "boot evidence, cancellation and safe stop");
  return 0;
}
