#ifndef Z80SBC_FLASH_DISK_H
#define Z80SBC_FLASH_DISK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	Z80_FLASH_FAULT_NONE = 0,
	Z80_FLASH_FAULT_AFTER_JOURNAL_DATA,
	Z80_FLASH_FAULT_AFTER_JOURNAL_HEADER,
	Z80_FLASH_FAULT_AFTER_TARGET_ERASE,
	Z80_FLASH_FAULT_AFTER_PARTIAL_TARGET,
	Z80_FLASH_FAULT_AFTER_TARGET_VERIFY,
	Z80_FLASH_FAULT_AFTER_HEADER_CLEAR,
	Z80_FLASH_FAULT_SAFE_EXECUTE_ENTRY,
	Z80_FLASH_FAULT_SAFE_EXECUTE_EXIT,
} z80_flash_fault_point_t;

bool z80_flash_storage_init(void);
void z80_flash_core0_service(void);
void z80_flash_core1_service(void);
uint8_t z80_flash_disk_io_read(uint8_t port);
void z80_flash_disk_io_write(uint8_t port, uint8_t value);
uint32_t z80_flash_disk_status(void);
bool z80_flash_disk_has_fatal_error(void);
bool z80_flash_disk_quiescent(void);
void z80_flash_disk_arm_fault(z80_flash_fault_point_t point);

#endif