#ifndef TEST_HARDWARE_FLASH_H
#define TEST_HARDWARE_FLASH_H
#include <stddef.h>
#include <stdint.h>
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (4u * 1024u * 1024u)
#define PICO_FLASH_ASSUME_CORE1_SAFE 1
extern uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t)test_flash)
void flash_range_erase(uint32_t offset, size_t length);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length);
#endif
