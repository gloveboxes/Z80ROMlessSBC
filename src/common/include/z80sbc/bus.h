#ifndef Z80SBC_BUS_H
#define Z80SBC_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"

enum { Z80_PORT_ADDRESS_MASK = 0x17 };

/* Port GPIOs A0/A1/A2/A4 are contiguous; bits holds them in bits 0-3. */
static inline uint8_t z80_port_decode(uint32_t bits) {
  return (uint8_t)((bits & 7u) | ((bits & 8u) << 1));
}

uint8_t z80_port_bus_sample(void);
void z80_data_bus_drive(uint8_t value);
void z80_data_bus_prepare_input(void);
uint8_t z80_data_bus_sample(void);
void z80_data_bus_isolate(void);

#endif