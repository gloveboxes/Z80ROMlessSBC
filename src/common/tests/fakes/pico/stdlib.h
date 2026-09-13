#ifndef TEST_PICO_STDLIB_H
#define TEST_PICO_STDLIB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico/types.h"
typedef uint64_t absolute_time_t;
static const absolute_time_t nil_time = 0;
enum { PICO_OK = 0 };
enum { GPIO_IN, GPIO_OUT, GPIO_FUNC_SPI, GPIO_IRQ_EDGE_FALL };
void gpio_init(uint pin);
void gpio_put(uint pin, bool value);
bool gpio_get(uint pin);
uint32_t gpio_get_all(void);
void gpio_set_dir(uint pin, bool output);
void gpio_set_function(uint pin, uint function);
void gpio_disable_pulls(uint pin);
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enabled);
void gpio_acknowledge_irq(uint pin, uint32_t events);
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enabled,
                                       void (*callback)(uint, uint32_t));
void busy_wait_us_32(uint32_t delay);
void sleep_ms(uint32_t delay);
absolute_time_t make_timeout_time_us(uint32_t delay);
absolute_time_t make_timeout_time_ms(uint32_t delay);
bool time_reached(absolute_time_t deadline);
void tight_loop_contents(void);
#endif