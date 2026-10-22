#ifndef Z80SBC_WEATHER_HTTP_H
#define Z80SBC_WEATHER_HTTP_H

#include <stdbool.h>
#include <stddef.h>

/* Decodes a complete, NUL-terminated response in place, including chunking. */
bool z80_weather_http_decode(char *wire, size_t length, unsigned *status,
                             const char **body, size_t *body_length);
void z80_weather_network_poll(void);

#endif
