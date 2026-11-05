#ifndef TEST_PICO_STDLIB_H
#define TEST_PICO_STDLIB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint64_t absolute_time_t;
#define GPIO_IN 0
#define GPIO_OUT 1
#define GPIO_FUNC_SIO 0
#define GPIO_FUNC_SPI 1
#define GPIO_FUNC_I2C 2
void gpio_init(unsigned pin);
void gpio_put(unsigned pin, bool value);
void gpio_set_dir(unsigned pin, bool output);
void gpio_set_function(unsigned pin, unsigned function);
void sleep_ms(unsigned ms);
absolute_time_t make_timeout_time_ms(unsigned ms);
bool time_reached(absolute_time_t deadline);
void stdio_init_all(void);
int getchar_timeout_us(unsigned us);
void tight_loop_contents(void);
#endif
