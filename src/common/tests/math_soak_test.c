#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void stdio_init_all(void);
int getchar_timeout_us(unsigned timeout);
static int capture_printf(const char *format, ...);
#define printf capture_printf
#define main math_soak_firmware_main
#include "../../stage06_sram_dma/math_soak_test.c"
#undef main
#undef printf

static uint8_t memory[65536];
static uint64_t now_us;
static bool connected, reset_held, trap_enabled;
static bool prepare_ok, write_ok, read_ok, run_ok, clock_ok, trap_ok;
static bool corrupt_load;
static unsigned writes, reads, starts, pll_calls;
static uint32_t system_hz, selected_hz, forced_hz, fake_timeouts, fake_errors;
static const char *input;
static char output[262144 * SOAK_HOURS];
static size_t output_length;

static int capture_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(output + output_length, sizeof(output) - output_length,
                         format, args);
  va_end(args);
  assert(result > 0 && (size_t)result < sizeof(output) - output_length);
  output_length += (size_t)result;
  return result;
}
void z80_safe_startup(void) { reset_held = true; }
void stdio_init_all(void) {}
int getchar_timeout_us(unsigned timeout) {
  assert(timeout == 0);
  return input && *input ? (unsigned char)*input++ : -1;
}
bool stdio_usb_connected(void) { return connected; }
void tight_loop_contents(void) { ++now_us; }
absolute_time_t get_absolute_time(void) { return now_us; }
absolute_time_t make_timeout_time_ms(uint32_t ms) {
  return now_us + (uint64_t)ms * 1000;
}
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
  assert(to >= from);
  return (int64_t)(to - from);
}
uint32_t clock_get_hz(uint clock) {
  assert(clock == clk_sys);
  return system_hz;
}
void set_sys_clock_pll(uint32_t vco, uint div1, uint div2) {
  assert(reset_held && !trap_enabled && !writes && !starts);
  assert(vco == 1200000000 && div1 == 5 && div2 == 2);
  system_hz = vco / div1 / div2;
  ++pll_calls;
}
bool z80_clock_set_hz(uint32_t hz) {
  assert(reset_held && !trap_enabled && hz == SOAK_HZ);
  selected_hz = forced_hz ? forced_hz : hz;
  return clock_ok;
}
uint32_t z80_clock_get_hz(void) { return selected_hz; }
void z80_cpu_fail_closed(void) { reset_held = true; }
bool z80_cpu_prepare_loader(void) {
  assert(reset_held && !trap_enabled);
  return prepare_ok;
}
bool z80_sram_write_byte(uint16_t address, uint8_t value) {
  assert(soak_state == SOAK_LOAD && !trap_enabled && address == writes);
  memory[address] = value;
  ++writes;
  return write_ok;
}
bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  assert(soak_state == SOAK_VERIFY && !trap_enabled && address == reads);
  *value = memory[address] ^ (corrupt_load ? 1 : 0);
  ++reads;
  return read_ok;
}
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  assert(reset_held && trap_enabled && hz == SOAK_HZ);
  assert(writes == SOAK_IMAGE_SIZE && reads == SOAK_IMAGE_SIZE);
  ++starts;
  reset_held = false;
  return run_ok;
}
bool z80_io_trap_enable(z80_io_read_handler_t reader,
                        z80_io_write_handler_t writer, void *context) {
  assert(reader == result_read && writer == result_write && context == NULL);
  trap_enabled = true;
  return trap_ok;
}
void z80_io_trap_disable(void) { trap_enabled = false; }
uint32_t z80_io_trap_timeout_count(void) { return fake_timeouts; }
uint32_t z80_io_trap_control_error_count(void) { return fake_errors; }

static void fixture(void) {
  soak_state = SOAK_IDLE;
  now_us = elapsed_us = 0;
  reset_held = connected = true;
  trap_enabled = was_connected = discard_line = corrupt_load = false;
  prepare_ok = write_ok = read_ok = run_ok = clock_ok = trap_ok = true;
  writes = reads = starts = pll_calls = command_length = 0;
  selected_hz = forced_hz = fake_timeouts = fake_errors = failures = actual_hz = 0;
  completed_phases = accounted_phases = protocol_error = error_length = 0;
  input = NULL;
  output_length = 0;
  output[0] = '\0';
  memset(memory, 0xEE, sizeof(memory));
  configure_system_clock();
  assert(pll_calls == 1 && system_hz == SOAK_SYSTEM_HZ);
}

static void send(const char *text) {
  input = text;
  while (*input) {
    poll_console();
    service_test();
  }
}

static void load_to_running(void) {
  for (unsigned i = 0; soak_state != SOAK_RUN; ++i) {
    assert(soak_state != SOAK_IDLE && i < SOAK_IMAGE_SIZE * 2);
    service_test();
  }
  assert(selected_hz == SOAK_HZ && !reset_held && trap_enabled);
  assert(memcmp(memory, soak_image, SOAK_IMAGE_SIZE) == 0);
}

static void failed(const char *reason) {
  assert(soak_state == SOAK_IDLE && reset_held && !trap_enabled);
  assert(failures == 1 && strstr(output, "FAIL:") && strstr(output, reason));
  assert(strstr(output, "PASS:") == NULL);
  send("s\n");
  assert(strstr(output, "successes=0 failures=1") || completed_phases != 0);
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "--emit-image") == 0) {
    uint8_t image[SOAK_IMAGE_SIZE];
    soak_build_image(image, z80_math_soak_code, sizeof(z80_math_soak_code));
    FILE *file = fopen(argv[2], "wb");
    if (!file) {
      perror("Opening resident test image");
      return 1;
    }
    bool written = fwrite(image, 1, sizeof(image), file) == sizeof(image);
    int closed = fclose(file);
    if (!written || closed != 0) {
      fprintf(stderr, "Failed to write complete resident test image\n");
      return 1;
    }
    return 0;
  }
  assert(argc == 1);
  assert(soak_flags(0xFFFF, 1, 0, false) == 0x51);
  assert(soak_flags(0x7FFF, 0, 1, false) == 0x94);
  assert(soak_flags(0x8000, 1, 0, true) == 0x16);
  assert(soak_flags(0, 0, 1, true) == 0x93);
  fixture();
  poll_console();
  assert(soak_state == SOAK_IDLE && !writes && !starts);
  send("h\ns\nff\n");
  assert(soak_state == SOAK_IDLE && strstr(output, "ERROR:"));
  connected = false;
  input = "f\n";
  poll_console();
  input = NULL;
  assert(!writes);
  connected = true;
  send("f\n");
  assert(soak_state == SOAK_LOAD);
  send("x");
  assert(soak_state == SOAK_IDLE && !failures && reset_held);
  send("f\n");
  assert(soak_state == SOAK_IDLE); // Abort discards the rest of its line.

  fixture();
  send("f\n");
  load_to_running();
  send("x");
  assert(soak_state == SOAK_IDLE && !failures && !trap_enabled && reset_held);
  assert(strstr(output, "operator abort") && !strstr(output, "PASS:"));
  fixture();
  send("f\n");
  load_to_running();
  connected = false;
  poll_console();
  assert(soak_state == SOAK_IDLE && !failures && reset_held && !trap_enabled);
  connected = true;
  poll_console();
  assert(starts == 1 && soak_state == SOAK_IDLE);

  for (unsigned fault = 0; fault < 9; ++fault) {
    fixture();
    if (fault == 0) system_hz--;
    if (fault == 1) clock_ok = false;
    if (fault == 2) forced_hz = 19000000;
    if (fault == 3) prepare_ok = false;
    if (fault == 4) write_ok = false;
    if (fault == 5) read_ok = false;
    if (fault == 6) corrupt_load = true;
    if (fault == 7) trap_ok = false;
    if (fault == 8) run_ok = false;
    send("f\n");
    for (unsigned i = 0; soak_state != SOAK_IDLE; ++i) {
      assert(i < SOAK_IMAGE_SIZE * 2);
      service_test();
    }
    failed("FAIL:");
  }
  for (unsigned fault = 0; fault < 7; ++fault) {
    fixture();
    send("f\n");
    load_to_running();
    if (fault == 0) result_write(6, 0x41, NULL);
    if (fault == 1) result_write(5, 0x40, NULL);
    if (fault == 2) result_read(6, NULL);
    if (fault == 3) ++fake_timeouts;
    if (fault == 4) ++fake_errors;
    if (fault == 5) now_us += SOAK_PROGRESS_MS * 1000;
    if (fault == 6) {
      result_write(6, 0xE1, NULL);
      result_write(6, 1, NULL);
      now_us += SOAK_PROGRESS_MS * 1000;
    }
    service_test();
    failed(fault == 6 ? "incomplete failure report" : "FAIL:");
  }
  fixture();
  send("f\n");
  load_to_running();
  const uint8_t error[] = {0xE1, 7, 3, 0x3C, 0x10, 0xFF, 0xFF, 0, 0};
  for (unsigned i = 0; i < sizeof(error); ++i)
    result_write(6, error[i], NULL);
  service_test();
  failed("stage=07 vector=3 address=103c actual=ffff expected=0000");

  fixture();
  send("f\n");
  load_to_running();
  for (unsigned phase = 0; phase < SOAK_SECONDS - 1; ++phase) {
    now_us += 1000000;
    result_write(6, (uint8_t)(0x40 + phase % 4), NULL);
    service_test();
    assert(soak_state == SOAK_RUN && !strstr(output, "PASS:"));
  }
  now_us += 999000;
  service_test();
  assert(soak_state == SOAK_RUN);
  now_us += 1000;
  service_test();
  assert(soak_state == SOAK_RUN); // Duration alone is not a complete cycle.
  result_write(6, 0x43, NULL);
  service_test();
  assert(soak_state == SOAK_IDLE && reset_held && !trap_enabled && !failures);
  assert(strstr(output, SOAK_HOURS == 1 ? "PASS: one-hour soak complete" :
                                        "PASS: 14-hour soak complete"));
  char expected[256];
  snprintf(expected, sizeof(expected), "pwm_ms=%u", SOAK_SECONDS * 1000);
  assert(strstr(output, expected));
  uint64_t phases = SOAK_SECONDS;
  snprintf(expected, sizeof(expected),
           "successes=%" PRIu64 " failures=0 sram_byte_checks=%" PRIu64
           " math_assertions=%" PRIu64 " stack_checks=%" PRIu64
           " loop_checks=%" PRIu64 " completed_cycles=%" PRIu64,
           phases * 79894, phases * 57344, phases * 22528,
           phases * 5, phases * 17, phases / 4);
  assert(strstr(output, expected));
  assert(writes == SOAK_IMAGE_SIZE && starts == 1);
  completed_phases = 60000;
  print_stats();
  assert(strstr(output, "successes=4793640000 failures=0"));
  puts("Math soak controller: duration/cycle boundaries, 64-bit counts, console and fail-closed faults passed");
  return 0;
}
