#ifndef TEST_HARDWARE_I2C_H
#define TEST_HARDWARE_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define i2c0 ((void *)0)
unsigned i2c_init(void *i2c, unsigned baud);
int i2c_write_timeout_us(void *i2c, uint8_t address, const uint8_t *bytes,
                         size_t size, bool nostop, unsigned timeout_us);
#endif
