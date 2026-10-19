#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void stdio_init_all(void);
int getchar_timeout_us(unsigned timeout);
static int capture_printf(const char *format, ...);
#define printf capture_printf
#define main pwm_firmware_main
#include "../../stage06_sram_dma/pwm_memory_test.c"
#undef main
#undef printf

static uint8_t memory[65536];
static uint64_t now_us;
static bool connected, reset_held, trap_enabled;
static bool prepare_ok, write_ok, read_ok, run_ok, clock_ok;
static bool trap_ok, corrupt_load;
static unsigned preparations, writes, reads, starts, selections, stops;
static uint32_t selected_hz, fake_timeouts, fake_control_errors;
static uint32_t system_hz;
static unsigned pll_configurations;
static char output[32768];
static size_t output_length;
static const char *input;

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
void z80_cpu_fail_closed(void) { ++stops; reset_held = true; }
bool z80_cpu_prepare_loader(void) {
  ++preparations;
  assert(reset_held && !trap_enabled);
  return prepare_ok;
}
bool z80_sram_write_byte(uint16_t address, uint8_t value) {
  assert(state == LOAD_IMAGE && !trap_enabled);
  ++writes;
  if (!write_ok)
    return false;
  memory[address] = value;
  return true;
}
bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  assert(state == VERIFY_IMAGE && !trap_enabled);
  ++reads;
  *value = memory[address] ^ (corrupt_load ? 1 : 0);
  return read_ok;
}
static uint32_t nominal(uint32_t hz) {
  uint32_t count = (system_hz + hz - 1) / hz;
  count = (count + 1) & ~1u;
  return (system_hz + count / 2) / count;
}
uint32_t clock_get_hz(uint clock) {
  assert(clock == clk_sys);
  return system_hz;
}
void set_sys_clock_pll(uint32_t vco_freq, uint post_div1, uint post_div2) {
  assert(reset_held && !trap_enabled && starts == 0 && writes == 0);
#if defined(Z80_EXPERIMENTAL_20MHZ)
  assert(vco_freq == 1200000000 && post_div1 == 5 && post_div2 == 2);
#elif defined(Z80_EXPERIMENTAL_19MHZ)
  assert(vco_freq == 1140000000 && post_div1 == 5 && post_div2 == 2);
#elif defined(Z80_EXPERIMENTAL_17MHZ)
  assert(vco_freq == 816000000 && post_div1 == 3 && post_div2 == 2);
#elif defined(Z80_EXPERIMENTAL_16MHZ)
  assert(vco_freq == 1536000000 && post_div1 == 6 && post_div2 == 2);
#elif defined(Z80_EXPERIMENTAL_15MHZ)
  assert(vco_freq == 1500000000 && post_div1 == 5 && post_div2 == 2);
#else
  assert(vco_freq == 840000000 && post_div1 == 3 && post_div2 == 2);
#endif
  system_hz = vco_freq / post_div1 / post_div2;
  ++pll_configurations;
}
bool z80_clock_set_hz(uint32_t hz) {
  assert(reset_held && hz >= MIN_HZ && hz <= MAX_HZ);
  ++selections;
  selected_hz = nominal(hz);
  return clock_ok;
}
uint32_t z80_clock_get_hz(void) { return selected_hz; }
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  assert(trap_enabled && reset_held && hz == requested_hz);
  ++starts;
  reset_held = false;
  selected_hz = nominal(hz);
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
uint32_t z80_io_trap_control_error_count(void) { return fake_control_errors; }

static void fixture(void) {
  state = IDLE;
  now_us = 0;
  reset_held = connected = true;
  trap_enabled = was_connected = discard_line = false;
  prepare_ok = write_ok = read_ok = run_ok = clock_ok = trap_ok = true;
  corrupt_load = false;
  preparations = writes = reads = starts = selections = stops = 0;
  selected_hz = fake_timeouts = fake_control_errors = 0;
  command_length = output_length = 0;
  total_checks = checks = running_us = 0;
  last_passed_hz = passed_speeds = cycles = bank = 0;
  requested_hz = actual_hz = 0;
  input = NULL;
  output[0] = '\0';
  memset(memory, 0xA5, sizeof(memory));
  system_hz = 144000000;
  pll_configurations = 0;
  configure_system_clock();
  assert(system_hz == SYSTEM_HZ);
#if defined(Z80_EXPERIMENTAL_14MHZ) || defined(Z80_EXPERIMENTAL_15MHZ) || \
    defined(Z80_EXPERIMENTAL_16MHZ) || defined(Z80_EXPERIMENTAL_17MHZ) || \
    defined(Z80_EXPERIMENTAL_19MHZ) || defined(Z80_EXPERIMENTAL_20MHZ)
  assert(pll_configurations == 1);
#else
  assert(pll_configurations == 0);
#endif
}

static void send(const char *text) {
  input = text;
  while (*input) {
    poll_console();
    service_test();
  }
}

static void load_to_running(void) {
  for (unsigned tick = 0; state != RUNNING; ++tick) {
    assert(state != IDLE && tick < 1024);
    service_test();
  }
}

static void simulated_bank(void) {
  load_to_running();
  unsigned old_bank = bank;
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    now_us += 4000000;
    result_write(RESULT_PORT, (uint8_t)(0x40 + old_bank * 4 + pattern), NULL);
    service_test();
  }
}

/* An independent instruction decoder executes every generated fill/read loop. */
static void execute_bank(bool alias, bool corrupt_last) {
  load_to_running();
  const unsigned executing_bank = bank;
  const uint16_t base = code_base, target = data_base;
  const size_t length = image_length;
  uint16_t pc = 0, hl = 0;
  uint8_t a = 0, d = 0, e = 0;
  bool zero = false;
  unsigned completed = 0;
  unsigned long steps = 0;
  uint32_t written = 0, verified = 0;
  while (state == RUNNING) {
    assert(++steps < 5000000);
    assert((pc >= base && (uint32_t)pc < base + length) ||
           (executing_bank == 1 && pc < 3));
    uint8_t opcode = memory[pc++];
    switch (opcode) {
      case 0xF3: now_us += 4; break; // DI
      case 0x21: // LD HL,nn
        hl = (uint16_t)(memory[pc] | (uint16_t)memory[pc + 1] << 8);
        pc += 2; now_us += 10; break;
      case 0x7C: a = (uint8_t)(hl >> 8); now_us += 4; break;
      case 0x7D: a = (uint8_t)hl; now_us += 4; break;
      case 0xEE: a ^= memory[pc++]; zero = a == 0; now_us += 7; break;
      case 0x77: { // LD (HL),A
        assert((hl & 0x8000) == target);
        assert(hl == (uint16_t)(target + written));
        assert(verified == 0 && written < BANK_SIZE);
        uint16_t mapped = alias ? (uint16_t)(target | (hl & 0xFF)) : hl;
        memory[mapped] = a;
        if (corrupt_last && written == BANK_SIZE - 1)
          memory[mapped] ^= 1;
        ++written;
        now_us += 7;
        break;
      }
      case 0x23: ++hl; now_us += 6; break;
      case 0xE6: a &= memory[pc++]; zero = a == 0; now_us += 7; break;
      case 0xB5: a |= (uint8_t)hl; zero = a == 0; now_us += 4; break;
      case 0x20: {
        int8_t offset = (int8_t)memory[pc++];
        if (!zero)
          pc = (uint16_t)(pc + offset);
        now_us += zero ? 7 : 12;
        break;
      }
      case 0x57: d = a; now_us += 4; break;
      case 0x7E: { // LD A,(HL)
        assert((hl & 0x8000) == target && written == BANK_SIZE);
        assert(hl == (uint16_t)(target + verified));
        uint16_t mapped = alias ? (uint16_t)(target | (hl & 0xFF)) : hl;
        a = memory[mapped];
        ++verified;
        now_us += 7;
        break;
      }
      case 0xBA: zero = a == d; now_us += 4; break;
      case 0xC2:
      case 0xC3: {
        uint16_t destination =
            (uint16_t)(memory[pc] | (uint16_t)memory[pc + 1] << 8);
        pc += 2;
        if (opcode == 0xC3 || !zero)
          pc = destination;
        now_us += 10;
        break;
      }
      case 0x3E: a = memory[pc++]; now_us += 7; break;
      case 0x5F: e = a; now_us += 4; break;
      case 0x7B: a = e; now_us += 4; break;
      case 0x7A: a = d; now_us += 4; break;
      case 0xD3: {
        uint8_t port = memory[pc++];
        assert(port == RESULT_PORT);
        now_us += 11;
        if (error_length == 0 && a != 0xE1) {
          assert(written == BANK_SIZE && verified == BANK_SIZE);
          assert(a == 0x40 + executing_bank * 4 + completed);
          for (uint32_t offset = 0; offset < BANK_SIZE; ++offset) {
            uint16_t address = (uint16_t)(target + offset);
            uint8_t expected = completed & 1 ? address >> 8 : address & 0xFF;
            if (completed & 2)
              expected ^= 0xFF;
            uint16_t mapped = alias ? (uint16_t)(target | (address & 0xFF))
                                    : address;
            assert(memory[mapped] == expected);
          }
          ++completed;
          written = verified = 0;
        }
        result_write(port, a, NULL);
        service_test();
        break;
      }
      default: assert(!"invalid generated Z80 opcode");
    }
  }
  if (alias || corrupt_last) {
    assert(state == IDLE && strstr(output, "CPU-detected RAM mismatch"));
    assert(error_length == 5);
    uint16_t failed = (uint16_t)(error_bytes[0] | (uint16_t)error_bytes[1] << 8);
    assert(failed == (corrupt_last ? (uint16_t)(target + BANK_SIZE - 1) : target));
    assert(error_bytes[2] != error_bytes[3]);
    assert(completed == (alias ? 1u : 0u));
  } else {
    assert(completed == 4 && state == LOAD_IMAGE);
    assert(checks >= (uint64_t)4 * BANK_SIZE);
  }
}

int main(void) {
  fixture();
  connected = false;
  for (unsigned i = 0; i < 10; ++i) {
    poll_console(); service_test();
  }
  assert(preparations == 0 && selections == 0 && writes == 0 && starts == 0);
  connected = true;
  poll_console(); service_test();
  assert(strstr(output, "waiting for f + Enter"));
  assert(preparations == 0 && selections == 0);
  send("f");
  assert(state == IDLE);
  send("\r\n");
  assert(state == LOAD_IMAGE && preparations == 1 && selections == 1);
  send("f\n");
  assert(preparations == 1 && strstr(output, "already active"));
  send("x");
  assert(state == IDLE && reset_held && !trap_enabled);
  assert(strstr(output, "operator abort") && strstr(output, "failures=0"));
  send("\nf\n");
  load_to_running();
  connected = false;
  poll_console();
  assert(state == IDLE && reset_held && !trap_enabled);
  assert(strstr(output, "console disconnected"));

  fixture();
  start_test();
  execute_bank(false, false);
  assert(bank == 1 && checks == 131072);
  execute_bank(false, false);
  assert(bank == 0 && cycles == 1 && checks == 262144);
  assert(running_us < (uint64_t)SCREEN_MS * 1000 && passed_speeds == 0);

  for (unsigned b = 0; b < 2; ++b) {
    fixture(); start_test();
    if (b) { bank = 1; prepare_bank(); }
    execute_bank(true, false);
    assert(checks == BANK_SIZE && reset_held && !trap_enabled);
    fixture(); start_test();
    if (b) { bank = 1; prepare_bank(); }
    execute_bank(false, true);
    assert(checks == 0 && reset_held && !trap_enabled);
  }

  fixture();
  start_test();
  for (unsigned halves = 0; state != IDLE; ++halves) {
    assert(halves < 60);
    simulated_bank();
  }
#if defined(Z80_EXPERIMENTAL_17MHZ) || defined(Z80_EXPERIMENTAL_18MHZ) || \
    defined(Z80_EXPERIMENTAL_19MHZ) || defined(Z80_EXPERIMENTAL_20MHZ)
  const unsigned expected_speeds = 1;
  assert(MIN_HZ == MAX_HZ && MIN_HZ >= 17000000 && MAX_HZ <= 20000000);
  assert(selections == 1 && strstr(output, "SKIP:") == NULL);
  assert(selected_hz == MAX_HZ);
  assert(strstr(output, "completed_check_successes=524288 failures=0"));
#elif defined(Z80_EXPERIMENTAL_16MHZ)
  const unsigned expected_speeds = 2;
  assert(MIN_HZ == 15000000 && MAX_HZ == 16000000);
  assert(selections == 3 && strstr(output, "SKIP: requested=15500000Hz"));
  assert(strstr(output, "actual=12800000Hz") &&
         strstr(output, "actual=16000000Hz"));
  assert(strstr(output, "completed_check_successes=1048576 failures=0"));
#elif defined(Z80_EXPERIMENTAL_15MHZ)
  const unsigned expected_speeds = 2;
  assert(MIN_HZ == 14000000 && MAX_HZ == 15000000);
  assert(selections == 3 && strstr(output, "SKIP: requested=14500000Hz"));
  assert(strstr(output, "actual=12500000Hz") &&
         strstr(output, "actual=15000000Hz"));
  assert(strstr(output, "completed_check_successes=1048576 failures=0"));
#elif defined(Z80_EXPERIMENTAL_14MHZ)
  const unsigned expected_speeds = 2;
  assert(MIN_HZ == 12000000 && MAX_HZ == 14000000);
  assert(selections == 5 && strstr(output, "SKIP: requested=12500000Hz"));
  assert(strstr(output, "actual=11666667Hz") &&
         strstr(output, "actual=14000000Hz"));
  assert(strstr(output, "completed_check_successes=1048576 failures=0"));
#elif defined(Z80_EXPERIMENTAL_12MHZ)
  const unsigned expected_speeds = 4;
  assert(MIN_HZ == 8000000 && MAX_HZ == 12000000);
  assert(selections == 9 && strstr(output, "SKIP: requested=8500000Hz"));
  assert(strstr(output, "actual=9000000Hz") &&
         strstr(output, "actual=10285714Hz") &&
         strstr(output, "actual=12000000Hz"));
  assert(strstr(output, "completed_check_successes=2097152 failures=0"));
#else
  const unsigned expected_speeds = 14;
  assert(MIN_HZ == 1000000 && MAX_HZ == 8000000);
  assert(selections == 15 && strstr(output, "SKIP: requested=6500000Hz"));
  assert(strstr(output, "completed_check_successes=7340032 failures=0"));
#endif
  assert(passed_speeds == expected_speeds && last_passed_hz == MAX_HZ);
  assert(strstr(output, "EXPERIMENTAL") && strstr(output, "screening complete"));
  assert(total_checks == (uint64_t)expected_speeds * 2 * 4 * 65536 && checks == 0);
  assert(reset_held && !trap_enabled);
  unsigned old_stops = stops;
  service_test();
  assert(stops == old_stops);
  send("f\n");
  assert(total_checks == 0 && passed_speeds == 0);

  for (unsigned fault = 0; fault < 9; ++fault) {
    fixture();
    switch (fault) {
      case 0: clock_ok = false; break;
      case 1: prepare_ok = false; break;
      case 2: write_ok = false; break;
      case 3: read_ok = false; break;
      case 4: corrupt_load = true; break;
      case 5: trap_ok = false; break;
      case 6: run_ok = false; break;
      case 7: break;
      case 8: --system_hz; break;
    }
    start_test();
    for (unsigned ticks = 0; state != IDLE; ++ticks) {
      assert(ticks < 1024);
      if (state == RUNNING)
        now_us += PROGRESS_TIMEOUT_MS * 1000;
      service_test();
    }
    assert(reset_held && !trap_enabled && strstr(output, "failures=1"));
    assert(passed_speeds == 0 && last_passed_hz == 0);
  }
  for (unsigned fault = 0; fault < 6; ++fault) {
    fixture(); start_test(); load_to_running();
    switch (fault) {
      case 0: result_write(RESULT_PORT, 0x41, NULL); break;
      case 1: result_write(0, 0x40, NULL); break;
      case 2: result_read(RESULT_PORT, NULL); break;
      case 3: ++fake_timeouts; break;
      case 4: ++fake_control_errors; break;
      case 5:
        result_write(RESULT_PORT, 0xE1, NULL);
        result_write(RESULT_PORT, 0xFF, NULL);
        now_us += PROGRESS_TIMEOUT_MS * 1000;
        break;
    }
    service_test();
    assert(state == IDLE && reset_held && !trap_enabled);
    assert(strstr(output, "failures=1"));
  }
  for (unsigned stage = 0; stage < 3; ++stage) {
    fixture(); start_test();
    if (stage >= 1)
      while (state == LOAD_IMAGE) service_test();
    if (stage == 2)
      load_to_running();
    send("x");
    assert(state == IDLE && reset_held && !trap_enabled);
  }
  puts("PASS: PWM program execution, bank coverage, patterns, sweep, duration, faults and console gating");
}
