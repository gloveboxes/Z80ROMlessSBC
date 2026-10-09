#ifndef Z80SBC_CLOCK_H
#define Z80SBC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

enum { Z80_CLOCK_MIN_HZ = 10, Z80_CLOCK_MAX_HZ = 8000000 };

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