#ifndef Z80SBC_IO_SERVICE_H
#define Z80SBC_IO_SERVICE_H

#include "z80sbc/io_ports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t generation;
  io_service_error_t error;
  bool has_response;
  size_t length;
  uint8_t data[IO_SERVICE_MAX_PAYLOAD + 1];
} io_service_result_t;

void z80_io_service_init(void);
io_service_error_t z80_io_service_submit(uint8_t command, const uint8_t *data,
                                        size_t length, uint32_t generation);
const io_service_result_t *z80_io_service_poll(void);
uint64_t z80_service_millis(void);
uint16_t z80_service_random(void);
size_t z80_service_info(uint8_t selector, char *text, size_t capacity);
/* Returns false until calendar time is synchronized. */
bool z80_service_clock(uint8_t operation, char *text, size_t capacity);
/* Core 1 only. Reads/writes the LED, or reports an unavailable backend. */
io_service_error_t z80_service_led(bool write, bool *state);
/* Core 1 only. Shared initialization for LED-only and network builds. */
bool z80_wireless_init(void);

#ifdef __cplusplus
}
#endif

#endif
