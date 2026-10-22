#ifndef TEST_PICO_MULTICORE_H
#define TEST_PICO_MULTICORE_H
#include <stddef.h>
#include <stdint.h>
void multicore_launch_core1(void (*entry)(void));
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack,
                                      size_t stack_size);
#endif
