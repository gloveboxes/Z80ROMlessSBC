#ifndef Z80SBC_CYCLE_STATS_H
#define Z80SBC_CYCLE_STATS_H

#include <stdint.h>

/* Cost of one core-0 service pass that did work. Core 0 is the only writer;
 * the status printer on another core reads and restarts the window. */
typedef struct {
  uint32_t passes;
  uint32_t max_cycles;
} z80_cycle_stats_t;

static inline void z80_cycle_stats_add(z80_cycle_stats_t *stats,
                                       uint32_t cycles) {
  __atomic_store_n(&stats->passes, stats->passes + 1, __ATOMIC_RELAXED);
  if (cycles > __atomic_load_n(&stats->max_cycles, __ATOMIC_RELAXED))
    __atomic_store_n(&stats->max_cycles, cycles, __ATOMIC_RELAXED);
}

static inline void z80_cycle_stats_take(z80_cycle_stats_t *stats,
                                        uint32_t *passes, uint32_t *max) {
  *passes = __atomic_exchange_n(&stats->passes, 0, __ATOMIC_RELAXED);
  *max = __atomic_exchange_n(&stats->max_cycles, 0, __ATOMIC_RELAXED);
}

#endif
