#include <stdio.h>
#include <string.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "z80sbc/cpu.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

enum {
  TERMINAL_DATA_PORT = 0x00,
  TERMINAL_STATUS_PORT = 0x01,
  TEST_RESULT_PORT = 0xFE,
  TERMINAL_RX_DEPTH = 128,
  TERMINAL_TX_DEPTH = 512,
  TERMINAL_STATUS_RX_READY = 1u << 0,
  TERMINAL_STATUS_TX_ROOM = 1u << 1,
  TERMINAL_STATUS_CONNECTED = 1u << 7,
  USB_COMMAND_ESCAPE = 0x1D,
  IO_MODE_RAM_CHECKER = 1,
  IO_MODE_SELF_TEST = 2,
  AUTOMATED_BOOT_CYCLES = 100,
};

static const uint8_t RAM_CHECK_PROGRAM[] = {
  0x31, 0xFE, 0xFF, 0x21, 0x00, 0x01, 0x36, 0x00,
  0x1E, 0x00, 0x16, 0x41, 0x1C, 0x34, 0x7E, 0xBB,
  0xC2, 0x33, 0x00, 0x7A, 0xD3, 0x00, 0xDB, 0x01,
  0xE6, 0x01, 0x28, 0x04, 0xDB, 0x00, 0xD3, 0x00,
  0x14, 0x7A, 0xFE, 0x5B, 0x20, 0x02, 0x16, 0x41,
  0x01, 0xFF, 0xFF, 0x0B, 0x78, 0xB1, 0x20, 0xFB,
  0xC3, 0x0C, 0x00, 0x3E, 0xE1, 0xD3, 0xFE, 0x76,
};

static const uint8_t SELF_TEST_PORTS[] = {0x00, 0x01, 0x55, 0xAA, 0xFF};

static queue_t terminal_rx_queue;
static queue_t terminal_tx_queue;
static uint8_t full_test_image[65536];
static uint8_t self_test_image[128];
static size_t self_test_image_length;
static uint32_t io_mode;
static uint32_t usb_connected;
static uint32_t usb_rx_drops;
static uint32_t usb_tx_drops;
static uint32_t boot_attempts;
static uint32_t dma_failures;
static uint32_t readback_failures;
static uint32_t ram_failures;
static uint32_t self_test_write_index;
static uint32_t self_test_errors;
static uint32_t self_test_complete;
static bool self_test_pending;
static absolute_time_t self_test_deadline;
static bool boot_cycle_active;
static unsigned int boot_cycles_remaining;
static unsigned int boot_cycles_passed;
static bool hour_test_active;
static absolute_time_t hour_test_started;
static absolute_time_t hour_test_deadline;
static uint32_t hour_trap_timeout_baseline;
static uint32_t hour_control_error_baseline;
static bool usb_command_pending;
static bool usb_was_connected;

static void clear_queue(queue_t *queue) {
  uint8_t discard;
  while (queue_try_remove(queue, &discard)) {}
}

static void clear_terminal_queues(void) {
  clear_queue(&terminal_rx_queue);
  clear_queue(&terminal_tx_queue);
}

static void terminal_receive(uint8_t value) {
  if (!queue_try_add(&terminal_rx_queue, &value)) {
    uint8_t discard;
    if (queue_try_remove(&terminal_rx_queue, &discard))
      __atomic_fetch_add(&usb_rx_drops, 1, __ATOMIC_RELAXED);
    if (!queue_try_add(&terminal_rx_queue, &value))
      __atomic_fetch_add(&usb_rx_drops, 1, __ATOMIC_RELAXED);
  }
}

static uint8_t virtual_read(uint8_t port, void *context) {
  (void)context;
  if (__atomic_load_n(&io_mode, __ATOMIC_ACQUIRE) == IO_MODE_SELF_TEST)
    return (uint8_t)(port ^ 0xA5u);
  if (port == TERMINAL_DATA_PORT) {
    uint8_t value = 0;
    queue_try_remove(&terminal_rx_queue, &value);
    return value;
  }
  if (port != TERMINAL_STATUS_PORT)
    return 0xFF;
  uint8_t status = 0;
  if (queue_get_level(&terminal_rx_queue) != 0)
    status |= TERMINAL_STATUS_RX_READY;
  if (queue_get_level(&terminal_tx_queue) < TERMINAL_TX_DEPTH)
    status |= TERMINAL_STATUS_TX_ROOM;
  if (__atomic_load_n(&usb_connected, __ATOMIC_ACQUIRE))
    status |= TERMINAL_STATUS_CONNECTED;
  return status;
}

static void virtual_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  if (__atomic_load_n(&io_mode, __ATOMIC_ACQUIRE) == IO_MODE_SELF_TEST) {
    if (port == TEST_RESULT_PORT) {
      bool passed = value == 0x5A &&
                    __atomic_load_n(&self_test_write_index,
                                    __ATOMIC_RELAXED) ==
                        sizeof(SELF_TEST_PORTS) &&
                    __atomic_load_n(&self_test_errors,
                                    __ATOMIC_RELAXED) == 0;
      if (!passed)
        __atomic_fetch_add(&self_test_errors, 1, __ATOMIC_RELAXED);
      __atomic_store_n(&self_test_complete, 1, __ATOMIC_RELEASE);
      return;
    }
    uint32_t index = __atomic_load_n(&self_test_write_index,
                                     __ATOMIC_RELAXED);
    if (index >= sizeof(SELF_TEST_PORTS) ||
        port != SELF_TEST_PORTS[index] || value != SELF_TEST_PORTS[index]) {
      __atomic_fetch_add(&self_test_errors, 1, __ATOMIC_RELAXED);
      return;
    }
    __atomic_store_n(&self_test_write_index, index + 1, __ATOMIC_RELAXED);
    return;
  }
  if (port == TEST_RESULT_PORT && value == 0xE1) {
    __atomic_fetch_add(&ram_failures, 1, __ATOMIC_RELAXED);
    return;
  }
  if (port == TERMINAL_DATA_PORT &&
      !queue_try_add(&terminal_tx_queue, &value))
    __atomic_fetch_add(&usb_tx_drops, 1, __ATOMIC_RELAXED);
}

static bool load_and_start(const uint8_t *program, size_t length,
                           uint32_t mode) {
  for (uint32_t address = 0; address < sizeof(full_test_image); ++address)
    full_test_image[address] =
        (uint8_t)address ^ (uint8_t)(address >> 8);
  memcpy(full_test_image, program, length);
  z80_io_trap_disable();
  z80_cpu_fail_closed();
  __atomic_fetch_add(&boot_attempts, 1, __ATOMIC_RELAXED);
  if (!z80_cpu_prepare_reset_dma() ||
      !z80_sram_load(0, full_test_image, sizeof(full_test_image))) {
    __atomic_fetch_add(&dma_failures, 1, __ATOMIC_RELAXED);
    return false;
  }
  if (!z80_sram_verify(0, full_test_image, sizeof(full_test_image))) {
    __atomic_fetch_add(&readback_failures, 1, __ATOMIC_RELAXED);
    return false;
  }
  clear_terminal_queues();
  __atomic_store_n(&io_mode, mode, __ATOMIC_RELEASE);
  return z80_io_trap_enable(virtual_read, virtual_write, NULL) &&
         z80_cpu_release_reset_and_run(1000000);
}

static bool start_ram_checker(void) {
  return load_and_start(RAM_CHECK_PROGRAM, sizeof(RAM_CHECK_PROGRAM),
                        IO_MODE_RAM_CHECKER);
}

static void emit_self_test_byte(uint8_t value) {
  self_test_image[self_test_image_length++] = value;
}

static void build_self_test_image(void) {
  size_t failure_patches[sizeof(SELF_TEST_PORTS)];
  self_test_image_length = 0;
  for (size_t index = 0; index < sizeof(SELF_TEST_PORTS); ++index) {
    uint8_t port = SELF_TEST_PORTS[index];
    emit_self_test_byte(0x3E);
    emit_self_test_byte(port);
    emit_self_test_byte(0xD3);
    emit_self_test_byte(port);
  }
  for (size_t index = 0; index < sizeof(SELF_TEST_PORTS); ++index) {
    uint8_t port = SELF_TEST_PORTS[index];
    uint16_t address = (uint16_t)(0x0200u + index);
    emit_self_test_byte(0xDB);
    emit_self_test_byte(port);
    emit_self_test_byte(0x32);
    emit_self_test_byte((uint8_t)address);
    emit_self_test_byte((uint8_t)(address >> 8));
    emit_self_test_byte(0x3A);
    emit_self_test_byte((uint8_t)address);
    emit_self_test_byte((uint8_t)(address >> 8));
    emit_self_test_byte(0xFE);
    emit_self_test_byte((uint8_t)(port ^ 0xA5u));
    emit_self_test_byte(0xC2);
    failure_patches[index] = self_test_image_length;
    emit_self_test_byte(0x00);
    emit_self_test_byte(0x00);
  }
  emit_self_test_byte(0x3E);
  emit_self_test_byte(0x5A);
  emit_self_test_byte(0xD3);
  emit_self_test_byte(TEST_RESULT_PORT);
  emit_self_test_byte(0x76);
  uint16_t failure_address = (uint16_t)self_test_image_length;
  emit_self_test_byte(0x3E);
  emit_self_test_byte(0xE1);
  emit_self_test_byte(0xD3);
  emit_self_test_byte(TEST_RESULT_PORT);
  emit_self_test_byte(0x76);
  for (size_t index = 0; index < sizeof(SELF_TEST_PORTS); ++index) {
    self_test_image[failure_patches[index]] = (uint8_t)failure_address;
    self_test_image[failure_patches[index] + 1] =
        (uint8_t)(failure_address >> 8);
  }
}

static bool start_self_test(void) {
  __atomic_store_n(&self_test_write_index, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&self_test_errors, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&self_test_complete, 0, __ATOMIC_RELEASE);
  bool started = load_and_start(self_test_image, self_test_image_length,
                                IO_MODE_SELF_TEST);
  self_test_pending = started;
  if (started)
    self_test_deadline = make_timeout_time_ms(500);
  return started;
}

static void print_status(void) {
  int64_t elapsed_us = hour_test_active
                           ? absolute_time_diff_us(hour_test_started,
                                                   get_absolute_time())
                           : 0;
  printf("\n[diag] boots=%lu dma_fail=%lu verify_fail=%lu ram_fail=%lu "
         "trap_timeout=%lu control_error=%lu rx_drop=%lu tx_drop=%lu "
         "hour_seconds=%llu\n",
         (unsigned long)__atomic_load_n(&boot_attempts, __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&dma_failures, __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&readback_failures, __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&ram_failures, __ATOMIC_RELAXED),
         (unsigned long)z80_io_trap_timeout_count(),
         (unsigned long)z80_io_trap_control_error_count(),
         (unsigned long)__atomic_load_n(&usb_rx_drops, __ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&usb_tx_drops, __ATOMIC_RELAXED),
         (unsigned long long)(elapsed_us > 0 ? elapsed_us / 1000000 : 0));
}

static void process_command(uint8_t command) {
  if (command == USB_COMMAND_ESCAPE) {
    terminal_receive(command);
  } else if (command == 's') {
    print_status();
  } else if (command == 'r') {
    boot_cycle_active = false;
    hour_test_active = false;
    printf(start_ram_checker() ? "\n[diag] RAM/USB checker started\n"
                               : "\n[diag] FAIL: RAM/USB checker\n");
  } else if (command == 'p') {
    boot_cycle_active = false;
    hour_test_active = false;
    printf(start_self_test() ? "\n[diag] port self-test started\n"
                             : "\n[diag] FAIL: port self-test start\n");
  } else if (command == 'b') {
    hour_test_active = false;
    boot_cycle_active = true;
    boot_cycles_remaining = AUTOMATED_BOOT_CYCLES;
    boot_cycles_passed = 0;
    bool started = start_self_test();
    if (!started)
      boot_cycle_active = false;
    printf(started ? "\n[diag] 100 boot cycles started\n"
                   : "\n[diag] FAIL: boot-cycle start\n");
  } else if (command == 'h') {
    boot_cycle_active = false;
    __atomic_store_n(&ram_failures, 0, __ATOMIC_RELAXED);
    hour_trap_timeout_baseline = z80_io_trap_timeout_count();
    hour_control_error_baseline = z80_io_trap_control_error_count();
    hour_test_active = start_ram_checker();
    if (hour_test_active) {
      hour_test_started = get_absolute_time();
      hour_test_deadline = make_timeout_time_ms(60u * 60u * 1000u);
    }
    printf(hour_test_active ? "\n[diag] one-hour RAM/USB test started\n"
                            : "\n[diag] FAIL: one-hour test start\n");
  } else if (command == 'x') {
    boot_cycle_active = false;
    hour_test_active = false;
    z80_io_trap_disable();
    z80_cpu_fail_closed();
    printf("\n[diag] CPU held in reset\n");
  }
}

static void service_usb(void) {
  bool connected = stdio_usb_connected();
  __atomic_store_n(&usb_connected, connected, __ATOMIC_RELEASE);
  if (usb_was_connected && !connected) {
    clear_terminal_queues();
    usb_command_pending = false;
  }
  usb_was_connected = connected;

  int input;
  while ((input = getchar_timeout_us(0)) >= 0) {
    uint8_t value = (uint8_t)input;
    if (usb_command_pending) {
      usb_command_pending = false;
      process_command(value);
    } else if (value == USB_COMMAND_ESCAPE) {
      usb_command_pending = true;
    } else if (__atomic_load_n(&io_mode, __ATOMIC_ACQUIRE) ==
               IO_MODE_RAM_CHECKER) {
      terminal_receive(value);
    }
  }

  if (!connected)
    return;
  for (unsigned int sent = 0; sent < 32; ++sent) {
    uint8_t value;
    if (!queue_try_remove(&terminal_tx_queue, &value))
      break;
    putchar_raw(value);
  }
}

static void service_self_test(void) {
  if (!self_test_pending)
    return;
  bool complete = __atomic_load_n(&self_test_complete, __ATOMIC_ACQUIRE);
  bool failed = __atomic_load_n(&self_test_errors, __ATOMIC_RELAXED) != 0;
  if (!complete && !failed && !time_reached(self_test_deadline))
    return;
  self_test_pending = false;
  if (!complete || failed) {
    boot_cycle_active = false;
    printf("\n[diag] FAIL: port self-test errors=%lu\n",
           (unsigned long)__atomic_load_n(&self_test_errors,
                                          __ATOMIC_RELAXED));
    return;
  }
  if (!boot_cycle_active) {
    printf("\n[diag] PASS: OUT/IN ports and SRAM stores\n");
    return;
  }
  ++boot_cycles_passed;
  if (--boot_cycles_remaining == 0) {
    boot_cycle_active = false;
    printf("\n[diag] PASS: %u reset-held boot/IO cycles\n",
           boot_cycles_passed);
    return;
  }
  if (!start_self_test()) {
    boot_cycle_active = false;
    printf("\n[diag] FAIL: boot cycle %u start\n", boot_cycles_passed + 1);
  }
}

static void service_hour_test(void) {
  if (!hour_test_active || !time_reached(hour_test_deadline))
    return;
  hour_test_active = false;
  bool passed = __atomic_load_n(&ram_failures, __ATOMIC_RELAXED) == 0 &&
                z80_io_trap_timeout_count() == hour_trap_timeout_baseline &&
                z80_io_trap_control_error_count() ==
                    hour_control_error_baseline;
  printf(passed ? "\n[diag] PASS: one-hour RAM/USB test\n"
                : "\n[diag] FAIL: one-hour RAM/USB test\n");
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  queue_init(&terminal_rx_queue, sizeof(uint8_t), TERMINAL_RX_DEPTH);
  queue_init(&terminal_tx_queue, sizeof(uint8_t), TERMINAL_TX_DEPTH);
  build_self_test_image();
  printf("\n[diag] Stage 8: virtual-ROM, USB CDC, and I/O trap\n");
  printf("[diag] normal bytes=terminal input; Ctrl-] then "
         "s=status r=restart p=port-test b=100-boots h=one-hour x=stop\n");
  printf(start_ram_checker() ? "[diag] PASS: RAM/USB checker at 1MHz\n"
                             : "[diag] FAIL: boot/trap initialization\n");

  while (true) {
    service_usb();
    service_self_test();
    service_hour_test();
    tight_loop_contents();
  }
}