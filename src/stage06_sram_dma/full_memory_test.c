#include "full_memory_test.h"

#include <stdint.h>
#include <stdio.h>

#include "z80sbc/cpu.h"
#include "z80sbc/sram.h"

enum { MEMORY_SIZE = 65536 };

static bool active;
static bool verifying;
static unsigned pattern;
static uint32_t address;
static unsigned successful_checks;
static unsigned failed_checks;

static uint8_t expected_byte(void) {
  return pattern == 0 ? (uint8_t)address : (uint8_t)(address >> 8);
}

static void stop(void) {
  z80_cpu_fail_closed();
  active = false;
}

static void print_stats(void) {
  printf("STATS: successes=%u failures=%u\n", successful_checks, failed_checks);
}

static void start_pattern(void) {
  address = 0;
  verifying = false;
  if (!z80_cpu_prepare_loader()) {
    ++failed_checks;
    stop();
    printf("FAIL: loader initialization for pass %u/2\n", pattern + 1);
    print_stats();
    return;
  }
  active = true;
  printf("pass %u/2: writing 0000-ffff, pattern=address %s byte; x aborts\n",
         pattern + 1, pattern == 0 ? "low" : "high");
}

void z80_full_memory_test_start(void) {
  stop();
  pattern = 0;
  successful_checks = failed_checks = 0;
  start_pattern();
}

bool z80_full_memory_test_active(void) {
  return active;
}

void z80_full_memory_test_abort(const char *reason) {
  if (!active)
    return;
  stop();
  printf("ABORTED: %s; RESET# LOW, SRAM inhibited, data paths off\n", reason);
  print_stats();
}

void z80_full_memory_test_tick(void) {
  if (!active)
    return;

  uint8_t expected = expected_byte();
  if (!verifying) {
    if (!z80_sram_write_byte((uint16_t)address, expected)) {
      ++failed_checks;
      stop();
      printf("address %04x, value ERROR, expected %02x - FAIL (write)\n",
             (unsigned)address, expected);
      print_stats();
      return;
    }
  } else {
    uint8_t actual;
    if (!z80_sram_read_byte((uint16_t)address, &actual)) {
      ++failed_checks;
      stop();
      printf("address %04x, value ERROR, expected %02x - FAIL (read)\n",
             (unsigned)address, expected);
      print_stats();
      return;
    }
    bool matches = actual == expected;
    if (matches)
      ++successful_checks;
    else {
      ++failed_checks;
      stop();
    }
    printf("address %04x, value %02x, expected %02x - %s\n",
           (unsigned)address, actual, expected, matches ? "SUCCESS" : "FAIL");
    if (!matches) {
      print_stats();
      return;
    }
  }

  if (++address != MEMORY_SIZE)
    return;
  if (!verifying) {
    address = 0;
    verifying = true;
    printf("pass %u/2: reading 0000-ffff\n", pattern + 1);
  } else if (pattern == 0) {
    pattern = 1;
    start_pattern();
  } else {
    stop();
    printf("SUCCESS: 65536 addresses verified in each of 2 passes; RESET# LOW\n");
    print_stats();
  }
}
