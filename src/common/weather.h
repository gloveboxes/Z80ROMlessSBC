#ifndef Z80SBC_WEATHER_H
#define Z80SBC_WEATHER_H

#include "io_service.h"

enum {
  Z80_WEATHER_NONE, Z80_WEATHER_FETCHING, Z80_WEATHER_READY, Z80_WEATHER_ERROR,
  Z80_WEATHER_BODY_MAX = 8192,
};

/* All calls and callbacks run on core 1, outside the Z80 trap. */
void z80_weather_init(bool (*start)(const char *path), void (*cancel)(void));
void z80_weather_poll(bool online);
void z80_weather_result(unsigned http_status, const char *body, size_t length,
                        const char *error);
io_service_error_t z80_weather_service(const uint8_t *request, size_t length,
                                      io_service_result_t *result);

#endif
