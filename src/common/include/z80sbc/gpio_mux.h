#ifndef Z80SBC_GPIO_MUX_H
#define Z80SBC_GPIO_MUX_H

#include "hardware/structs/io_bank0.h"
#include "hardware/regs/io_bank0.h"

/* Only for CLK/GP7/GP9 after gpio_init/gpio_set_function initialized their
 * pads. Their IE/OD/ISO settings stay fixed. Like gpio_set_function, clear
 * overrides when selecting the peripheral, but avoid repeating pad setup
 * and a flash-resident SDK call on every I/O handoff. Callers retain the
 * preloaded output levels, isolation and clock guards around this write. */
static inline void z80_gpio_mux_select(unsigned pin, unsigned function) {
  io_bank0_hw->io[pin].ctrl = function << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
}

#endif
