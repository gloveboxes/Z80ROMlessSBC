#ifndef Z80SBC_MCP23S17_H
#define Z80SBC_MCP23S17_H

#include <stdbool.h>
#include <stdint.h>

bool mcp23s17_init(uint32_t spi_hz);
bool mcp23s17_write_register(uint8_t reg, uint8_t value);
bool mcp23s17_read_register(uint8_t reg, uint8_t *value);
bool mcp23s17_set_directions(uint8_t port_a, uint8_t port_b);
bool mcp23s17_write_ports(uint8_t port_a, uint8_t port_b);
bool mcp23s17_read_ports(uint8_t *port_a, uint8_t *port_b);
bool mcp23s17_register_test(void);

#endif