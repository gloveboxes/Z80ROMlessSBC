#ifndef Z80SBC_CLOCK_H
#define Z80SBC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

enum {
#if defined(Z80_BANKED_PIO)
  /* PIO1 generates exactly 20 MHz from the 120 MHz supervisor clock. */
  Z80_CLOCK_MIN_HZ = 20000000,
  Z80_CLOCK_MAX_HZ = 20000000,
#elif defined(Z80_CLOCK_20MHZ)
  Z80_CLOCK_MIN_HZ = 10,
  Z80_CLOCK_MAX_HZ = 20000000,
#else
  Z80_CLOCK_MIN_HZ = 10,
  Z80_CLOCK_MAX_HZ = 8000000,
#endif
};

// PIO builds accept only 20 MHz; PWM builds select the fastest stable 50%
// rate not exceeding hz.
bool z80_clock_set_hz(uint32_t hz);
uint32_t z80_clock_get_hz(void);
void z80_clock_stop(void);
#if !defined(Z80_BANKED_PIO)
// PWM builds only: restart the run clock after a trapped I/O cycle.
void z80_clock_resume(void);
#endif
// Core-0 bus owner only: stop the run clock and leave SIO CLK driven LOW.
void z80_clock_prepare_stepping(void);
// Requires preparation above and no intervening clock-owner changes.
// Return LOW after 1 us of settling; half_period_us must be > 0.
void z80_clock_step_prepared(uint32_t half_period_us);
// Return LOW after 1 us of settling; half_period_us must be > 0.
void z80_clock_one_cycle(uint32_t half_period_us);
void z80_reset_with_clock_cycles(unsigned int cycles,
                                 uint32_t half_period_us);

#endif