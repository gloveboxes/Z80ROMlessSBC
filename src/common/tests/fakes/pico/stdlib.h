#ifndef TEST_PICO_STDLIB_H
#define TEST_PICO_STDLIB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico/types.h"
#define __not_in_flash_func(name) name
typedef uint64_t absolute_time_t;
static const absolute_time_t nil_time = 0;
enum { PICO_OK = 0 };
enum {
  GPIO_IN, GPIO_OUT, GPIO_FUNC_SPI, GPIO_IRQ_EDGE_FALL,
  GPIO_FUNC_SIO, GPIO_FUNC_PWM
};
void gpio_init(uint pin);
void gpio_put(uint pin, bool value);
bool gpio_get(uint pin);
uint32_t gpio_get_all(void);
void gpio_set_dir(uint pin, bool output);
static inline void gpio_put_masked(uint32_t mask, uint32_t value) {
  for (uint pin = 0; pin < 32; ++pin)
    if (mask & (1u << pin))
      gpio_put(pin, (value & (1u << pin)) != 0);
}
static inline void gpio_set_mask(uint32_t mask) {
  gpio_put_masked(mask, mask);
}
static inline void gpio_set_dir_masked(uint32_t mask, uint32_t value) {
  for (uint pin = 0; pin < 32; ++pin)
    if (mask & (1u << pin))
      gpio_set_dir(pin, (value & (1u << pin)) != 0);
}
void gpio_set_function(uint pin, uint function);
uint gpio_get_function(uint pin);
void gpio_disable_pulls(uint pin);
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enabled);
void gpio_acknowledge_irq(uint pin, uint32_t events);
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enabled,
                                       void (*callback)(uint, uint32_t));
void busy_wait_us_32(uint32_t delay);
void sleep_ms(uint32_t delay);
absolute_time_t get_absolute_time(void);
uint64_t time_us_64(void);
static inline uint32_t to_ms_since_boot(absolute_time_t time) {
  return (uint32_t)(time / 1000);
}
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to);
int putchar_raw(int value);
absolute_time_t make_timeout_time_us(uint32_t delay);
absolute_time_t make_timeout_time_ms(uint32_t delay);
bool time_reached(absolute_time_t deadline);
void tight_loop_contents(void);
void stdio_init_all(void);
uint get_core_num(void);
void alarm_pool_init_default(void);
int getchar_timeout_us(uint32_t timeout_us);
#endif