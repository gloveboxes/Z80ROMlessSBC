#ifndef Z80SBC_RAM_TIMER_H
#define Z80SBC_RAM_TIMER_H

#include <stdint.h>

#include "pico/stdlib.h"

/* Inlined copy of the SDK timer_time_us_64 loop for core-0 trap polls, so a
 * timer read never fetches SDK code from XIP flash. Host builds use the
 * time_us_64 fake.
 *
 * Hardware settling guards deliberately keep calling busy_wait_us_32: its
 * nominal 0-1 us wait plus call overhead is the hardware-qualified minimum.
 * An inlined loop can return after tens of ns, letting the release loop
 * sample IORQ# too early and step an extra Z80 clock. */
#if PICO_ON_DEVICE
#include "hardware/structs/m33.h"
#include "hardware/structs/timer.h"

/* Core-local Cortex-M33 DWT cycle counter (clk_sys ticks) for latency
 * statistics. Returns false if the counter is not implemented. */
static inline bool z80_cycle_counter_enable(void) {
  m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
  if (m33_hw->dwt_ctrl & M33_DWT_CTRL_NOCYCCNT_BITS)
    return false;
  m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
  return true;
}

static inline uint32_t z80_cycle_count(void) {
  return m33_hw->dwt_cyccnt;
}

static inline uint64_t z80_time_us_64(void) {
  uint32_t high = timer_hw->timerawh;
  uint32_t low;
  while (true) {
    low = timer_hw->timerawl;
    uint32_t next_high = timer_hw->timerawh;
    if (next_high == high)
      break;
    high = next_high;
  }
  return ((uint64_t)high << 32) | low;
}
#else
#define z80_time_us_64() time_us_64()
#define z80_cycle_counter_enable() true
/* Host fakes advance time_us_64; model 120 cycles per microsecond. */
#define z80_cycle_count() ((uint32_t)(time_us_64() * 120u))
#endif

#endif
