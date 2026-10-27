#ifndef Z80SBC_SUPERVISOR_H
#define Z80SBC_SUPERVISOR_H

#include <stdbool.h>
#include <stddef.h>
#include "pico/types.h"

void output_with_initial_level(uint pin, bool level);
void input_with_no_pull(uint pin);
void z80_safe_startup(void);
/* Core 0 only. Change banks only in reset, the loader, or a held I/O cycle. */
void z80_memory_bank_select(unsigned bank);
typedef void (*z80_memory_bank_driver_t)(unsigned bank);
void z80_memory_bank_set_driver(z80_memory_bank_driver_t driver);
void z80_memory_bank_set_available(bool available);
unsigned z80_memory_bank_status(void);
void z80_isolate_buses(void);
void z80_walking_output_test(const uint *pins, size_t count,
                             uint32_t dwell_ms);

#endif
