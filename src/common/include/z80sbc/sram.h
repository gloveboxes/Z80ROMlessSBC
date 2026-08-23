#ifndef Z80SBC_SRAM_H
#define Z80SBC_SRAM_H

#include <stdbool.h>
#include <stdint.h>

bool z80_sram_prepare_dma(void);
bool z80_sram_write_byte(uint16_t address, uint8_t value);
bool z80_sram_read_byte(uint16_t address, uint8_t *value);
bool z80_sram_load(uint16_t address, const uint8_t *data, uint32_t length);
bool z80_sram_verify(uint16_t address, const uint8_t *data, uint32_t length);
bool z80_sram_pattern_test(bool complement);
bool z80_sram_march_test(void);

#endif