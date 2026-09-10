#ifndef Z80SBC_TEST_PROGRESS_H
#define Z80SBC_TEST_PROGRESS_H

#include "pico/stdlib.h"

enum { Z80_TEST_PROGRESS_TIMEOUT_MS = 5000 };

typedef struct {
  uint32_t last_count;
  absolute_time_t deadline;
  bool observed;
} z80_test_progress_t;

static inline void z80_test_progress_start(z80_test_progress_t *progress,
                                           uint32_t count) {
  progress->last_count = count;
  progress->deadline = make_timeout_time_ms(Z80_TEST_PROGRESS_TIMEOUT_MS);
  progress->observed = false;
}

static inline bool z80_test_progress_poll(z80_test_progress_t *progress,
                                          uint32_t count) {
  if (time_reached(progress->deadline))
    return false;
  if (count != progress->last_count) {
    progress->last_count = count;
    progress->observed = true;
    progress->deadline = make_timeout_time_ms(Z80_TEST_PROGRESS_TIMEOUT_MS);
  }
  return true;
}

#endif