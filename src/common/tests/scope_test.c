#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void stdio_init_all(void);
int getchar_timeout_us(unsigned timeout);
static int capture_printf(const char *format, ...);
#define printf capture_printf
#define main scope_firmware_main
#include "../../stage06_sram_dma/scope_test.c"
#undef main
#undef printf

static uint8_t memory[65536];
static uint64_t now_us;
static bool connected, reset_held, prepare_ok, write_ok, read_ok, run_ok, clock_ok;
static bool corrupt_load;
static unsigned writes, image_reads, result_reads, starts, stops, preparations;
static uint32_t system_hz, selected_hz, forced_hz, run_hz;
static const char *input;
static char output[65536];
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
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
  assert(to >= from);
  return (int64_t)(to - from);
}
uint32_t clock_get_hz(uint clock) {
  assert(clock == clk_sys);
  return system_hz;
}
void set_sys_clock_pll(uint32_t vco, uint div1, uint div2) {
  assert(reset_held && !writes && !starts);
  assert(vco == 1200000000 && div1 == 5 && div2 == 2);
  system_hz = vco / div1 / div2;
}
bool z80_clock_set_hz(uint32_t hz) {
  assert(reset_held && hz == SCOPE_HZ);
  selected_hz = forced_hz ? forced_hz : hz;
  return clock_ok;
}
uint32_t z80_clock_get_hz(void) { return selected_hz; }
void z80_cpu_fail_closed(void) { reset_held = true; ++stops; }
bool z80_cpu_prepare_loader(void) {
  assert(reset_held);
  ++preparations;
  return prepare_ok;
}
bool z80_sram_write_byte(uint16_t address, uint8_t value) {
  assert(scope_state == SCOPE_LOAD && address == writes);
  memory[address] = value;
  ++writes;
  return write_ok;
}
bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  assert(reset_held);
  if (scope_state == SCOPE_VERIFY) {
    assert(address == image_reads++);
    *value = memory[address] ^ (corrupt_load ? 1 : 0);
  } else {
    assert(scope_state == SCOPE_READ);
    assert(address == SCOPE_MAILBOX_BASE + scope_index);
    ++result_reads;
    *value = memory[address];
  }
  return read_ok;
}
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  assert(reset_held && hz == SCOPE_HZ);
  assert(writes == SOAK_IMAGE_SIZE && image_reads == SOAK_IMAGE_SIZE);
  ++starts;
  reset_held = false;
  if (run_hz)
    selected_hz = run_hz;
  return run_ok;
}

static void fixture(void) {
  scope_state = SCOPE_IDLE;
  now_us = elapsed_ms = scope_index = command_length = output_length = 0;
  writes = image_reads = result_reads = starts = stops = preparations = 0;
  selected_hz = forced_hz = run_hz = 0;
  connected = reset_held = prepare_ok = write_ok = read_ok = run_ok = clock_ok = true;
  was_connected = discard_line = has_run = corrupt_load = false;
  input = NULL;
  output[0] = '\0';
  memset(memory, 0xEE, sizeof(memory));
  configure_system_clock();
  assert(system_hz == SCOPE_SYSTEM_HZ);
}

static void send(const char *text) {
  input = text;
  while (*input) {
    poll_console();
    service_test();
  }
}

static void run_until(scope_state_t state) {
  for (unsigned i = 0; scope_state != state; ++i) {
    assert(i < SOAK_IMAGE_SIZE * 2);
    assert(scope_state != SCOPE_IDLE);
    service_test();
  }
}

static void start_running(void) {
  send("f\n");
  run_until(SCOPE_RUN);
  assert(!reset_held && has_run && selected_hz == SCOPE_HZ);
  assert(memcmp(memory, scope_image, SOAK_IMAGE_SIZE) == 0);
}

static uint8_t *set_result(uint32_t phases, bool failed) {
  uint8_t *mailbox = memory + SCOPE_MAILBOX_BASE;
  memset(mailbox, 0, SCOPE_MAILBOX_SIZE);
  memcpy(mailbox, "SCP1", 4);
  mailbox[SCOPE_STATE] = failed ? SCOPE_FAILED : SCOPE_RUNNING;
  soak_word(mailbox + SCOPE_PHASES, (uint16_t)phases);
  soak_word(mailbox + SCOPE_PHASES + 2, (uint16_t)(phases >> 16));
  mailbox[SCOPE_PATTERN] = phases ? (uint8_t)((phases - 1) % 4) : 0xFF;
  mailbox[SCOPE_STAGE] = 7;
  mailbox[SCOPE_VECTOR] = 3;
  soak_word(mailbox + SCOPE_ADDRESS, 0x103C);
  soak_word(mailbox + SCOPE_ACTUAL, 0xFFFF);
  assert(scope_snapshot_valid(mailbox));
  return mailbox;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "--emit-image") == 0) {
    uint8_t image[SOAK_IMAGE_SIZE];
    scope_build_image(image);
    FILE *file = fopen(argv[2], "wb");
    if (!file) {
      perror("Opening scope image");
      return 1;
    }
    bool written = fwrite(image, 1, sizeof(image), file) == sizeof(image);
    int closed = fclose(file);
    if (!written || closed != 0) {
      fprintf(stderr, "Failed to write complete scope image\n");
      return 1;
    }
    return 0;
  }
  assert(argc == 1);
  fixture();
  poll_console();
  send("h\ns\nff\nr\n");
  assert(scope_state == SCOPE_IDLE && !writes && !starts);
  connected = false;
  input = "f\n";
  poll_console();
  input = NULL;
  assert(!writes);
  connected = true;
  send("f\nx");
  assert(scope_state == SCOPE_IDLE && !has_run && !result_reads);
  send("f\n");
  assert(scope_state == SCOPE_IDLE);

  fixture();
  start_running();
  unsigned old_stops = stops, old_preparations = preparations;
  now_us += UINT64_C(7200000000);
  send("s\nf\nr\nh\n");
  service_test();
  assert(scope_state == SCOPE_RUN && !result_reads);
  assert(stops == old_stops && preparations == old_preparations && starts == 1);
  assert(strstr(output, "CLOCK RUNNING") && !strstr(output, "PASS:"));
  set_result(8, false);
  send("x");
  assert(scope_state == SCOPE_READ && reset_held);
  run_until(SCOPE_IDLE);
  assert(result_reads == SCOPE_MAILBOX_SIZE && writes == SOAK_IMAGE_SIZE);
  assert(elapsed_ms == 7200000);
  assert(strstr(output, "phases=8 completed_cycles=2 sram_byte_checks=458752"));
  assert(strstr(output, "completed_phase_checks=639152"));
  assert(strstr(output, "RESULT: no mismatch") && !strstr(output, "PASS:"));

  fixture();
  start_running();
  set_result(4, true);
  send("x");
  run_until(SCOPE_IDLE);
  assert(strstr(output, "FAIL: Z80 mismatch stage=07 vector=3 address=103c "
                        "actual=ffff expected=0000"));

  for (unsigned fault = 0; fault < 10; ++fault) {
    fixture();
    if (fault == 0) system_hz--;
    if (fault == 1) clock_ok = false;
    if (fault == 2) forced_hz = 19000000;
    if (fault == 3) prepare_ok = false;
    if (fault == 4) write_ok = false;
    if (fault == 5) read_ok = false;
    if (fault == 6) corrupt_load = true;
    if (fault == 7) run_ok = false;
    if (fault == 8) run_hz = 19000000;
    send("f\n");
    if (fault == 9) send("x");
    if (scope_state != SCOPE_IDLE)
      run_until(SCOPE_IDLE);
    assert(scope_state == SCOPE_IDLE && reset_held && !has_run);
    assert(fault == 9 || strstr(output, "FAIL:"));
    assert(!strstr(output, "PASS:"));
  }
  for (unsigned active = 0; active < 4; ++active) {
    fixture();
    if (active >= 2)
      start_running();
    else {
      send("f\n");
      if (active == 1)
        run_until(SCOPE_VERIFY);
    }
    if (active == 3) {
      set_result(4, false);
      send("x");
    }
    connected = false;
    poll_console();
    assert(scope_state == SCOPE_IDLE && reset_held);
    unsigned previous_starts = starts, previous_reads = result_reads;
    connected = true;
    poll_console();
    service_test();
    assert(starts == previous_starts && result_reads == previous_reads);
    if (active >= 2) {
      set_result(4, false);
      send("\nr\n");
      run_until(SCOPE_IDLE);
      assert(result_reads >= SCOPE_MAILBOX_SIZE && reset_held);
    }
  }
  for (unsigned fault = 0; fault < 5; ++fault) {
    fixture();
    start_running();
    uint8_t *mailbox = set_result(fault == 4 ? 0 : 4, false);
    if (fault == 0) prepare_ok = false;
    if (fault == 1) read_ok = false;
    if (fault == 2) mailbox[0] ^= 1;
    if (fault == 3) mailbox[SCOPE_BUSY] = 1;
    send("x");
    if (scope_state != SCOPE_IDLE)
      run_until(SCOPE_IDLE);
    assert(reset_held && !strstr(output, "PASS:"));
    assert(strstr(output, fault < 2 ? "FAIL:" : "INCONCLUSIVE:"));
  }
  fixture();
  for (unsigned fault = 0; fault < 10; ++fault) {
    uint8_t *mailbox = set_result(65536, true);
    if (fault == 0) mailbox[SCOPE_STATE] = 0;
    if (fault == 1) mailbox[SCOPE_PATTERN] = 1;
    if (fault == 2) mailbox[SCOPE_STAGE] = 0x13;
    if (fault == 3) mailbox[SCOPE_VECTOR] = 128;
    if (fault == 4) soak_word(mailbox + SCOPE_ACTUAL, 0);
    if (fault >= 5) {
      mailbox[SCOPE_STAGE] = 0x80;
      mailbox[SCOPE_VECTOR] = 0xFF;
      soak_word(mailbox + SCOPE_ADDRESS, 0x8000);
      soak_word(mailbox + SCOPE_ACTUAL, 1);
      if (fault == 5) mailbox[SCOPE_VECTOR] = 1;
      if (fault == 6) soak_word(mailbox + SCOPE_ADDRESS, 0x1FFF);
      if (fault == 7) mailbox[SCOPE_ACTUAL + 1] = 1;
      if (fault == 8) mailbox[SCOPE_EXPECTED + 1] = 1;
      if (fault == 9) mailbox[SCOPE_BUSY] = 1;
    }
    assert(!scope_snapshot_valid(mailbox));
  }
  puts("Scope controller: gating, quiet run, readback, snapshots and faults passed");
  return 0;
}
