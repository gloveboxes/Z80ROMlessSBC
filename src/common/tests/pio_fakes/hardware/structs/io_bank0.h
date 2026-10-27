#ifndef TEST_IO_BANK0_H
#define TEST_IO_BANK0_H
#include <stdint.h>
typedef struct {
  struct { volatile uint32_t status, ctrl; } io[32];
} io_bank0_hw_t;
extern io_bank0_hw_t test_io_bank0;
#define io_bank0_hw (&test_io_bank0)
#endif
