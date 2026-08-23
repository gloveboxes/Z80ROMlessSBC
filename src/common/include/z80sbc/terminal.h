#ifndef Z80SBC_TERMINAL_H
#define Z80SBC_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool z80_terminal_init(void);
void z80_terminal_core1_service(void);
uint8_t z80_terminal_io_read(uint8_t port);
void z80_terminal_io_write(uint8_t port, uint8_t value);

void z80_terminal_network_receive(const uint8_t *data, size_t length);
size_t z80_terminal_network_supply(uint8_t *data, size_t capacity);
void z80_terminal_network_connected(void);
void z80_terminal_network_disconnected(void);
void z80_terminal_network_tx_dropped(size_t count);

uint32_t z80_terminal_rx_drop_count(void);
uint32_t z80_terminal_tx_drop_count(void);
bool z80_terminal_client_connected(void);

#ifdef __cplusplus
}
#endif

#endif