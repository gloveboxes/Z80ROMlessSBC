#include <stdio.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"
#include "z80sbc/terminal.h"

enum {
  QUALIFICATION_DATA_PORT = 0xFE,
  QUALIFICATION_RESULT_PORT = 0xFD,
  QUALIFICATION_NONE = 0,
  QUALIFICATION_ADDRESS = 1,
  QUALIFICATION_RAM = 2,
  QUALIFICATION_MIN_HZ = 1000000,
  QUALIFICATION_MAX_HZ = 8000000,
  QUALIFICATION_STEP_HZ = 500000,
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

static const uint16_t QUALIFICATION_ADDRESSES[] = {
  0x0000, 0xFFFF, 0x5555, 0xAAAA,
  0x0001, 0x0002, 0x0004, 0x0008,
  0x0010, 0x0020, 0x0040, 0x0080,
  0x0100, 0x0200, 0x0400, 0x0800,
  0x1000, 0x2000, 0x4000, 0x8000,
  0xFFFE, 0xFFFD, 0xFFFB, 0xFFF7,
  0xFFEF, 0xFFDF, 0xFFBF, 0xFF7F,
  0xFEFF, 0xFDFF, 0xFBFF, 0xF7FF,
  0xEFFF, 0xDFFF, 0xBFFF, 0x7FFF,
};

static uint8_t address_test_image[256];
static size_t address_test_image_length;
static uint8_t address_expected[
    sizeof(QUALIFICATION_ADDRESSES) / sizeof(QUALIFICATION_ADDRESSES[0])];
static uint32_t qualification_mode;
static uint32_t qualification_index;
static uint32_t qualification_errors;
static uint32_t qualification_complete;
static uint32_t qualification_clock_hz = QUALIFICATION_MIN_HZ;
static absolute_time_t qualification_deadline;
static bool hour_test_active;
static absolute_time_t hour_test_deadline;
static uint32_t hour_trap_timeout_baseline;
static uint32_t hour_control_error_baseline;
static uint32_t flash_pause_request;
static uint32_t flash_paused;

static void emit_address_test_byte(uint8_t value) {
  address_test_image[address_test_image_length++] = value;
}

static void build_address_test_image(void) {
  address_test_image_length = 0;
  for (size_t index = 0;
       index < sizeof(QUALIFICATION_ADDRESSES) /
                   sizeof(QUALIFICATION_ADDRESSES[0]);
       ++index) {
    uint16_t address = QUALIFICATION_ADDRESSES[index];
    emit_address_test_byte(0x3A);
    emit_address_test_byte((uint8_t)address);
    emit_address_test_byte((uint8_t)(address >> 8));
    emit_address_test_byte(0xD3);
    emit_address_test_byte(QUALIFICATION_DATA_PORT);
  }
  emit_address_test_byte(0x3E);
  emit_address_test_byte(0x5A);
  emit_address_test_byte(0xD3);
  emit_address_test_byte(QUALIFICATION_RESULT_PORT);
  emit_address_test_byte(0x76);
}

static uint8_t virtual_read(uint8_t port, void *context) {
  (void)context;
  if (port >= 0x10 && port <= 0x14)
    return z80_flash_disk_io_read(port);
  return z80_terminal_io_read(port);
}

static void virtual_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  uint32_t mode = __atomic_load_n(&qualification_mode, __ATOMIC_ACQUIRE);
  if (port == QUALIFICATION_DATA_PORT) {
    if (mode == QUALIFICATION_ADDRESS) {
      uint32_t index = __atomic_load_n(&qualification_index,
                                       __ATOMIC_RELAXED);
      if (index >= sizeof(address_expected) ||
          value != address_expected[index])
        __atomic_fetch_add(&qualification_errors, 1, __ATOMIC_RELAXED);
      else
        __atomic_store_n(&qualification_index, index + 1,
                         __ATOMIC_RELAXED);
    } else if (mode == QUALIFICATION_RAM && value == 0xE1) {
      __atomic_fetch_add(&qualification_errors, 1, __ATOMIC_RELAXED);
    }
    return;
  }
  if (port == QUALIFICATION_RESULT_PORT && mode == QUALIFICATION_ADDRESS) {
    if (value != 0x5A ||
        __atomic_load_n(&qualification_index, __ATOMIC_RELAXED) !=
            sizeof(address_expected))
      __atomic_fetch_add(&qualification_errors, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&qualification_complete, 1, __ATOMIC_RELEASE);
    return;
  }
  if (port >= 0x10 && port <= 0x14)
    z80_flash_disk_io_write(port, value);
  else
    z80_terminal_io_write(port, value);
}

static void core1_main(void) {
  while (true) {
    if (__atomic_load_n(&flash_pause_request, __ATOMIC_ACQUIRE)) {
      if (!__atomic_load_n(&flash_paused, __ATOMIC_ACQUIRE)) {
        z80_flash_core1_service();
        if (z80_flash_disk_quiescent())
          __atomic_store_n(&flash_paused, 1, __ATOMIC_RELEASE);
      }
    } else {
      __atomic_store_n(&flash_paused, 0, __ATOMIC_RELEASE);
      z80_flash_core1_service();
    }
    z80_terminal_core1_service();
    tight_loop_contents();
  }
}

static bool pause_flash_service(void) {
  __atomic_store_n(&flash_pause_request, 1, __ATOMIC_RELEASE);
  absolute_time_t deadline = make_timeout_time_ms(1000);
  while (!__atomic_load_n(&flash_paused, __ATOMIC_ACQUIRE)) {
    z80_flash_core0_service();
    if (time_reached(deadline)) {
      __atomic_store_n(&flash_pause_request, 0, __ATOMIC_RELEASE);
      return false;
    }
    tight_loop_contents();
  }
  return true;
}

static void resume_flash_service(void) {
  __atomic_store_n(&flash_pause_request, 0, __ATOMIC_RELEASE);
}

static bool set_qualification_clock(uint32_t clock_hz) {
  if (clock_hz < QUALIFICATION_MIN_HZ || clock_hz > QUALIFICATION_MAX_HZ ||
      !pause_flash_service())
    return false;
  bool acquired = z80_cpu_request_bus(500000);
  if (!acquired) {
    resume_flash_service();
    return false;
  }
  z80_io_trap_disable();
  bool configured = z80_clock_set_hz(clock_hz);
  z80_io_trap_rearm();
  bool released = z80_cpu_release_bus(500000);
  resume_flash_service();
  if (!configured || !released) {
    z80_io_trap_disable();
    z80_cpu_fail_closed();
    return false;
  }
  __atomic_store_n(&qualification_clock_hz, clock_hz, __ATOMIC_RELEASE);
  return true;
}

static bool start_address_test(void) {
  if (!pause_flash_service())
    return false;
  z80_io_trap_disable();
  bool loaded = z80_cpu_load_and_verify(address_test_image,
                                        address_test_image_length);
  for (size_t index = 0; loaded && index < sizeof(address_expected); ++index)
    loaded = z80_sram_read_byte(QUALIFICATION_ADDRESSES[index],
                                &address_expected[index]);
  __atomic_store_n(&qualification_index, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&qualification_errors, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&qualification_complete, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&qualification_mode, QUALIFICATION_ADDRESS,
                   __ATOMIC_RELEASE);
  qualification_deadline = make_timeout_time_ms(2000);
  bool started = loaded &&
                 z80_io_trap_enable(virtual_read, virtual_write, NULL) &&
                 z80_cpu_release_reset_and_run(
                     __atomic_load_n(&qualification_clock_hz,
                                     __ATOMIC_ACQUIRE));
  resume_flash_service();
  return started;
}

static bool start_ram_test(bool one_hour) {
  if (!pause_flash_service())
    return false;
  z80_io_trap_disable();
  __atomic_store_n(&qualification_errors, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&qualification_mode, QUALIFICATION_RAM, __ATOMIC_RELEASE);
  bool started = z80_cpu_load_and_verify(RAM_CHECK_PROGRAM,
                                         sizeof(RAM_CHECK_PROGRAM)) &&
                 z80_io_trap_enable(virtual_read, virtual_write, NULL) &&
                 z80_cpu_release_reset_and_run(
                     __atomic_load_n(&qualification_clock_hz,
                                     __ATOMIC_ACQUIRE));
  resume_flash_service();
  hour_test_active = started && one_hour;
  if (hour_test_active) {
    hour_test_deadline = make_timeout_time_ms(60u * 60u * 1000u);
    hour_trap_timeout_baseline = z80_io_trap_timeout_count();
    hour_control_error_baseline = z80_io_trap_control_error_count();
  }
  return started;
}

static void service_qualification(void) {
  uint32_t mode = __atomic_load_n(&qualification_mode, __ATOMIC_ACQUIRE);
  if (mode == QUALIFICATION_ADDRESS &&
      (__atomic_load_n(&qualification_complete, __ATOMIC_ACQUIRE) ||
       time_reached(qualification_deadline))) {
    bool passed = __atomic_load_n(&qualification_complete,
                                  __ATOMIC_ACQUIRE) &&
                  __atomic_load_n(&qualification_errors,
                                  __ATOMIC_RELAXED) == 0;
    printf(passed ? "PASS: CPU address/readback pattern\n"
                  : "FAIL: CPU address/readback pattern\n");
    __atomic_store_n(&qualification_mode, QUALIFICATION_NONE,
                     __ATOMIC_RELEASE);
  }
  if (hour_test_active && time_reached(hour_test_deadline)) {
    hour_test_active = false;
    bool passed = __atomic_load_n(&qualification_errors,
                                  __ATOMIC_RELAXED) == 0 &&
                  z80_io_trap_timeout_count() ==
                      hour_trap_timeout_baseline &&
                  z80_io_trap_control_error_count() ==
                      hour_control_error_baseline;
    printf(passed ? "PASS: one-hour RAM/terminal test\n"
                  : "FAIL: one-hour RAM/terminal test\n");
  }
}

static _Noreturn void fail_closed(const char *reason) {
  z80_io_trap_disable();
  z80_cpu_fail_closed();
  printf("FAIL: %s\n", reason);
  while (true)
    tight_loop_contents();
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 10: CP/M flash disks and WebSocket terminal\n");

  if (!z80_flash_storage_init())
    fail_closed("boot package or journal recovery");
  if (!z80_terminal_init())
    fail_closed("terminal timers");
  build_address_test_image();
  multicore_launch_core1(core1_main);
  if (!z80_io_trap_enable(virtual_read, virtual_write, NULL))
    fail_closed("I/O trap initialization");
  if (!z80_cpu_release_reset_and_run(1000000))
    fail_closed("CPU start");
  printf("PASS: CP/M started; WebSocket port 8088\n");
  printf("+=500kHz, -=500kHz, a=CPU address/readback, "
         "t=RAM/terminal, h=one-hour RAM/terminal, s=status\n");

  while (true) {
    z80_flash_core0_service();
    service_qualification();
    int command = getchar_timeout_us(0);
    if (command == '+') {
      uint32_t current = __atomic_load_n(&qualification_clock_hz,
                                         __ATOMIC_ACQUIRE);
      uint32_t requested = current + QUALIFICATION_STEP_HZ;
      printf(set_qualification_clock(requested) ? "clock=%luHz\n"
                                                : "FAIL: clock change\n",
             (unsigned long)requested);
    } else if (command == '-') {
      uint32_t current = __atomic_load_n(&qualification_clock_hz,
                                         __ATOMIC_ACQUIRE);
      uint32_t requested = current > QUALIFICATION_MIN_HZ
                               ? current - QUALIFICATION_STEP_HZ
                               : 0;
      printf(set_qualification_clock(requested) ? "clock=%luHz\n"
                                                : "FAIL: clock change\n",
             (unsigned long)requested);
    } else if (command == 'a') {
      hour_test_active = false;
      printf(start_address_test() ? "address/readback test started\n"
                                  : "FAIL: address/readback start\n");
    } else if (command == 't') {
      printf(start_ram_test(false) ? "RAM/terminal test started\n"
                                   : "FAIL: RAM/terminal start\n");
    } else if (command == 'h') {
      printf(start_ram_test(true) ? "one-hour RAM/terminal test started\n"
                                  : "FAIL: one-hour test start\n");
    } else if (command == 's')
      printf("clock=%lu client=%u rx_drop=%lu tx_drop=%lu disk=%02lx "
             "fatal=%u qual_errors=%lu\n",
             (unsigned long)__atomic_load_n(&qualification_clock_hz,
                                             __ATOMIC_ACQUIRE),
             z80_terminal_client_connected(),
             (unsigned long)z80_terminal_rx_drop_count(),
             (unsigned long)z80_terminal_tx_drop_count(),
             (unsigned long)z80_flash_disk_status(),
             z80_flash_disk_has_fatal_error(),
             (unsigned long)__atomic_load_n(&qualification_errors,
                                             __ATOMIC_RELAXED));
    tight_loop_contents();
  }
}