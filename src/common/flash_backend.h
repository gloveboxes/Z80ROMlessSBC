#ifndef Z80SBC_FLASH_BACKEND_H
#define Z80SBC_FLASH_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool z80_flash_backend_init(void);
void z80_flash_backend_core0_service(void);
bool z80_flash_backend_write_record(unsigned int drive, uint16_t lba,
                                    const uint8_t *data, size_t length);

#endif