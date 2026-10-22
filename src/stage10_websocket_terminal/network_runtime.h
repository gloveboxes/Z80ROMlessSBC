#ifndef Z80_NETWORK_RUNTIME_H
#define Z80_NETWORK_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool z80_network_poll(void);
uint32_t z80_network_generation(void);
void z80_network_core1_service(void);

#ifdef __cplusplus
}
#endif
#endif
