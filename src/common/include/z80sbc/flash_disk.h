#ifndef Z80SBC_FLASH_DISK_H
#define Z80SBC_FLASH_DISK_H

#include <stdbool.h>
#include <stdint.h>

enum {
  Z80_BOOT_REPORT_PORT = 0x05,
  Z80_BOOT_REPORT_BIOS = 0xB0,
  Z80_BOOT_REPORT_CCP = 0xC0,
};

typedef enum {
  Z80_BOOT_RECOVERY,
  Z80_BOOT_MANIFEST,
  Z80_BOOT_LOAD,
  Z80_BOOT_VERIFY,
  Z80_BOOT_VERIFIED,
} z80_boot_phase_t;

typedef enum {
  Z80_BOOT_ERROR_NONE,
  Z80_BOOT_ERROR_JOURNAL,
  Z80_BOOT_ERROR_MANIFEST,
  Z80_BOOT_ERROR_PAYLOAD_CRC,
  Z80_BOOT_ERROR_LOADER,
  Z80_BOOT_ERROR_WRITE,
  Z80_BOOT_ERROR_READ,
  Z80_BOOT_ERROR_MISMATCH,
  Z80_BOOT_ERROR_FLASH_INIT,
  Z80_BOOT_ERROR_CANCELLED,
} z80_boot_error_t;

typedef struct {
  z80_boot_phase_t phase;
  z80_boot_error_t error;
  uint32_t completed;
  uint32_t total;
  uint16_t address;
  uint8_t expected;
  uint8_t actual;
} z80_boot_status_t;

typedef bool (*z80_boot_observer_t)(const z80_boot_status_t *status,
                                   void *context);

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
bool z80_flash_storage_init_observed(z80_boot_observer_t observer,
                                     void *context);
const z80_boot_status_t *z80_flash_boot_status(void);
void z80_flash_core0_service(void);
void z80_flash_core1_service(void);
uint8_t z80_flash_disk_io_read(uint8_t port);
void z80_flash_disk_io_write(uint8_t port, uint8_t value);
uint32_t z80_flash_disk_status(void);
bool z80_flash_disk_has_fatal_error(void);
bool z80_flash_disk_quiescent(void);
/* Core 0 closes this gate before publishing a confirmed reboot request. */
void z80_flash_disk_set_write_enabled(bool enabled);
void z80_flash_disk_arm_fault(z80_flash_fault_point_t point);

#endif
