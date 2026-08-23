#ifndef Z80SBC_BUS_H
#define Z80SBC_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"

void z80_set_transceiver(uint oe_n, uint dir, bool direction);
bool z80_address_bus_drive(uint16_t address);
bool z80_address_bus_prepare_input(void);
bool z80_address_bus_sample(uint16_t *address);
void z80_address_bus_isolate(void);
void z80_data_bus_drive(uint8_t value);
void z80_data_bus_prepare_input(void);
uint8_t z80_data_bus_sample(void);
void z80_data_bus_isolate(void);

#endif