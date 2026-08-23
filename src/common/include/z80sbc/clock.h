#ifndef Z80SBC_CLOCK_H
#define Z80SBC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool z80_clock_set_hz(uint32_t hz);
void z80_clock_stop(void);
void z80_clock_resume(void);
void z80_clock_one_cycle(uint32_t half_period_us);
void z80_reset_with_clock_cycles(unsigned int cycles,
                                 uint32_t half_period_us);

#endif