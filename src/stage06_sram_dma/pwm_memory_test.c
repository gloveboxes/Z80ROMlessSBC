#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

enum {
  RESULT_PORT = 0x06,
#if defined(Z80_CLOCK_20MHZ)
  MIN_HZ = 20000000,
  SYSTEM_HZ = 120000000,
#else
  MIN_HZ = 1000000,
  SYSTEM_HZ = 144000000,
#endif
  MAX_HZ = Z80_CLOCK_MAX_HZ,
  STEP_HZ = 500000,
  SCREEN_MS = 60000,
  PROGRESS_TIMEOUT_MS = 5000,
  IMAGE_CAPACITY = 256,
  BANK_SIZE = 32768,
};

typedef enum { IDLE, LOAD_IMAGE, VERIFY_IMAGE, RUNNING } test_state_t;

static test_state_t state;
static uint8_t image[IMAGE_CAPACITY];
static size_t image_length;
static bool image_valid;
static unsigned bank;
static uint16_t code_base;
static uint16_t data_base;
static uint32_t load_index;
static uint32_t requested_hz;
static uint32_t actual_hz;
static uint32_t last_passed_hz;
static unsigned passed_speeds;
static unsigned cycles;
static uint64_t checks;
static uint64_t total_checks;
static uint64_t running_us;
static absolute_time_t bank_started;
static absolute_time_t progress_deadline;
static absolute_time_t report_deadline;
static uint32_t trap_timeout_baseline;
static uint32_t trap_error_baseline;

/* Only the IRQ writes these; main takes a snapshot before acting on them. */
static uint32_t completed_patterns;
static uint32_t accounted_patterns;
static uint32_t protocol_error;
static uint32_t error_length;
static uint8_t error_bytes[4];
static bool was_connected;
static char command;
static unsigned command_length;
static bool discard_line;

static void configure_system_clock(void) {
#if defined(Z80_CLOCK_20MHZ)
  // RESET remains asserted; USB and Z80 execution have not started.
  set_sys_clock_pll(1200000000, 5, 2);
#endif
}

static void emit(uint8_t byte) {
  if (image_length < sizeof(image))
    image[image_length++] = byte;
  else
    image_valid = false;
}

static void emit_word(uint16_t word) {
  emit((uint8_t)word);
  emit((uint8_t)(word >> 8));
}

static void repeat_until_bank_end(size_t loop) {
  emit(0x23);                         // INC HL
  emit(0x7C); emit(0xE6); emit(0x7F); // LD A,H; AND 7F
  emit(0xB5);                         // OR L
  emit(0x20);                         // JR NZ,loop
  int displacement = (int)loop - (int)(image_length + 1);
  if (displacement < -128 || displacement > 127)
    image_valid = false;
  emit((uint8_t)displacement);
}

static bool build_image(void) {
  image_length = 0;
  image_valid = true;
  code_base = bank == 0 ? 0 : 0x8000;
  data_base = bank == 0 ? 0x8000 : 0;
  size_t failure_patches[4];
  emit(0xF3); // DI; no stack or interrupt service is needed.
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    emit(0x21); emit_word(data_base); // LD HL,bank
    size_t fill_loop = image_length;
    emit(pattern & 1u ? 0x7C : 0x7D); // LD A,H / LD A,L
    if (pattern & 2u) {
      emit(0xEE); emit(0xFF); // XOR FF
    }
    emit(0x77); // LD (HL),A
    repeat_until_bank_end(fill_loop);

    emit(0x21); emit_word(data_base);
    size_t read_loop = image_length;
    emit(pattern & 1u ? 0x7C : 0x7D);
    if (pattern & 2u) {
      emit(0xEE); emit(0xFF);
    }
    emit(0x57); emit(0x7E); emit(0xBA); // LD D,A; LD A,(HL); CP D
    emit(0xC2); // JP NZ,failure, preserving actual A, expected D and HL
    failure_patches[pattern] = image_length;
    emit_word(0);
    repeat_until_bank_end(read_loop);
    emit(0x3E); emit((uint8_t)(0x40 + bank * 4 + pattern));
    emit(0xD3); emit(RESULT_PORT);
  }
  emit(0xC3); emit_word(code_base);
  uint16_t failure_address = (uint16_t)(code_base + image_length);
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    image[failure_patches[pattern]] = (uint8_t)failure_address;
    image[failure_patches[pattern] + 1] = (uint8_t)(failure_address >> 8);
  }
  emit(0x5F); // LD E,A saves the failed read before reporting.
  emit(0x3E); emit(0xE1); emit(0xD3); emit(RESULT_PORT);
  const uint8_t registers[] = {0x7D, 0x7C, 0x7B, 0x7A};
  for (size_t i = 0; i < sizeof(registers); ++i) {
    emit(registers[i]); // Address LOW/HIGH, actual E, expected D.
    emit(0xD3); emit(RESULT_PORT);
  }
  emit(0x76); // HALT after the complete failure packet.
  return image_valid;
}

static uint8_t result_read(uint8_t port, void *context) {
  (void)port;
  (void)context;
  __atomic_store_n(&protocol_error, 1, __ATOMIC_RELEASE);
  return 0xFF;
}

static void result_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  if (port != RESULT_PORT) {
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
    uint32_t count = __atomic_load_n(&completed_patterns, __ATOMIC_RELAXED);
    if (count >= 4 || value != 0x40 + bank * 4 + count)
      __atomic_store_n(&protocol_error, 1, __ATOMIC_RELEASE);
    else
      __atomic_store_n(&completed_patterns, count + 1, __ATOMIC_RELEASE);
  }
}

static void stop_cpu(void) {
  z80_io_trap_disable();
  z80_cpu_fail_closed();
}

static void finish(const char *reason, bool failed) {
  stop_cpu();
  state = IDLE;
  printf("%s: %s; RESET# LOW\n", failed ? "FAIL" : "STOP", reason);
  printf("STATS: completed_check_successes=%" PRIu64
         " failures=%u passed_speeds=%u last_passed_actual=%luHz\n",
         total_checks + checks, failed ? 1u : 0u, passed_speeds,
         (unsigned long)last_passed_hz);
}

static uint32_t transfer_length(void) {
  return (uint32_t)image_length + (bank == 1 ? 3u : 0u);
}

static uint16_t transfer_address(void) {
  return load_index < image_length
             ? (uint16_t)(code_base + load_index)
             : (uint16_t)(load_index - image_length);
}

static uint8_t transfer_value(void) {
  const uint8_t jump[] = {0xC3, 0x00, 0x80};
  return load_index < image_length ? image[load_index]
                                   : jump[load_index - image_length];
}

static void prepare_bank(void) {
  stop_cpu();
  if (!build_image() || !z80_cpu_prepare_loader()) {
    finish("image/loader initialization", true);
    return;
  }
  load_index = 0;
  state = LOAD_IMAGE;
}

static void begin_rate(void) {
  while (requested_hz <= MAX_HZ) {
    stop_cpu();
    // Select the nominal rate with RESET asserted, before loading/executing.
    if (!z80_clock_set_hz(requested_hz)) {
      finish("PWM configuration", true);
      return;
    }
    actual_hz = z80_clock_get_hz();
    stop_cpu();
    if (actual_hz != last_passed_hz)
      break;
    printf("SKIP: requested=%luHz actual=%luHz already passed\n",
           (unsigned long)requested_hz, (unsigned long)actual_hz);
    requested_hz += STEP_HZ;
  }
  if (requested_hz > MAX_HZ) {
    finish("PWM screening complete (functional, not timing qualification)", false);
    return;
  }
  checks = running_us = 0;
  cycles = bank = 0;
  printf("RUN: requested=%luHz actual=%luHz minimum=60s%s\n",
         (unsigned long)requested_hz, (unsigned long)actual_hz,
         actual_hz > 6000000 ? " EXPERIMENTAL" : "");
  report_deadline = make_timeout_time_ms(10000);
  prepare_bank();
}

static void start_test(void) {
  requested_hz = MIN_HZ;
  actual_hz = last_passed_hz = 0;
  passed_speeds = cycles = 0;
  total_checks = checks = running_us = 0;
  if (clock_get_hz(clk_sys) != SYSTEM_HZ) {
    finish("unexpected Pico system clock", true);
    return;
  }
  begin_rate();
}

static void service_test(void) {
  if (state == IDLE)
    return;
  if (state == LOAD_IMAGE || state == VERIFY_IMAGE) {
    uint8_t expected = transfer_value();
    if (state == LOAD_IMAGE) {
      if (!z80_sram_write_byte(transfer_address(), expected)) {
        finish("assisted program load", true);
        return;
      }
    } else {
      uint8_t actual;
      if (!z80_sram_read_byte(transfer_address(), &actual) ||
          actual != expected) {
        finish("assisted program verification", true);
        return;
      }
    }
    if (++load_index < transfer_length())
      return;
    load_index = 0;
    if (state == LOAD_IMAGE) {
      state = VERIFY_IMAGE;
      return;
    }
    __atomic_store_n(&completed_patterns, 0, __ATOMIC_RELAXED);
    accounted_patterns = 0;
    __atomic_store_n(&protocol_error, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&error_length, 0, __ATOMIC_RELAXED);
    trap_timeout_baseline = z80_io_trap_timeout_count();
    trap_error_baseline = z80_io_trap_control_error_count();
    progress_deadline = make_timeout_time_ms(PROGRESS_TIMEOUT_MS);
    bank_started = get_absolute_time();
    if (!z80_io_trap_enable(result_read, result_write, NULL) ||
        !z80_cpu_release_reset_and_run(requested_hz) ||
        z80_clock_get_hz() != actual_hz) {
      finish("PWM execution start", true);
      return;
    }
    state = RUNNING;
    return;
  }
  uint32_t completed = __atomic_load_n(&completed_patterns, __ATOMIC_ACQUIRE);
  uint32_t error = __atomic_load_n(&error_length, __ATOMIC_ACQUIRE);
  if (__atomic_load_n(&protocol_error, __ATOMIC_ACQUIRE) ||
      z80_io_trap_timeout_count() != trap_timeout_baseline ||
      z80_io_trap_control_error_count() != trap_error_baseline) {
    finish("I/O protocol/control fault", true);
    return;
  }
  if (time_reached(progress_deadline)) {
    finish(error ? "incomplete RAM failure report" : "CPU progress timeout", true);
    return;
  }
  if (completed > accounted_patterns) {
    checks += (uint64_t)(completed - accounted_patterns) * BANK_SIZE;
    accounted_patterns = completed;
    progress_deadline = make_timeout_time_ms(PROGRESS_TIMEOUT_MS);
  }
  if (error == 5) {
    stop_cpu();
    uint16_t address = (uint16_t)(error_bytes[0] | (uint16_t)error_bytes[1] << 8);
    printf("address %04x, value %02x, expected %02x - FAIL (PWM %luHz)\n",
           address, error_bytes[2], error_bytes[3], (unsigned long)actual_hz);
    finish("CPU-detected RAM mismatch", true);
    return;
  }
  if (time_reached(report_deadline)) {
    printf("PROGRESS: actual=%luHz completed_check_successes=%" PRIu64
           " completed_64KiB_cycles=%u\n",
           (unsigned long)actual_hz, checks, cycles);
    report_deadline = make_timeout_time_ms(10000);
  }
  if (completed != 4 || error != 0)
    return;
  stop_cpu();
  running_us += (uint64_t)absolute_time_diff_us(bank_started, get_absolute_time());
  if (bank == 0) {
    bank = 1;
    prepare_bank();
  } else {
    ++cycles;
    if (running_us >= (uint64_t)SCREEN_MS * 1000u) {
      printf("PASS: requested=%luHz actual=%luHz pwm_ms=%" PRIu64
             " cycles=%u successes=%" PRIu64 " failures=0\n",
             (unsigned long)requested_hz, (unsigned long)actual_hz,
             running_us / 1000u, cycles, checks);
      total_checks += checks;
      checks = 0;
      last_passed_hz = actual_hz;
      ++passed_speeds;
      requested_hz += STEP_HZ;
      begin_rate();
    } else {
      bank = 0;
      prepare_bank();
    }
  }
}

static void print_help(void) {
  if (MIN_HZ == MAX_HZ)
    printf("PWM SRAM screening: f + Enter starts %uMHz; x aborts immediately.\n",
           MIN_HZ / 1000000);
  else
    printf("PWM SRAM screening: f + Enter starts %u-%uMHz; x aborts immediately.\n",
           MIN_HZ / 1000000, MAX_HZ / 1000000);
  printf("Pico clk_sys=%luHz; reported PWM rates are nominal, not measured.\n",
         (unsigned long)clock_get_hz(clk_sys));
  printf("60s minimum PWM time per distinct speed; four patterns over all 64KiB.\n");
  printf("DTR required; connecting does not start. Destructive; all ICs powered.\n");
  printf("h=help, s=status (Enter required); a reboot invalidates an active run.\n");
}

static void poll_console(void) {
  if (!stdio_usb_connected()) {
    if (state != IDLE)
      finish("console disconnected", false);
    was_connected = false;
    command_length = 0;
    discard_line = false;
    return;
  }
  if (!was_connected) {
    was_connected = true;
    printf("PWM SRAM screening ready; RESET# LOW; waiting for f + Enter.\n");
    print_help();
  }
  int value = getchar_timeout_us(0);
  if (value < 0)
    return;
  if (value == 'x') {
    if (state != IDLE)
      finish("operator abort", false);
    command_length = 0;
    discard_line = true;
  } else if (value == '\r' || value == '\n') {
    if (!discard_line && command_length != 0) {
      if (command_length != 1)
        printf("ERROR: use one command then Enter.\n");
      else if (command == 'h')
        print_help();
      else if (command == 's')
        printf("STATE: %s requested=%luHz actual=%luHz successes=%" PRIu64 "\n",
               state == IDLE ? "STOPPED" : "ACTIVE",
               (unsigned long)requested_hz, (unsigned long)actual_hz,
               total_checks + checks);
      else if (command == 'f' && state == IDLE)
        start_test();
      else
        printf("ERROR: unknown command or test already active; x stops.\n");
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
