#ifndef TEST_PIO_IRQ_H
#define TEST_PIO_IRQ_H
#include <stdbool.h>
#include "pico/types.h"
enum { PIO1_IRQ_0 = 9 };
typedef void (*irq_handler_t)(void);
irq_handler_t irq_get_exclusive_handler(uint irq);
bool irq_has_shared_handler(uint irq);
void irq_set_exclusive_handler(uint irq, irq_handler_t handler);
void irq_set_enabled(uint irq, bool enabled);
#endif
