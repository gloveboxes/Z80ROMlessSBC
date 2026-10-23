#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdint.h>
uint32_t tud_cdc_write_available(void);
uint32_t tud_cdc_write(const void *data, uint32_t length);
uint32_t tud_cdc_write_flush(void);
#endif
