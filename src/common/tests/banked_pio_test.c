/* Execute the real C ownership/clock driver against an instrumented PIO HAL.
 * The separately registered Python test executes the assembled PIO words. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../banked_pio.c"

struct test_pio test_pio1;
io_bank0_hw_t test_io_bank0;
static bool levels[32], outputs[32], claimed[4], enabled[4];
static uint pcs[4], pin_latches[4];
static pio_sm_config configs[4];
static uint32_t irq_flags, system_hz = 120000000;
static uint64_t now;
static bool space_available = true, irq_enabled, irq_source, bank_available = true;
static bool interrupts_disabled;
static uint tokens, calls;
static irq_handler_t irq_handler;
static z80_memory_bank_driver_t bank_driver;

static void fold_irq_force(void) {
  irq_flags |= test_pio1.irq_force;
  test_pio1.irq_force = 0;
}
uint32_t clock_get_hz(uint clock) { (void)clock; return system_hz; }
void gpio_put(uint pin, bool value) { levels[pin] = value; }
bool gpio_get(uint pin) { return levels[pin]; }
uint32_t gpio_get_all(void) {
  uint32_t value = 0;
  for (uint pin = 0; pin < 32; ++pin) value |= (uint32_t)levels[pin] << pin;
  return value;
}
void gpio_set_function(uint pin, uint function) { io_bank0_hw->io[pin].ctrl = function; }
uint gpio_get_function(uint pin) { return io_bank0_hw->io[pin].ctrl; }
void gpio_set_dir(uint pin, bool output) { outputs[pin] = output; }
void busy_wait_us_32(uint32_t delay) { now += delay; }
uint64_t time_us_64(void) { return now; }
uint32_t save_and_disable_interrupts(void) {
  uint32_t previous = interrupts_disabled;
  interrupts_disabled = true;
  return previous;
}
void restore_interrupts(uint32_t state) { interrupts_disabled = state; }
void z80_memory_bank_set_driver(z80_memory_bank_driver_t driver) { bank_driver = driver; }
unsigned z80_memory_bank_status(void) { return bank_available ? 0xB4 | levels[8] : 0; }
void z80_clock_stop(void) { z80_banked_pio_clock_stop(); }
void z80_clock_prepare_stepping(void) {
  z80_clock_stop();
  busy_wait_us_32(1);
  levels[PIN_CLK] = false;
  io_bank0_hw->io[PIN_CLK].ctrl = GPIO_FUNC_SIO;
}
irq_handler_t irq_get_exclusive_handler(uint irq) { assert(irq == PIO1_IRQ_0); return irq_handler; }
bool irq_has_shared_handler(uint irq) { assert(irq == PIO1_IRQ_0); return false; }
void irq_set_exclusive_handler(uint irq, irq_handler_t handler) {
  assert(irq == PIO1_IRQ_0); irq_handler = handler;
}
void irq_set_enabled(uint irq, bool enable) { assert(irq == PIO1_IRQ_0); irq_enabled = enable; }
bool pio_can_add_program(PIO pio, const struct pio_program *p) {
  assert(pio == pio1 && p->length == 31); return space_available;
}
int pio_claim_unused_sm(PIO pio, bool required) {
  assert(pio == pio1 && !required);
  for (uint sm = 0; sm < 4; ++sm) if (!claimed[sm]) { claimed[sm] = true; return (int)sm; }
  return -1;
}
bool pio_sm_is_claimed(PIO pio, uint sm) { assert(pio == pio1); return claimed[sm]; }
void pio_sm_unclaim(PIO pio, uint sm) { assert(pio == pio1); claimed[sm] = false; }
uint pio_add_program(PIO pio, const struct pio_program *p) {
  assert(pio == pio1 && p->length == 31); return 1; /* exercise relocated labels */
}
void pio_gpio_init(PIO pio, uint pin) {
  assert(pio == pio1);
  io_bank0_hw->io[pin].ctrl = 99;
  if (pin == PIN_CLK) assert(!(pin_latches[clock_sm] & (1u << pin)));
  if (pin == PIN_DATA_DOWN_OE_N || pin == PIN_IO_RELEASE)
    for (uint data = 10; data <= 17; ++data) assert(!outputs[data]);
}
uint pio_sm_get_pc(PIO pio, uint sm) { assert(pio == pio1); return pcs[sm]; }
void pio_sm_init(PIO pio, uint sm, uint pc, const pio_sm_config *config) {
  assert(pio == pio1 && !enabled[sm]); configs[sm] = *config; pcs[sm] = pc;
}
void pio_sm_set_enabled(PIO pio, uint sm, bool enable) {
  assert(pio == pio1); fold_irq_force(); enabled[sm] = enable;
}
void pio_sm_restart(PIO pio, uint sm) { assert(pio == pio1 && !enabled[sm]); }
void pio_sm_exec(PIO pio, uint sm, uint16_t instruction) {
  assert(pio == pio1 && !enabled[sm]);
  if ((instruction >> 13) == 0) {
    pcs[sm] = instruction & 31;
  } else if ((instruction >> 13) == 7) {
    assert((instruction >> 13) == 7 && ((instruction >> 5) & 7) == pio_pins);
    uint32_t mask = ((1u << configs[sm].set_count) - 1) << configs[sm].set_base;
    pin_latches[sm] = (pin_latches[sm] & ~mask) |
                     (((uint32_t)instruction & 31) << configs[sm].set_base);
  } else {
    assert((instruction & 0xE0FF) == pio_encode_nop());
  }
  if (configs[sm].side_count && (instruction & 0x1000)) {
    uint32_t side_mask = 1u << configs[sm].side_base;
    pin_latches[sm] = (pin_latches[sm] & ~side_mask) |
                     ((instruction & 0x0800) ? side_mask : 0);
  }
}
void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t values, uint32_t mask) {
  assert(pio == pio1 && !enabled[sm]);
  pin_latches[sm] = (pin_latches[sm] & ~mask) | (values & mask);
  if (mask & (1u << PIN_MEMORY_BANK)) levels[PIN_MEMORY_BANK] = values & (1u << PIN_MEMORY_BANK);
}
void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint first, uint count, bool output) {
  assert(pio == pio1 && !enabled[sm]); (void)first; (void)count; assert(output);
}
void pio_sm_clear_fifos(PIO pio, uint sm) { assert(pio == pio1 && sm == (uint)io_sm); tokens = 0; }
bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { assert(pio == pio1 && sm == (uint)io_sm); return tokens == 0; }
uint32_t pio_sm_get(PIO pio, uint sm) { assert(!pio_sm_is_rx_fifo_empty(pio, sm)); --tokens; return 0; }
bool pio_interrupt_get(PIO pio, uint index) {
  assert(pio == pio1); fold_irq_force(); return (irq_flags >> index) & 1;
}
void pio_interrupt_clear(PIO pio, uint index) {
  assert(pio == pio1); fold_irq_force(); irq_flags &= ~(1u << index);
}
void pio_set_irq0_source_enabled(PIO pio, uint source, bool enable) {
  assert(pio == pio1 && source == pis_interrupt0); irq_source = enable;
}

static void slow_service(void) {
  ++calls;
  assert(in_slow && !clock_running && !enabled[io_sm]);
  assert(io_bank0_hw->io[7].ctrl == GPIO_FUNC_SIO && io_bank0_hw->io[9].ctrl == GPIO_FUNC_SIO);
  /* Simulate an IN reply leaving Pico data outputs behind. Completion must
   * turn them back into inputs before PIO can enable the downward buffer. */
  for (uint pin = 10; pin <= 17; ++pin) outputs[pin] = true;
  gpio_put(PIN_DATA_UP_OE_N, 0);
  gpio_put(PIN_IO_RELEASE, 1);
  bank_driver(1); /* valid OUT7 can also arrive via conservative slow routing */
  assert(in_slow && !enabled[io_sm]);
  gpio_put(PIN_DATA_UP_OE_N, 1);
  gpio_put(PIN_DATA_DOWN_OE_N, 1);
  busy_wait_us_32(1);
  gpio_put(PIN_IO_RELEASE, 0);
  z80_banked_pio_slow_complete();
  assert(!pio_interrupt_get(pio1, SLOW_IRQ));
  assert(enabled[io_sm] && pcs[io_sm] == program_offset + banked_io_offset_io_start);
  z80_banked_pio_clock_resume();
}

int main(void) {
  memset(levels, 1, sizeof(levels));
  for (uint pin = 10; pin <= 17; ++pin) outputs[pin] = true;
  space_available = false;
  assert(!z80_banked_pio_rearm(slow_service) && !allocated);
  space_available = true;
  system_hz = 144000000;
  assert(!z80_banked_pio_rearm(slow_service) && !allocated);
  system_hz = 120000000;
  claimed[3] = true;
  assert(!z80_banked_pio_rearm(slow_service) && !allocated);
  claimed[3] = false;
  bank_available = false;
  assert(!z80_banked_pio_rearm(slow_service));
  bank_available = true;
  assert(z80_banked_pio_rearm(slow_service));
  assert(allocated && io_active && irq_enabled && irq_source && bank_driver);
  assert(configs[clock_sm].wrap_bottom == 1 && configs[clock_sm].wrap_top == 4);
  assert(configs[clock_sm].set_base == PIN_CLK && configs[clock_sm].side_count == 0);
  assert(configs[io_sm].in_base == 10 && configs[io_sm].out_base == 8);
  assert(configs[io_sm].set_base == 7 && configs[io_sm].side_base == 9);
  assert(configs[io_sm].jmp_pin == 0 && configs[io_sm].right_shift);
  assert(!z80_banked_pio_clock_start(19000000));
  assert(z80_banked_pio_clock_start(20000000) && clock_running);
  assert(!pio_interrupt_get(pio1, CPU_HOLD_IRQ));
  bank_driver(0);
  assert(!levels[8] && pcs[io_sm] == 5);
  irq_flags |= 1u << SLOW_IRQ;
  irq_handler();
  assert(calls == 1 && clock_running && levels[8]);
  assert(z80_banked_pio_poll());
  levels[9] = true;
  assert(z80_banked_pio_poll());
  now += 499999;
  assert(z80_banked_pio_poll());
  now += 3;
  assert(!z80_banked_pio_poll());
  tokens = 4; /* full progress FIFO resets the deadline without stalling PIO */
  assert(z80_banked_pio_poll() && tokens == 0);
  levels[9] = false;
  assert(z80_banked_pio_poll());
  z80_banked_pio_disable();
  assert(!io_active && !irq_source && !bank_driver && clock_running && levels[8]);
  assert(io_bank0_hw->io[7].ctrl == GPIO_FUNC_SIO && io_bank0_hw->io[8].ctrl == GPIO_FUNC_SIO);
  levels[0] = false; /* rearm while BUSACK# held, preserving the selected bank */
  assert(z80_banked_pio_rearm(slow_service) && levels[8]);
  uint32_t previous = save_and_disable_interrupts();
  z80_banked_pio_disable();
  assert(interrupts_disabled);
  restore_interrupts(previous);
  assert(!interrupts_disabled);
  puts("PASS: PIO allocation/fallback, clock mux, slow-IN input turnaround, bank ownership, timeout and bus-grant rearm");
}
