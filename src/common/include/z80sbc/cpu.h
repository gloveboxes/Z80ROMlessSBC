#ifndef Z80SBC_CPU_H
#define Z80SBC_CPU_H

#include <stdbool.h>
#include <stdint.h>

bool z80_cpu_prepare_reset_dma(void);
bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length);
bool z80_cpu_release_reset_and_run(uint32_t clock_hz);
bool z80_cpu_request_bus(uint32_t timeout_us);
bool z80_cpu_release_bus(uint32_t timeout_us);
void z80_cpu_fail_closed(void);

#endif