/* Instrumented two-block PIO HAL for banked_pio.c host tests: PIO1 runs the
 * clock SM, PIO2 the I/O engine. Include after "../banked_pio.c"; the test
 * defines the GPIO fakes and may read the block state below. */
#ifndef TEST_PIO_BLOCKS_H
#define TEST_PIO_BLOCKS_H

#include <assert.h>
#include <string.h>

struct test_pio test_pio0, test_pio1;
io_bank0_hw_t test_io_bank0;

typedef struct {
  bool claimed[4], enabled[4], space, rx_irq;
  uint pcs[4], offset;
  pio_sm_config configs[4];
  uint32_t flags, pins, dirs;
  uint32_t rx[8], tx[4];
  uint rx_count, tx_count;
} fake_block_t;

static fake_block_t blocks[2] = {
    {.space = true, .offset = 1}, /* relocate both programs */
    {.space = true, .offset = 2},
};
static irq_handler_t io_irq;
static bool io_irq_enabled;
static unsigned io_inits;

static fake_block_t *block(PIO pio) {
  assert(pio == pio0 || pio == pio1);
  return &blocks[pio == pio1];
}
static void fold_irq_force(void) {
  blocks[0].flags |= test_pio0.irq_force;
  blocks[1].flags |= test_pio1.irq_force;
  test_pio0.irq_force = test_pio1.irq_force = 0;
}
/* Pad level of a pin owned by a PIO block, or -1 for SIO. */
static int pio_pad_level(uint pin) {
  uint function = io_bank0_hw->io[pin].ctrl;
  if (function == GPIO_FUNC_PIO0) return (int)(blocks[0].pins >> pin & 1);
  if (function == GPIO_FUNC_PIO1) return (int)(blocks[1].pins >> pin & 1);
  return -1;
}

irq_handler_t irq_get_exclusive_handler(uint irq) {
  assert(irq == PIO1_IRQ_0);
  return io_irq;
}
bool irq_has_shared_handler(uint irq) { assert(irq == PIO1_IRQ_0); return false; }
void irq_set_exclusive_handler(uint irq, irq_handler_t handler) {
  assert(irq == PIO1_IRQ_0 && io_irq == NULL);
  io_irq = handler;
}
void irq_set_enabled(uint irq, bool enable) {
  assert(irq == PIO1_IRQ_0);
  io_irq_enabled = enable;
}
bool pio_can_add_program(PIO pio, const struct pio_program *program) {
  assert(program == (pio == pio0 ? &banked_clock_program : &banked_io_program));
  assert(program->length <= 32);
  return block(pio)->space;
}
uint pio_add_program(PIO pio, const struct pio_program *program) {
  assert(pio_can_add_program(pio, program));
  return block(pio)->offset;
}
int pio_claim_unused_sm(PIO pio, bool required) {
  assert(!required);
  for (uint sm = 0; sm < 4; ++sm)
    if (!block(pio)->claimed[sm]) {
      block(pio)->claimed[sm] = true;
      return (int)sm;
    }
  return -1;
}
bool pio_sm_is_claimed(PIO pio, uint sm) { return block(pio)->claimed[sm]; }
void pio_sm_unclaim(PIO pio, uint sm) { block(pio)->claimed[sm] = false; }
void pio_gpio_init(PIO pio, uint pin) {
  fake_block_t *b = block(pio);
  /* PIO must already drive the level/direction SIO held: no glitch. */
  if (pio == pio1) {
    ++io_inits;
    assert(b->dirs >> pin & 1 ? (b->pins >> pin & 1) == gpio_get(pin) : true);
  }
  io_bank0_hw->io[pin].ctrl = pio == pio0 ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1;
}
uint pio_sm_get_pc(PIO pio, uint sm) { return block(pio)->pcs[sm]; }
void pio_sm_init(PIO pio, uint sm, uint pc, const pio_sm_config *config) {
  fake_block_t *b = block(pio);
  assert(!b->enabled[sm]);
  b->configs[sm] = *config;
  b->pcs[sm] = pc;
  b->rx_count = b->tx_count = 0;
}
void pio_sm_set_enabled(PIO pio, uint sm, bool enable) {
  fold_irq_force();
  block(pio)->enabled[sm] = enable;
}
void pio_sm_restart(PIO pio, uint sm) { assert(!block(pio)->enabled[sm]); }
void pio_sm_exec(PIO pio, uint sm, uint16_t instruction) {
  fake_block_t *b = block(pio);
  assert(!b->enabled[sm]);
  if ((instruction >> 13) == 0) {
    b->pcs[sm] = instruction & 31;
  } else {
    assert(pio == pio0 && (instruction >> 13) == 7); /* clock SET pins */
    b->pins = (b->pins & ~(1u << PIN_CLK)) | ((instruction & 1u) << PIN_CLK);
  }
}
void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t values, uint32_t mask) {
  fake_block_t *b = block(pio);
  assert(!b->enabled[sm]);
  b->pins = (b->pins & ~mask) | (values & mask);
}
void pio_sm_set_pindirs_with_mask(PIO pio, uint sm, uint32_t dirs, uint32_t mask) {
  fake_block_t *b = block(pio);
  assert(!b->enabled[sm]);
  b->dirs = (b->dirs & ~mask) | (dirs & mask);
}
void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint first, uint count, bool output) {
  assert(!block(pio)->enabled[sm] && output);
  block(pio)->dirs |= ((1u << count) - 1u) << first;
}
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) {
  assert(pio == pio1 && sm == (uint)io_sm);
  return blocks[1].rx_count == 0;
}
uint32_t pio_sm_get(PIO pio, uint sm) {
  assert(!pio_sm_is_rx_fifo_empty(pio, sm));
  uint32_t value = blocks[1].rx[0];
  memmove(blocks[1].rx, blocks[1].rx + 1, --blocks[1].rx_count * sizeof(uint32_t));
  return value;
}
void pio_sm_put(PIO pio, uint sm, uint32_t value) {
  assert(pio == pio1 && sm == (uint)io_sm && blocks[1].tx_count < 4);
  blocks[1].tx[blocks[1].tx_count++] = value;
}
bool pio_interrupt_get(PIO pio, uint index) {
  fold_irq_force();
  return block(pio)->flags >> index & 1;
}
void pio_interrupt_clear(PIO pio, uint index) {
  fold_irq_force();
  block(pio)->flags &= ~(1u << index);
}
void pio_set_irq0_source_enabled(PIO pio, uint source, bool enable) {
  assert(pio == pio1 && source == pis_sm0_rx_fifo_not_empty + (uint)io_sm);
  blocks[1].rx_irq = enable;
}

/* What the I/O SM autopushes: data, A0/A1/A2/A4, RD#, WR#. */
static uint32_t fake_snapshot(bool read, bool write, uint8_t port, uint8_t value) {
  uint32_t nibble = (port & 7u) | ((port >> 1) & 8u);
  return value | nibble << 8 | (uint32_t)!read << (PIN_RD_N - PIN_DATA_0) |
         (uint32_t)!write << (PIN_WR_N - PIN_DATA_0);
}
static void fake_push(uint32_t snapshot) {
  assert(blocks[1].rx_count < 4);
  blocks[1].rx[blocks[1].rx_count++] = snapshot;
}
static uint32_t fake_take_reply(void) {
  assert(blocks[1].tx_count > 0);
  uint32_t value = blocks[1].tx[0];
  memmove(blocks[1].tx, blocks[1].tx + 1, --blocks[1].tx_count * sizeof(uint32_t));
  return value;
}

#endif
