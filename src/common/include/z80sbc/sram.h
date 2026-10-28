#ifndef Z80SBC_SRAM_H
#define Z80SBC_SRAM_H

#include <stdbool.h>
#include <stdint.h>

enum {
  Z80_SRAM_BANK_RUNTIME_BYTES = 0x800,
  Z80_SRAM_BANK_RESIDENT_BYTES = 256,
  Z80_SRAM_BANK_INIT_BYTES = Z80_SRAM_BANK_RUNTIME_BYTES + Z80_SRAM_BANK_RESIDENT_BYTES,
};

bool z80_sram_prepare_loader(void);
/* Loader session required. Probe A16 isolation, initialize bank 1 extent tables, allocator and private stack,
 * and mirror the 256-byte resident page from the validated boot image.
 * Logs separate load/readback passes over 2304 bytes; unavailable or failed
 * bank 1 remains disabled, and every exit restores bank 0. */
void z80_sram_prepare_banked_memory(const uint8_t *resident_page);
bool z80_sram_write_byte(uint16_t address, uint8_t value);
bool z80_sram_read_byte(uint16_t address, uint8_t *value);
bool z80_sram_load(uint16_t address, const uint8_t *data, uint32_t length);
bool z80_sram_verify(uint16_t address, const uint8_t *data, uint32_t length);
bool z80_sram_pattern_test(bool complement);
bool z80_sram_march_test(void);

#endif
