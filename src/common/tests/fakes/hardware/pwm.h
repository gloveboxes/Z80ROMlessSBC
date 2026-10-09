#ifndef TEST_HARDWARE_PWM_H
#define TEST_HARDWARE_PWM_H
#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
uint pwm_gpio_to_slice_num(uint pin);
uint pwm_gpio_to_channel(uint pin);
void pwm_set_enabled(uint slice, bool enabled);
void pwm_set_clkdiv_int_frac4(uint slice, uint8_t divider, uint8_t fraction);
void pwm_set_wrap(uint slice, uint16_t wrap);
void pwm_set_chan_level(uint slice, uint channel, uint16_t level);
void pwm_set_counter(uint slice, uint16_t counter);
#endif
