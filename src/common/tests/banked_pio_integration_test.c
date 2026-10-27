/* Execute the real C ownership/clock driver against an instrumented PIO HAL.
 * The separately registered Python test executes the assembled PIO words. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../banked_pio.c"
#include "../clock.c"
#include "../bus.c"
#include "../io_trap.c"

struct test_pio test_pio1;
io_bank0_hw_t test_io_bank0;
static bool sio_levels[32];
static bool levels[32], outputs[32], claimed[4], enabled[4];
static uint pcs[4], pin_latches[4];
static pio_sm_config configs[4];
static uint32_t irq_flags, system_hz = 120000000;
static uint64_t now;
static bool space_available = true, irq_enabled, irq_source, bank_available = true;
static bool interrupts_disabled;
static bool inject_startup_io;
static uint32_t frequency_at_startup_io;
static void transaction(uint8_t port, bool read, uint8_t value);
static uint tokens;
static uint masked_pin_updates, clock_disables, mux_changes;
static uint completion_edges, phases;
static irq_handler_t irq_handler;
static z80_memory_bank_driver_t bank_driver;

static void fold_irq_force(void) {
  irq_flags |= test_pio1.irq_force;
  test_pio1.irq_force = 0;
}
uint32_t clock_get_hz(uint clock) { (void)clock; return system_hz; }
static unsigned sampled, writes;
static uint8_t received;
static bool raw_gpio_get(uint pin) {
  if (io_bank0_hw->io[pin].ctrl == 99) {
    uint sm = pin == PIN_CLK ? (uint)clock_sm : (uint)io_sm;
    return (pin_latches[sm] >> pin) & 1;
  }
  if (pin == PIN_DATA_0 || (pin > PIN_DATA_0 && pin <= PIN_DATA_7)) {
    if (outputs[pin]) return sio_levels[pin];
  }
  return outputs[pin] ? sio_levels[pin] : levels[pin];
}
static void clock_falling_edge(void) {
  if (!levels[PIN_IORQ_N] && gpio_get(PIN_IO_RELEASE)) {
    assert(completion_edges);
    if (--completion_edges)
      return;
    if (!levels[PIN_RD_N]) {
      assert(!gpio_get(PIN_DATA_UP_OE_N));
      assert(gpio_get(PIN_DATA_DOWN_OE_N));
      received = 0;
      for (uint bit = 0; bit < 8; ++bit) {
        assert(outputs[PIN_DATA_0 + bit]);
        received |= gpio_get(PIN_DATA_0 + bit) << bit;
      }
      ++sampled;
    } else {
      assert(gpio_get(PIN_DATA_UP_OE_N));
      assert(!gpio_get(PIN_DATA_DOWN_OE_N));
    }
    levels[PIN_IORQ_N] = levels[PIN_RD_N] = levels[PIN_WR_N] = true;
  }
}
static bool checking_clock, observed_clock;
bool gpio_get(uint pin) {
  if (!checking_clock) {
    checking_clock = true;
    bool clock = raw_gpio_get(PIN_CLK);
    bool falling = observed_clock && !clock;
    observed_clock = clock;
    if (falling) clock_falling_edge();
    checking_clock = false;
  }
  return raw_gpio_get(pin);
}
void gpio_put(uint pin, bool value) {
  (void)gpio_get(PIN_CLK);
  sio_levels[pin] = value;
  (void)gpio_get(PIN_CLK);
}
void gpio_set_function(uint pin, uint function) {
  (void)gpio_get(PIN_CLK);
  ++mux_changes;
  io_bank0_hw->io[pin].ctrl = function;
  (void)gpio_get(PIN_CLK);
}
uint gpio_get_function(uint pin) { return io_bank0_hw->io[pin].ctrl; }
void gpio_init(uint pin) { (void)pin; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enable) {
  (void)pin; (void)events; assert(!enable);
}
void gpio_acknowledge_irq(uint pin, uint32_t events) { (void)pin; (void)events; }
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enable,
                                      void (*callback)(uint, uint32_t)) {
  (void)pin; (void)events; (void)enable; (void)callback; assert(false);
}
uint32_t gpio_get_all(void) {
  uint32_t value = 0;
  for (uint pin = 0; pin < 32; ++pin) value |= (uint32_t)gpio_get(pin) << pin;
  return value;
}
absolute_time_t make_timeout_time_us(uint32_t delay) { return now + delay; }
bool time_reached(absolute_time_t deadline) { return now >= deadline; }
void tight_loop_contents(void) { assert(false); }
static watchdog_hw_t watchdog_registers;
watchdog_hw_t *watchdog_hw = &watchdog_registers;
bool watchdog_caused_reboot(void) { return false; }
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  (void)pc; (void)sp; (void)delay; assert(false);
}
void z80_cpu_fail_closed(void) { assert(false); }
uint pwm_gpio_to_slice_num(uint pin) { (void)pin; return 0; }
uint pwm_gpio_to_channel(uint pin) { (void)pin; return 0; }
void pwm_set_enabled(uint slice, bool enabled) { (void)slice; assert(!enabled); }
void pwm_set_clkdiv_int_frac4(uint slice, uint8_t divider, uint8_t fraction) {
  (void)slice; (void)divider; (void)fraction; assert(false);
}
void pwm_set_wrap(uint slice, uint16_t wrap) { (void)slice; (void)wrap; assert(false); }
void pwm_set_chan_level(uint slice, uint channel, uint16_t level) {
  (void)slice; (void)channel; (void)level; assert(false);
}
void pwm_set_counter(uint slice, uint16_t counter) { (void)slice; (void)counter; assert(false); }

void gpio_set_dir(uint pin, bool output) { outputs[pin] = output; }
void busy_wait_us_32(uint32_t delay) { now += delay; }
uint64_t time_us_64(void) { return now; }
uint32_t save_and_disable_interrupts(void) {
  uint32_t previous = interrupts_disabled;
  interrupts_disabled = true;
  return previous;
}
void restore_interrupts(uint32_t state) {
  interrupts_disabled = state;
  if (!interrupts_disabled && inject_startup_io && clock_running) {
    /* Interrupt as soon as the first PIO clock starts, before the caller of
     * clock_start can finish. This is the real BIOS cold-entry OUT. */
    inject_startup_io = false;
    frequency_at_startup_io = z80_clock_get_hz();
    transaction(5, false, 0xB0);
  }
}
void z80_memory_bank_set_driver(z80_memory_bank_driver_t driver) { bank_driver = driver; }
void z80_memory_bank_select(unsigned bank) { bank_driver(bank & 1); }
unsigned z80_memory_bank_status(void) { return bank_available ? 0xB4 | levels[8] : 0; }
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
  ++mux_changes;
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
  if (sm == (uint)clock_sm && !enable) ++clock_disables;
}
void pio_sm_restart(PIO pio, uint sm) { assert(pio == pio1 && !enabled[sm]); }
void pio_sm_exec(PIO pio, uint sm, uint16_t instruction) {
  assert(pio == pio1 && !enabled[sm]);
  (void)gpio_get(PIN_CLK);
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
  (void)gpio_get(PIN_CLK);
}
void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t values, uint32_t mask) {
  assert(pio == pio1 && !enabled[sm]);
  ++masked_pin_updates;
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

static uint8_t read_port(uint8_t port, void *context) {
  (void)context;
  assert(!enabled[io_sm] && in_slow);
  assert(gpio_get_function(PIN_DATA_DOWN_OE_N) == GPIO_FUNC_SIO);
  assert(gpio_get_function(PIN_IO_RELEASE) == GPIO_FUNC_SIO);
  assert(port == 1); return 2;
}
static void write_port(uint8_t port, uint8_t value, void *context) {
  (void)context;
  assert(!enabled[io_sm] && in_slow);
  assert(gpio_get_function(PIN_DATA_DOWN_OE_N) == GPIO_FUNC_SIO);
  assert(gpio_get_function(PIN_IO_RELEASE) == GPIO_FUNC_SIO);
  assert(!gpio_get(PIN_DATA_DOWN_OE_N) && gpio_get(PIN_DATA_UP_OE_N));
  assert((port == 5 && value == 0xB0) || (port == 0 && value == 'X'));
  ++writes;
}
static void transaction(uint8_t port, bool read, uint8_t value) {
  uint64_t started = now;
  uint masks_before = masked_pin_updates, disables_before = clock_disables;
  uint muxes_before = mux_changes;
  bool bank_before = gpio_get(PIN_MEMORY_BANK);
  bool high_phase = (phases >> 1) & 1;
  completion_edges = 1 + ((phases++ >> 2) % 3);
  uint pulses = completion_edges - (high_phase && completion_edges > 1);
  if (high_phase) pin_latches[clock_sm] |= 1u << PIN_CLK;
  else pin_latches[clock_sm] &= ~(1u << PIN_CLK);
  for (uint bit = 0; bit < 4; ++bit)
    levels[PIN_PORT_A0 + bit] = (port >> (bit == 3 ? 4 : bit)) & 1;
  for (uint bit = 0; bit < 8; ++bit) levels[PIN_DATA_0 + bit] = (value >> bit) & 1;
  levels[PIN_IORQ_N] = false;
  levels[PIN_RD_N] = !read;
  levels[PIN_WR_N] = read;
  irq_flags |= 1u << SLOW_IRQ;
  irq_handler();
  assert(clock_running && !in_slow && enabled[io_sm]);
  assert(pcs[io_sm] == program_offset + banked_io_offset_io_start);
  assert(io_bank0_hw->io[PIN_CLK].ctrl == 99 && io_bank0_hw->io[PIN_IO_RELEASE].ctrl == 99);
  assert(!gpio_get(PIN_IO_RELEASE) && gpio_get(PIN_DATA_DOWN_OE_N));
  assert(gpio_get(PIN_DATA_UP_OE_N));
  assert(!pio_interrupt_get(pio1, SLOW_IRQ));
  if (read) assert(received == (port == 7 ? (0xB4 | bank_before) : 2));
  assert(gpio_get(PIN_MEMORY_BANK) == bank_before);
  assert(masked_pin_updates == masks_before);
  assert(clock_disables - disables_before == 2);
  assert(mux_changes == muxes_before); /* All six mux writes avoid SDK pad setup. */
  /* Retain the hardware-qualified SIO handoff, including its extra HIGH
   * guard before CLK changes mux. PIO forcing is not an electrical model. */
  assert(now - started == (read ? 5u : 4u) + 3u * pulses);
}
int main(int argc, char **argv) {
  assert(argc == 1 || (argc == 2 && strcmp(argv[1], "startup-irq") == 0));
  bool early = argc == 2;
  memset(levels, 1, sizeof(levels));
  memset(sio_levels, 1, sizeof(sio_levels));
  for (uint pin = 0; pin < 32; ++pin) io_bank0_hw->io[pin].ctrl = GPIO_FUNC_SIO;
  outputs[PIN_CLK] = outputs[PIN_DATA_UP_OE_N] = true;
  outputs[PIN_DATA_DOWN_OE_N] = outputs[PIN_MEMORY_BANK] = outputs[PIN_IO_RELEASE] = true;
  sio_levels[PIN_CLK] = sio_levels[PIN_IO_RELEASE] = sio_levels[PIN_MEMORY_BANK] = false;
  assert(z80_io_trap_enable(read_port, write_port, NULL));
  assert(!z80_clock_set_hz(19000000) && z80_clock_get_hz() == 0);
  inject_startup_io = early;
  assert(z80_clock_set_hz(20000000));
  if (early) {
    assert(!inject_startup_io && frequency_at_startup_io == 20000000);
  } else {
    transaction(5, false, 0xB0);
  }
  assert(!z80_clock_set_hz(19000000) && z80_clock_get_hz() == 20000000);
  assert(clock_running);
  for (uint count = 0; count < 100; ++count) {
    bank_driver(count & 1); /* Control-pin SET must preserve either A16 level. */
    transaction(1, true, 0);
    transaction(0, false, 'X');
  }
  assert(sampled == 100 && writes == 101);
  z80_banked_pio_status_t status = z80_banked_pio_status();
  assert(status.requests == 201 && status.reads == 100 && status.writes == 101);
  assert(status.active && status.running && !status.slow);
  /* Reads retain capability discovery; slow-routed writes cannot switch banks. */
  transaction(7, true, 0);
  transaction(7, false, !gpio_get(PIN_MEMORY_BANK));
  assert(sampled == 101 && writes == 101);
  puts("PASS: real PIO/clock/trap/bus integration: BIOS marker then 100 console status/output pairs");
}
