#ifndef Z80SBC_FLASH_DISK_H
#define Z80SBC_FLASH_DISK_H

#include <stdbool.h>
#include <stdint.h>

bool z80_flash_storage_init(void);
void z80_flash_core0_service(void);
void z80_flash_core1_service(void);
uint8_t z80_flash_disk_io_read(uint8_t port);
void z80_flash_disk_io_write(uint8_t port, uint8_t value);
uint32_t z80_flash_disk_status(void);
bool z80_flash_disk_has_fatal_error(void);

#endif