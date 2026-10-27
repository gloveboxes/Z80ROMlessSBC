#ifndef TEST_MATRIX_PIO_H
#define TEST_MATRIX_PIO_H

#include <cstdint>
#include "pico/types.h"

struct test_pio;
using PIO = test_pio *;
extern PIO pio0;

struct pio_program {
  const uint16_t *instructions;
  uint length;
  int origin;
  uint pio_version;
};

struct pio_sm_config {
  uint wrap_target = 0;
  uint wrap = 0;
  uint scl = 0;
  uint sda = 0;
  bool out_right = true;
  bool in_right = true;
  float divider = 1;
};

inline pio_sm_config pio_get_default_sm_config() { return {}; }
inline void sm_config_set_wrap(pio_sm_config *c, uint first, uint last) {
  c->wrap_target = first;
  c->wrap = last;
}
inline void sm_config_set_sideset(pio_sm_config *, uint bits, bool optional,
                                  bool pindirs) {
  if (bits != 2 || !optional || !pindirs)
    __builtin_trap();
}
inline void sm_config_set_sideset_pins(pio_sm_config *c, uint pin) {
  c->scl = pin;
}
inline void sm_config_set_out_pins(pio_sm_config *c, uint pin, uint count) {
  if (count != 1)
    __builtin_trap();
  c->sda = pin;
}
inline void sm_config_set_set_pins(pio_sm_config *c, uint pin, uint count) {
  if (count != 1 || c->sda != pin)
    __builtin_trap();
}
inline void sm_config_set_in_pins(pio_sm_config *c, uint pin) {
  if (c->sda != pin)
    __builtin_trap();
}
inline void sm_config_set_out_shift(pio_sm_config *c, bool right, bool automatic,
                                    uint threshold) {
  if (automatic || threshold != 32)
    __builtin_trap();
  c->out_right = right;
}
inline void sm_config_set_in_shift(pio_sm_config *c, bool right, bool automatic,
                                   uint threshold) {
  if (automatic || threshold != 32)
    __builtin_trap();
  c->in_right = right;
}
inline void sm_config_set_clkdiv(pio_sm_config *c, float divider) {
  c->divider = divider;
}

uint pio_get_dreq(PIO, uint, bool);

bool pio_can_add_program(PIO, const pio_program *);
int pio_claim_unused_sm(PIO, bool);
uint pio_add_program(PIO, const pio_program *);
void pio_gpio_init(PIO, uint);
void pio_sm_init(PIO, uint, uint, const pio_sm_config *);
void pio_sm_set_pins_with_mask(PIO, uint, uint32_t, uint32_t);
void pio_sm_set_consecutive_pindirs(PIO, uint, uint, uint, bool);
void pio_sm_set_enabled(PIO, uint, bool);
bool pio_sm_is_tx_fifo_full(PIO, uint);
bool pio_sm_is_rx_fifo_empty(PIO, uint);
void pio_sm_put(PIO, uint, uint32_t);
uint32_t pio_sm_get(PIO, uint);
void pio_sm_unclaim(PIO, uint);
void pio_remove_program(PIO, const pio_program *, uint);

#endif
