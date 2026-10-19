#ifndef Z80SBC_CLOCK_H
#define Z80SBC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

enum {
  Z80_CLOCK_MIN_HZ = 10,
#if defined(Z80_EXPERIMENTAL_20MHZ)
  Z80_CLOCK_MAX_HZ = 20000000,
#elif defined(Z80_EXPERIMENTAL_19MHZ)
  Z80_CLOCK_MAX_HZ = 19000000,
#elif defined(Z80_EXPERIMENTAL_18MHZ)
  Z80_CLOCK_MAX_HZ = 18000000,
#elif defined(Z80_EXPERIMENTAL_17MHZ)
  Z80_CLOCK_MAX_HZ = 17000000,
#elif defined(Z80_EXPERIMENTAL_16MHZ)
  Z80_CLOCK_MAX_HZ = 16000000,
#elif defined(Z80_EXPERIMENTAL_15MHZ)
  Z80_CLOCK_MAX_HZ = 15000000,
#elif defined(Z80_EXPERIMENTAL_14MHZ)
  Z80_CLOCK_MAX_HZ = 14000000,
#elif defined(Z80_EXPERIMENTAL_12MHZ)
  Z80_CLOCK_MAX_HZ = 12000000,
#else
  Z80_CLOCK_MAX_HZ = 8000000,
#endif
};

// Select the fastest stable 50% PWM rate not exceeding hz.
bool z80_clock_set_hz(uint32_t hz);
uint32_t z80_clock_get_hz(void);
void z80_clock_stop(void);
void z80_clock_resume(void);
// Return LOW after 1 us of settling; half_period_us must be > 0.
void z80_clock_one_cycle(uint32_t half_period_us);
void z80_reset_with_clock_cycles(unsigned int cycles,
                                 uint32_t half_period_us);

#endif