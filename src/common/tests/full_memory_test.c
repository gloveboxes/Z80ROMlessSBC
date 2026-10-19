#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int result_printf(const char *format, ...);
#define printf result_printf
#include "../../stage06_sram_dma/full_memory_test.c"
#undef printf

static uint8_t memory[65536];
static unsigned preparations;
static unsigned stops;
static unsigned writes;
static unsigned reads;
static unsigned rows;
static unsigned failures;
static unsigned reported_successes;
static unsigned reported_failures;
static unsigned stats_lines;
static unsigned success_rows;
static bool summary;
static bool alias;
static bool prepare_failure;
static bool write_failure;
static bool read_failure;
static bool corrupt_last;
static char last_line[256];

static int result_printf(const char *format, ...) {
  char output[256];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(output, sizeof(output), format, args);
  va_end(args);
  assert(length > 0 && (size_t)length < sizeof(output));
  assert(output[length - 1] == '\n');
  assert(strchr(output, '\n') == output + length - 1);
  if (strncmp(output, "STATS:", 6) == 0) {
    int consumed = 0;
    assert(sscanf(output, "STATS: successes=%u failures=%u%n",
                  &reported_successes, &reported_failures, &consumed) == 2);
    assert(strcmp(output + consumed, "\n") == 0);
    assert(reported_successes == success_rows);
    assert(!z80_full_memory_test_active());
    ++stats_lines;
    return length;
  }
  memcpy(last_line, output, (size_t)length + 1);
  if (strncmp(last_line, "address ", 8) == 0) {
    ++rows;
    if (strstr(last_line, "ERROR")) {
      assert(strstr(last_line, " - FAIL"));
      ++failures;
      return length;
    }
    unsigned reported_address, value, expected;
    char result[16];
    int consumed = 0;
    assert(sscanf(last_line, "address %x, value %x, expected %x - %15s%n",
                  &reported_address, &value, &expected, result, &consumed) == 4);
    assert(strcmp(last_line + consumed, "\n") == 0);
    assert(reported_address == ((reads - 1) & 0xFFFFu));
    unsigned expected_value = preparations == 1 ?
        reported_address & 0xFFu : reported_address >> 8;
    assert(expected == expected_value);
    if (strcmp(result, "SUCCESS") == 0) {
      assert(value == expected);
      ++success_rows;
    } else {
      assert(strcmp(result, "FAIL") == 0 && value != expected);
      ++failures;
    }
  } else if (strncmp(last_line, "SUCCESS:", 8) == 0) {
    summary = true;
  }
  return length;
}

bool z80_cpu_prepare_loader(void) {
  ++preparations;
  return !prepare_failure;
}
void z80_cpu_fail_closed(void) { ++stops; }
bool z80_sram_write_byte(uint16_t target, uint8_t value) {
  assert(target == (writes & 0xFFFFu));
  assert((preparations == 1 && reads == 0) ||
         (preparations == 2 && reads == 65536));
  assert(value == (preparations == 1 ? target & 0xFFu : target >> 8));
  ++writes;
  if (write_failure)
    return false;
  memory[alias ? target & 0xFF : target] =
      value ^ (corrupt_last && target == 0xFFFF ? 1 : 0);
  return true;
}
bool z80_sram_read_byte(uint16_t target, uint8_t *value) {
  assert(target == (reads & 0xFFFFu));
  assert(writes == preparations * 65536);
  ++reads;
  if (read_failure)
    return false;
  *value = memory[alias ? target & 0xFF : target];
  return true;
}

static void fixture(void) {
  active = false;
  preparations = stops = writes = reads = rows = failures = 0;
  reported_successes = reported_failures = stats_lines = success_rows = 0;
  summary = alias = prepare_failure = write_failure = read_failure = false;
  corrupt_last = false;
  memset(memory, 0xA5, sizeof(memory));
  last_line[0] = 0;
}

static void run_to_stop(void) {
  for (unsigned tick = 0; z80_full_memory_test_active(); ++tick) {
    assert(tick < 262145);
    z80_full_memory_test_tick();
  }
}

int main(void) {
  fixture();
  z80_full_memory_test_start();
  assert(writes == 0 && reads == 0 && z80_full_memory_test_active());
  run_to_stop();
  assert(writes == 131072 && reads == 131072 && rows == 131072);
  assert(preparations == 2 && summary && failures == 0 && stops == 2);
  assert(stats_lines == 1 && reported_successes == 131072 &&
         reported_failures == 0);
  for (unsigned target = 0; target < sizeof(memory); ++target)
    assert(memory[target] == target >> 8);
  z80_full_memory_test_tick();
  assert(writes == 131072 && reads == 131072 && stats_lines == 1);

  fixture();
  alias = true;
  z80_full_memory_test_start();
  run_to_stop();
  assert(preparations == 2 && writes == 131072 && reads == 65537);
  assert(rows == 65537 && failures == 1 && !summary);
  assert(stats_lines == 1 && reported_successes == 65536 &&
         reported_failures == 1);
  assert(strcmp(last_line,
                "address 0000, value ff, expected 00 - FAIL\n") == 0);

  fixture();
  corrupt_last = true;
  z80_full_memory_test_start();
  run_to_stop();
  assert(writes == 65536 && reads == 65536 && failures == 1);
  assert(stats_lines == 1 && reported_successes == 65535 &&
         reported_failures == 1);
  assert(strcmp(last_line,
                "address ffff, value fe, expected ff - FAIL\n") == 0);
  assert(!summary && preparations == 1);

  fixture();
  prepare_failure = true;
  z80_full_memory_test_start();
  assert(!z80_full_memory_test_active() && writes == 0 && reads == 0);
  assert(strstr(last_line, "FAIL: loader initialization"));
  assert(stats_lines == 1 && reported_successes == 0 && reported_failures == 1);

  fixture();
  z80_full_memory_test_start();
  for (unsigned tick = 0; tick < 131071; ++tick)
    z80_full_memory_test_tick();
  prepare_failure = true;
  z80_full_memory_test_tick();
  assert(!z80_full_memory_test_active() && preparations == 2);
  assert(writes == 65536 && reads == 65536 && !summary);
  assert(strstr(last_line, "FAIL: loader initialization for pass 2/2"));
  assert(stats_lines == 1 && reported_successes == 65536 &&
         reported_failures == 1);

  fixture();
  write_failure = true;
  z80_full_memory_test_start();
  run_to_stop();
  assert(writes == 1 && reads == 0 && failures == 1 && !summary);
  assert(strstr(last_line, "value ERROR, expected 00 - FAIL (write)"));
  assert(stats_lines == 1 && reported_successes == 0 && reported_failures == 1);

  fixture();
  read_failure = true;
  z80_full_memory_test_start();
  run_to_stop();
  assert(writes == 65536 && reads == 1 && failures == 1 && !summary);
  assert(strstr(last_line, "value ERROR, expected 00 - FAIL (read)"));
  assert(stats_lines == 1 && reported_successes == 0 && reported_failures == 1);

  const unsigned abort_ticks[] = {0, 1, 65535, 65536, 65537, 131072, 196608};
  for (unsigned trial = 0; trial < sizeof(abort_ticks) / sizeof(abort_ticks[0]);
       ++trial) {
    fixture();
    z80_full_memory_test_start();
    for (unsigned tick = 0; tick < abort_ticks[trial]; ++tick)
      z80_full_memory_test_tick();
    unsigned before = writes + reads;
    unsigned previous_stops = stops;
    z80_full_memory_test_abort("operator");
    assert(!z80_full_memory_test_active() && stops == previous_stops + 1);
    assert(strstr(last_line, "ABORTED: operator"));
    assert(stats_lines == 1 && reported_successes == reads &&
           reported_failures == 0);
    z80_full_memory_test_tick();
    assert(writes + reads == before && !summary);
    z80_full_memory_test_abort("operator");
    assert(stats_lines == 1);
    z80_full_memory_test_start();
    assert(z80_full_memory_test_active() && address == 0 && pattern == 0);
    assert(successful_checks == 0 && failed_checks == 0);
  }
  puts("PASS: 64 KiB fill/read passes, all rows, wrap, high-address aliases,"
       " failures and abort at phase boundaries");
}
