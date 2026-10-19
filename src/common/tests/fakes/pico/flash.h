#ifndef TEST_PICO_FLASH_H
#define TEST_PICO_FLASH_H
#include <stdbool.h>
#include <stdint.h>
#define PICO_ERROR_TIMEOUT (-1)
bool flash_safe_execute_core_init(void);
int flash_safe_execute(void (*callback)(void *), void *parameter,
                        uint32_t timeout_ms);
#endif
