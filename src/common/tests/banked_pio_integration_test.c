/* Execute the real I/O trap, clock and two-block PIO driver together against
 * an instrumented PIO HAL. The Python test executes the assembled PIO words. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../banked_pio.c"
#include "../clock.c"
#include "../bus.c"
#include "../io_trap.c"
#include "pio_fakes/test_pio_blocks.h"

static bool levels[32], outputs[32];
static uint64_t now;
static bool bank_available = true, interrupts_disabled, failed_closed;
static unsigned writes, reads;
static z80_memory_bank_driver_t bank_driver;
static jmp_buf reboot_exit;

uint32_t clock_get_hz(uint clock) { (void)clock; return 120000000; }
void gpio_init(uint pin) { (void)pin; }
void gpio_disable_pulls(uint pin) { (void)pin; }
void gpio_put(uint pin, bool value) { levels[pin] = value; }
bool gpio_get(uint pin) {
  int pad = pio_pad_level(pin);
  return pad >= 0 ? pad : levels[pin];
}
uint32_t gpio_get_all(void) {
  uint32_t value = 0;
  for (uint pin = 0; pin < 32; ++pin) value |= (uint32_t)gpio_get(pin) << pin;
  return value;
}
void gpio_set_function(uint pin, uint function) { io_bank0_hw->io[pin].ctrl = function; }
uint gpio_get_function(uint pin) { return io_bank0_hw->io[pin].ctrl; }
void gpio_set_dir(uint pin, bool output) { outputs[pin] = output; }
void busy_wait_us_32(uint32_t delay) { now += delay; }
uint64_t time_us_64(void) { return now; }
absolute_time_t make_timeout_time_us(uint32_t delay) { return now + delay; }
bool time_reached(absolute_time_t deadline) { return now >= deadline; }
void tight_loop_contents(void) { assert(false); }
static watchdog_hw_t watchdog_registers;
watchdog_hw_t *watchdog_hw = &watchdog_registers;
bool watchdog_caused_reboot(void) { return false; }
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  (void)pc; (void)sp; (void)delay;
  longjmp(reboot_exit, 1);
}
void z80_cpu_fail_closed(void) { failed_closed = true; }
void z80_memory_bank_set_driver(z80_memory_bank_driver_t driver) { bank_driver = driver; }
void z80_memory_bank_select(unsigned bank) { bank_driver(bank & 1); }
unsigned z80_memory_bank_status(void) { return bank_available ? 0xB4 | gpio_get(8) : 0; }
uint32_t save_and_disable_interrupts(void) {
  uint32_t previous = interrupts_disabled;
  interrupts_disabled = true;
  return previous;
}
void restore_interrupts(uint32_t state) { interrupts_disabled = state; }

static uint8_t read_port(uint8_t port, void *context) {
  (void)context;
  /* PIO1 completes the cycle; core 0 only supplies the byte. */
  assert(port == 1 && io_active && blocks[1].enabled[io_sm] && clock_running);
  ++reads;
  return 2;
}
static void write_port(uint8_t port, uint8_t value, void *context) {
  (void)context;
  assert(io_active && blocks[1].enabled[io_sm] && clock_running);
  assert((port == 5 && value == 0xB0) || (port == 0 && value == 'X'));
  ++writes;
}
static void in_cycle(uint8_t port, uint8_t expected) {
  uint64_t started = now;
  fake_push(fake_snapshot(true, false, port, 0));
  io_irq();
  uint32_t reply = fake_take_reply();
  assert((reply >> 2 & 0xFF) == expected && (reply & 1u) == gpio_get(PIN_MEMORY_BANK));
  assert((reply & 2u) == 0 && reply >> 10 == (0x3FFu | 0x003u << 10));
  assert(now == started); /* no settling guards or stepping on core 0 */
}
static void out_cycle(uint8_t port, uint8_t value) {
  uint64_t started = now;
  unsigned before = writes;
  fake_push(fake_snapshot(false, true, port, value));
  io_irq();
  assert(writes == before + 1 && blocks[1].tx_count == 0 && now == started);
}

int main(void) {
  memset(levels, 1, sizeof(levels));
  levels[PIN_CLK] = levels[PIN_IO_RELEASE] = levels[PIN_MEMORY_BANK] = false;
  for (uint pin = 0; pin < 32; ++pin) io_bank0_hw->io[pin].ctrl = GPIO_FUNC_SIO;
  outputs[PIN_CLK] = true;
  assert(z80_io_trap_enable(read_port, write_port, NULL));
  assert(!z80_clock_set_hz(19000000) && z80_clock_get_hz() == 0);
  assert(z80_clock_set_hz(20000000));
  out_cycle(5, 0xB0);
  assert(!z80_clock_set_hz(19000000) && z80_clock_get_hz() == 20000000);
  for (uint count = 0; count < 100; ++count) {
    z80_memory_bank_select(count & 1); /* replies carry either A16 level */
    in_cycle(1, 2);
    out_cycle(0, 'X');
  }
  assert(reads == 100 && writes == 101);
  z80_banked_pio_status_t status = z80_banked_pio_status();
  assert(status.reads == 100 && status.writes == 101);
  assert(status.active && status.running);
  /* Bank status is answered by the trap, not the application. */
  in_cycle(7, (uint8_t)(0xB4 | gpio_get(PIN_MEMORY_BANK)));
  assert(reads == 100);
  /* Supervisor drain applies completed OUTs without the IRQ. */
  fake_push(fake_snapshot(false, true, 0, 'X'));
  z80_io_trap_drain_writes();
  assert(writes == 102 && blocks[1].rx_count == 0);
  /* Neither strobe active: fail closed with the cycle recorded for reset. */
  fake_push(fake_snapshot(false, false, 1, 0));
  if (setjmp(reboot_exit) == 0) {
    io_irq();
    assert(false);
  }
  assert(failed_closed && z80_io_trap_control_error_count() == 1);
  assert(watchdog_hw->scratch[1] == Z80_IO_TRAP_RESET_INVALID_CONTROLS);
  assert(!io_active);
  puts("PASS: real trap/clock/PIO integration: IN replies, queued OUTs, bank "
       "status, drain and invalid-strobe fail-closed");
}
