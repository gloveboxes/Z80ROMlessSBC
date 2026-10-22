#ifndef Z80SBC_NETWORK_TIME_H
#define Z80SBC_NETWORK_TIME_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
void z80_io_ports_set_unix_time(uint32_t seconds);
/* UTC for calendar services and TLS validity checks; zero before SNTP sync. */
time_t z80_network_time(time_t *result);
#ifdef __cplusplus
}
#endif

#endif
