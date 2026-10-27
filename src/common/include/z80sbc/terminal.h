#ifndef Z80SBC_TERMINAL_H
#define Z80SBC_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t rx_used;
  uint32_t rx_capacity;
  uint32_t rx_peak;
  uint32_t tx_used;
  uint32_t tx_capacity;
  uint32_t tx_peak;
} z80_terminal_buffer_stats_t;

void z80_terminal_queue_init(void);
void z80_terminal_set_connected(bool connected);
uint8_t z80_terminal_io_read(uint8_t port);
void z80_terminal_io_write(uint8_t port, uint8_t value);

void z80_terminal_receive(const uint8_t *data, size_t length);
/* Core 1 must consume accepted bytes before servicing ownership changes. */
size_t z80_terminal_peek(uint8_t *data, size_t capacity);
size_t z80_terminal_supply(uint8_t *data, size_t capacity);
bool z80_terminal_output_pending(void);
bool z80_terminal_browser_try_receive(const uint8_t *data, size_t length);
void z80_terminal_browser_connect(void);
void z80_terminal_browser_disconnect(void);

uint32_t z80_terminal_rx_drop_count(void);
uint32_t z80_terminal_tx_drop_count(void);
z80_terminal_buffer_stats_t z80_terminal_get_buffer_stats(void);
bool z80_terminal_client_connected(void);
bool z80_terminal_browser_connected(void);
void z80_terminal_usb_connected(bool connected);
void z80_terminal_usb_receive(uint8_t value);
size_t z80_terminal_usb_supply(uint8_t *data, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
