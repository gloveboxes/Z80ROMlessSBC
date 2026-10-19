#include <inttypes.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"
#include "math_soak_program.h"
#include "math_soak_image.h"

enum {
  SOAK_HZ = Z80_CLOCK_MAX_HZ,
  SOAK_SYSTEM_HZ = 120000000,
#ifdef Z80_SOAK_14H
  SOAK_HOURS = 14,
#else
  SOAK_HOURS = 1,
#endif
  SOAK_SECONDS = SOAK_HOURS * 3600,
  SOAK_PROGRESS_MS = 5000,
  SOAK_PORT = 6,
};
_Static_assert(SOAK_HZ == 20000000, "Use the isolated 20 MHz chain");
_Static_assert(SOAK_VECTOR_BASE + SOAK_VECTOR_COUNT * SOAK_VECTOR_SIZE == 0x1A00,
               "Assembly table/stack boundary");
typedef enum { SOAK_IDLE, SOAK_LOAD, SOAK_VERIFY, SOAK_RUN } soak_state_t;
static soak_state_t soak_state;
static uint8_t soak_image[SOAK_IMAGE_SIZE];
static unsigned load_index;
static unsigned failures;
static uint32_t actual_hz;
static uint32_t completed_phases, accounted_phases, protocol_error, error_length;
static uint8_t error_bytes[8];
static uint32_t timeout_baseline, control_baseline;
static absolute_time_t run_started, progress_deadline, report_deadline;
static uint64_t elapsed_us;
static bool was_connected, discard_line;
static unsigned command_length;
static char command;

static void configure_system_clock(void) {
  set_sys_clock_pll(1200000000, 5, 2);
}

static void stop_cpu(void) {
  z80_io_trap_disable();
  z80_cpu_fail_closed();
}

static void print_stats(void) {
  uint64_t phases = __atomic_load_n(&completed_phases, __ATOMIC_ACQUIRE);
  uint64_t bytes = phases * SOAK_WORK_BYTES;
  uint64_t math = phases * SOAK_VECTOR_COUNT * SOAK_REPEATS *
                  SOAK_ASSERTIONS_PER_VECTOR;
  uint64_t stack = phases * SOAK_STACK_CHECKS;
  uint64_t loops = phases * SOAK_LOOP_CHECKS;
  printf("STATS: successes=%" PRIu64 " failures=%u sram_byte_checks=%" PRIu64
         " math_assertions=%" PRIu64 " stack_checks=%" PRIu64
         " loop_checks=%" PRIu64 " completed_cycles=%" PRIu64 "\n",
         bytes + math + stack + loops, failures, bytes, math, stack,
         loops, phases / 4);
}

static void finish(const char *reason, bool failed, bool passed) {
  stop_cpu();
  if (soak_state == SOAK_RUN)
    elapsed_us = (uint64_t)absolute_time_diff_us(run_started, get_absolute_time());
  soak_state = SOAK_IDLE;
  failures = failed ? 1 : 0;
  printf("%s: %s; actual=%uHz pwm_ms=%" PRIu64 "; RESET# LOW\n",
         failed ? "FAIL" : passed ? "PASS" : "STOP", reason, (unsigned)actual_hz,
         elapsed_us / 1000);
  print_stats();
}

static uint8_t result_read(uint8_t port, void *context) {
  (void)port;
  (void)context;
  __atomic_store_n(&protocol_error, 1, __ATOMIC_RELEASE);
  return 0xFF;
}

static void result_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  if (port != SOAK_PORT) {
    __atomic_store_n(&protocol_error, 1, __ATOMIC_RELEASE);
    return;
  }
  uint32_t length = __atomic_load_n(&error_length, __ATOMIC_RELAXED);
  if (length != 0) {
    if (length > sizeof(error_bytes)) {
      __atomic_store_n(&protocol_error, 1, __ATOMIC_RELEASE);
      return;
    }
    error_bytes[length - 1] = value;
    __atomic_store_n(&error_length, length + 1, __ATOMIC_RELEASE);
  } else if (value == 0xE1) {
    __atomic_store_n(&error_length, 1, __ATOMIC_RELEASE);
  } else {
    uint32_t count = __atomic_load_n(&completed_phases, __ATOMIC_RELAXED);
    if (count == UINT32_MAX || value != 0x40 + count % 4)
      __atomic_store_n(&protocol_error, 1, __ATOMIC_RELEASE);
    else
      __atomic_store_n(&completed_phases, count + 1, __ATOMIC_RELEASE);
  }
}

static void start_test(void) {
  stop_cpu();
  elapsed_us = load_index = accounted_phases = 0;
  actual_hz = failures = 0;
  __atomic_store_n(&completed_phases, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&error_length, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&protocol_error, 0, __ATOMIC_RELAXED);
  if (clock_get_hz(clk_sys) != SOAK_SYSTEM_HZ ||
      !z80_clock_set_hz(SOAK_HZ)) {
    finish("unexpected system/PWM clock", true, false);
    return;
  }
  actual_hz = z80_clock_get_hz();
  if (actual_hz != SOAK_HZ) {
    finish("unexpected system/PWM clock", true, false);
    return;
  }
  stop_cpu();
  soak_build_image(soak_image, z80_math_soak_code, sizeof(z80_math_soak_code));
  if (!z80_cpu_prepare_loader()) {
    finish("loader initialization", true, false);
    return;
  }
  soak_state = SOAK_LOAD;
  printf("LOAD: resident code/reference/stack image, %u bytes; Z80 held to assisted clocks\n",
         SOAK_IMAGE_SIZE);
}

static void service_test(void) {
  if (soak_state == SOAK_IDLE)
    return;
  if (soak_state == SOAK_LOAD || soak_state == SOAK_VERIFY) {
    uint8_t expected = soak_image[load_index];
    if (soak_state == SOAK_LOAD) {
      if (!z80_sram_write_byte((uint16_t)load_index, expected)) {
        finish("assisted image load", true, false);
        return;
      }
    } else {
      uint8_t value;
      if (!z80_sram_read_byte((uint16_t)load_index, &value)) {
        finish("assisted image read", true, false);
        return;
      }
      if (value != expected) {
        stop_cpu();
        printf("IMAGE MISMATCH: address=%04x actual=%02x expected=%02x\n",
               load_index, value, expected);
        finish("assisted image verification", true, false);
        return;
      }
    }
    if (++load_index != SOAK_IMAGE_SIZE)
      return;
    load_index = 0;
    if (soak_state == SOAK_LOAD) {
      soak_state = SOAK_VERIFY;
      return;
    }
    timeout_baseline = z80_io_trap_timeout_count();
    control_baseline = z80_io_trap_control_error_count();
    bool started = z80_io_trap_enable(result_read, result_write, NULL) &&
                   z80_cpu_release_reset_and_run(SOAK_HZ);
    actual_hz = z80_clock_get_hz();
    if (!started || actual_hz != SOAK_HZ) {
      finish("20 MHz execution start", true, false);
      return;
    }
    run_started = get_absolute_time();
    progress_deadline = make_timeout_time_ms(SOAK_PROGRESS_MS);
    report_deadline = make_timeout_time_ms(10000);
    soak_state = SOAK_RUN;
    printf("RUN: actual=%uHz minimum=%us; 56KiB SRAM, 128 math vectors x16/phase\n",
           SOAK_HZ, SOAK_SECONDS);
    return;
  }
  uint32_t count = __atomic_load_n(&completed_phases, __ATOMIC_ACQUIRE);
  uint32_t length = __atomic_load_n(&error_length, __ATOMIC_ACQUIRE);
  if (__atomic_load_n(&protocol_error, __ATOMIC_ACQUIRE) ||
      z80_io_trap_timeout_count() != timeout_baseline ||
      z80_io_trap_control_error_count() != control_baseline) {
    finish("I/O protocol/control fault", true, false);
    return;
  }
  if (length == sizeof(error_bytes) + 1) {
    stop_cpu();
    printf("MISMATCH: stage=%02x vector=%u address=%04x actual=%04x expected=%04x\n",
           error_bytes[0], error_bytes[1],
           error_bytes[2] | (unsigned)error_bytes[3] << 8,
           error_bytes[4] | (unsigned)error_bytes[5] << 8,
           error_bytes[6] | (unsigned)error_bytes[7] << 8);
    finish("Z80-detected arithmetic/stack/SRAM mismatch", true, false);
    return;
  }
  if (time_reached(progress_deadline)) {
    finish(length ? "incomplete failure report" : "CPU progress timeout",
           true, false);
    return;
  }
  if (count != accounted_phases) {
    accounted_phases = count;
    progress_deadline = make_timeout_time_ms(SOAK_PROGRESS_MS);
  }
  elapsed_us = (uint64_t)absolute_time_diff_us(run_started, get_absolute_time());
  if (elapsed_us >= (uint64_t)SOAK_SECONDS * 1000000 &&
      count >= 4 && count % 4 == 0 && length == 0) {
    finish(SOAK_HOURS == 1
               ? "one-hour soak complete (functional, not timing qualification)"
               : "14-hour soak complete (functional, not timing qualification)",
           false, true);
    return;
  }
  if (time_reached(report_deadline)) {
    printf("PROGRESS: actual=%uHz pwm_ms=%" PRIu64
           " phases=%lu completed_math_vectors=%" PRIu64 "\n",
           SOAK_HZ, elapsed_us / 1000, (unsigned long)count,
           (uint64_t)count * SOAK_VECTOR_COUNT * SOAK_REPEATS);
    print_stats();
    report_deadline = make_timeout_time_ms(10000);
  }
}

static void print_help(void) {
  printf("20MHz math/SRAM soak ready; f + Enter starts %s; x aborts immediately.\n",
         SOAK_HOURS == 1 ? "one hour" : "14 hours");
  printf("Pico clk_sys=%luHz; nominal PWM=20000000Hz, not measured.\n",
         (unsigned long)clock_get_hz(clk_sys));
  printf("DTR required; destructive 2000-FFFF work area; all ICs must be powered.\n");
  printf("h=help, s=status (Enter required); no automatic start/restart.\n");
  printf("Totals count completed phases only; abort/disconnect is not a PASS.\n");
}

static void poll_console(void) {
  if (!stdio_usb_connected()) {
    if (soak_state != SOAK_IDLE)
      finish("console disconnected", false, false);
    was_connected = false;
    command_length = 0;
    discard_line = false;
    return;
  }
  if (!was_connected) {
    was_connected = true;
    print_help();
  }
  int value = getchar_timeout_us(0);
  if (value < 0)
    return;
  if (value == 'x') {
    if (soak_state != SOAK_IDLE)
      finish("operator abort", false, false);
    command_length = 0;
    discard_line = true;
  } else if (value == '\r' || value == '\n') {
    if (!discard_line && command_length != 0) {
      if (command_length != 1)
        printf("ERROR: use one command then Enter.\n");
      else if (command == 'h')
        print_help();
      else if (command == 's') {
        printf("STATE: %s\n", soak_state == SOAK_IDLE ? "STOPPED" : "ACTIVE");
        print_stats();
      } else if (command == 'f' && soak_state == SOAK_IDLE)
        start_test();
      else
        printf("ERROR: unknown command or soak already active; x stops.\n");
    }
    command_length = 0;
    discard_line = false;
  } else if (!discard_line) {
    if (command_length == 0)
      command = (char)value;
    if (command_length < 2)
      ++command_length;
  }
}

int main(void) {
  z80_safe_startup();
  configure_system_clock();
  stdio_init_all();
  while (true) {
    poll_console();
    service_test();
    tight_loop_contents();
  }
}
