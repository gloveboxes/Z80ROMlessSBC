#ifndef TEST_HARDWARE_CLOCKS_H
#define TEST_HARDWARE_CLOCKS_H
#include <stdint.h>
#include "pico/types.h"
enum { clk_sys };
uint32_t clock_get_hz(uint clock);
void set_sys_clock_pll(uint32_t vco_freq, uint post_div1, uint post_div2);
#endif
