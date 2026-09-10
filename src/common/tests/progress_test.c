#include <assert.h>
#include <stdio.h>
#include "z80sbc/test_progress.h"

static uint64_t now_us;
absolute_time_t make_timeout_time_ms(uint32_t delay) {
  return now_us + (uint64_t)delay * 1000u;
}
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }

int main(void) {
  z80_test_progress_t progress;
  z80_test_progress_start(&progress, 42);
  assert(!progress.observed);
  now_us = 4999000;
  assert(z80_test_progress_poll(&progress, 42));
  now_us = 5000000;
  assert(!z80_test_progress_poll(&progress, 42));

  z80_test_progress_start(&progress, 42);
  now_us += 1700000;
  assert(z80_test_progress_poll(&progress, 43) && progress.observed);
  now_us += 4999000;
  assert(z80_test_progress_poll(&progress, 43));
  now_us += 1000;
  assert(!z80_test_progress_poll(&progress, 43));
  assert(!z80_test_progress_poll(&progress, 44));

  z80_test_progress_start(&progress, UINT32_MAX);
  ++now_us;
  assert(z80_test_progress_poll(&progress, 0) && progress.observed);

  z80_test_progress_start(&progress, 0);
  for (uint32_t seconds = 1; seconds <= 3600; ++seconds) {
    now_us += 1000000;
    assert(z80_test_progress_poll(&progress, seconds));
  }
  assert(progress.observed);
  now_us += 5000000;
  assert(!z80_test_progress_poll(&progress, 3600));
  puts("PASS: missing heartbeat, stalled CPU, counter wrap and one-hour progress");
  return 0;
}