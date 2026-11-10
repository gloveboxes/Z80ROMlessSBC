#ifndef Z80SBC_CPU_H
#define Z80SBC_CPU_H

#include <stdbool.h>
#include <stdint.h>

bool z80_cpu_prepare_loader(void);
bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length);
bool z80_cpu_release_reset_and_run(uint32_t clock_hz);
// Called repeatedly while waiting for BUSACK so the waiting core can still
// complete a Z80 cycle that is held for a deferred IN reply.
void z80_cpu_set_bus_wait_hook(void (*hook)(void));
bool z80_cpu_request_bus(uint32_t timeout_us);
bool z80_cpu_release_bus(uint32_t timeout_us);
void z80_cpu_fail_closed(void);

#endif