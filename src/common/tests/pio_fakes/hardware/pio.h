#ifndef TEST_BANKED_PIO_H
#define TEST_BANKED_PIO_H
#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
enum {
  NUM_PIO_STATE_MACHINES = 4, pis_interrupt0 = 0,
  GPIO_FUNC_PIO0 = 98, GPIO_FUNC_PIO1 = 99,
};
enum pio_fifo_join { PIO_FIFO_JOIN_NONE = 0, PIO_FIFO_JOIN_RX = 2 };
enum { pis_sm0_rx_fifo_not_empty = 4 };
static inline uint pio_get_rx_fifo_not_empty_interrupt_source(uint sm) {
  return pis_sm0_rx_fifo_not_empty + sm;
}
typedef struct test_pio { volatile uint32_t irq_force; } *PIO;
extern struct test_pio test_pio0, test_pio1;
#define pio0 (&test_pio0)
#define pio1 (&test_pio1)
struct pio_program {
  const uint16_t *instructions;
  uint length;
  int origin;
  uint pio_version;
};
typedef struct {
  uint wrap_bottom, wrap_top, set_base, set_count, out_base, out_count;
  uint in_base, side_base, side_count, jmp_pin;
  bool right_shift, in_right_shift, autopush;
  uint push_threshold, fifo_join;
  float divider;
} pio_sm_config;
static inline pio_sm_config pio_get_default_sm_config(void) {
  return (pio_sm_config){.divider = 1};
}
static inline void sm_config_set_wrap(pio_sm_config *c, uint first, uint last) {
  c->wrap_bottom = first; c->wrap_top = last;
}
static inline void sm_config_set_sideset(pio_sm_config *c, uint bits, bool optional, bool dirs) {
  (void)optional; (void)dirs; c->side_count = bits;
}
static inline void sm_config_set_set_pins(pio_sm_config *c, uint pin, uint count) {
  c->set_base = pin; c->set_count = count;
}
static inline void sm_config_set_out_pins(pio_sm_config *c, uint pin, uint count) {
  c->out_base = pin; c->out_count = count;
}
static inline void sm_config_set_in_pins(pio_sm_config *c, uint pin) { c->in_base = pin; }
static inline void sm_config_set_sideset_pins(pio_sm_config *c, uint pin) { c->side_base = pin; }
static inline void sm_config_set_jmp_pin(pio_sm_config *c, uint pin) { c->jmp_pin = pin; }
static inline void sm_config_set_clkdiv(pio_sm_config *c, float divider) { c->divider = divider; }
static inline void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush, uint threshold) {
  c->in_right_shift = right; c->autopush = autopush; c->push_threshold = threshold;
}
static inline void sm_config_set_fifo_join(pio_sm_config *c, enum pio_fifo_join join) {
  c->fifo_join = join;
}
static inline void sm_config_set_out_shift(pio_sm_config *c, bool right, bool autopull, uint threshold) {
  (void)autopull; (void)threshold; c->right_shift = right;
}
static inline uint16_t pio_encode_jmp(uint target) { return (uint16_t)target; }
static inline uint16_t pio_encode_nop(void) { return 0xA042; }
enum { pio_pins = 0 };
static inline uint16_t pio_encode_set(uint dest, uint value) {
  return (uint16_t)(0xE000u | (dest << 5) | value);
}
static inline uint16_t pio_encode_sideset_opt(uint bits, uint value) {
  return (uint16_t)(0x1000u | (value << (12u - bits)));
}
bool pio_can_add_program(PIO, const struct pio_program *);
int pio_claim_unused_sm(PIO, bool);
bool pio_sm_is_claimed(PIO, uint);
void pio_sm_unclaim(PIO, uint);
uint pio_add_program(PIO, const struct pio_program *);
void pio_gpio_init(PIO, uint);
uint pio_sm_get_pc(PIO, uint);
void pio_sm_init(PIO, uint, uint, const pio_sm_config *);
void pio_sm_set_enabled(PIO, uint, bool);
void pio_sm_restart(PIO, uint);
void pio_sm_exec(PIO, uint, uint16_t);
void pio_sm_set_pins_with_mask(PIO, uint, uint32_t, uint32_t);
void pio_sm_set_consecutive_pindirs(PIO, uint, uint, uint, bool);
void pio_sm_set_pindirs_with_mask(PIO, uint, uint32_t, uint32_t);
void pio_sm_put(PIO, uint, uint32_t);
void pio_sm_clear_fifos(PIO, uint);
bool pio_sm_is_rx_fifo_empty(PIO, uint);
uint32_t pio_sm_get(PIO, uint);
bool pio_interrupt_get(PIO, uint);
void pio_interrupt_clear(PIO, uint);
void pio_set_irq0_source_enabled(PIO, uint, bool);
#endif
