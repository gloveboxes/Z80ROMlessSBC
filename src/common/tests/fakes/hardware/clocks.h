#ifndef TEST_HARDWARE_CLOCKS_H
#define TEST_HARDWARE_CLOCKS_H
#include <stdint.h>
#include "pico/types.h"
enum { clk_sys };
uint32_t clock_get_hz(uint clock);
#endif
