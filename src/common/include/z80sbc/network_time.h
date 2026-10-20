#ifndef Z80SBC_NETWORK_TIME_H
#define Z80SBC_NETWORK_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void z80_io_ports_set_unix_time(uint32_t seconds);
#ifdef __cplusplus
}
#endif

#endif
