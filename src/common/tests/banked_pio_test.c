/* Execute the real PIO0-clock/PIO1-engine driver against an instrumented PIO HAL.
 * The separately registered Python test executes the assembled PIO words. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../banked_pio.c"
#include "pio_fakes/test_pio_blocks.h"

static bool levels[32], outputs[32];
static uint32_t system_hz = 120000000;
static uint64_t now;
static bool bank_available = true;
static bool interrupts_disabled;
static z80_memory_bank_driver_t bank_driver;
static uint8_t write_ports[16], write_values[16], read_ports[16];
static uint write_count, read_count;
static uint32_t fault_controls;
static jmp_buf fault_exit;

uint32_t clock_get_hz(uint clock) { (void)clock; return system_hz; }
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
uint32_t save_and_disable_interrupts(void) {
  uint32_t previous = interrupts_disabled;
  interrupts_disabled = true;
  return previous;
}
void restore_interrupts(uint32_t state) { interrupts_disabled = state; }
void z80_memory_bank_set_driver(z80_memory_bank_driver_t driver) { bank_driver = driver; }
unsigned z80_memory_bank_status(void) { return bank_available ? 0xB4 | gpio_get(8) : 0; }
void z80_clock_stop(void) { z80_banked_pio_clock_stop(); }
void z80_clock_prepare_stepping(void) {
  z80_clock_stop();
  busy_wait_us_32(1);
  levels[PIN_CLK] = false;
  io_bank0_hw->io[PIN_CLK].ctrl = GPIO_FUNC_SIO;
}

static uint8_t record_read(uint8_t port) {
  assert(read_count < 16);
  read_ports[read_count++] = port;
  return (uint8_t)(0xC0 | port);
}
static void record_write(uint8_t port, uint8_t value) {
  assert(write_count < 16);
  write_ports[write_count] = port;
  write_values[write_count++] = value;
}
static void record_fault(uint32_t controls) {
  fault_controls = controls;
  longjmp(fault_exit, 1);
}
static bool rearm(void) {
  return z80_banked_pio_rearm(record_read, record_write, record_fault);
}

static void assert_sio_idle(unsigned bank) {
  assert(!io_active && !blocks[1].rx_irq && !bank_driver);
  for (uint pin = PIN_DATA_UP_OE_N; pin <= PIN_DATA_7; ++pin)
    assert(io_bank0_hw->io[pin].ctrl == GPIO_FUNC_SIO);
  assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  assert(!levels[PIN_IO_RELEASE] && levels[PIN_MEMORY_BANK] == bank);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin) assert(!outputs[pin]);
}

static void test_allocation(void) {
  blocks[1].space = false;
  assert(!rearm() && !allocated);
  blocks[1].space = true;
  system_hz = 144000000;
  assert(!rearm() && !allocated);
  system_hz = 120000000;
  blocks[0].space = false; /* no room beside the matrix program */
  assert(!rearm() && !allocated);
  blocks[0].space = true;
  for (uint sm = 0; sm < 4; ++sm) blocks[0].claimed[sm] = true;
  assert(!rearm() && !allocated);
  for (uint sm = 0; sm < 3; ++sm) blocks[0].claimed[sm] = false;
  /* PIO0 SM 3 stays claimed: the matrix display shares PIO0. */
  blocks[1].claimed[2] = true; /* PIO1 must not be shared */
  assert(!rearm() && !allocated);
  blocks[1].claimed[2] = false;
}

static void test_unbanked_ownership(void) {
  /* PIO still owns clock and I/O without bank 1, but GP8/A16 stays on SIO
   * and no bank driver is installed, so OUT 07h cannot switch banks. */
  bank_available = false;
  assert(rearm() && allocated && io_active && !bank_driver);
  for (uint pin = PIN_DATA_UP_OE_N; pin <= PIN_DATA_7; ++pin)
    if (pin != PIN_MEMORY_BANK)
      assert(io_bank0_hw->io[pin].ctrl == GPIO_FUNC_PIO1);
  assert(io_bank0_hw->io[PIN_MEMORY_BANK].ctrl == GPIO_FUNC_SIO);
  z80_banked_pio_disable();
  assert_sio_idle(false);
  bank_available = true;
}

static void test_configuration(void) {
  levels[PIN_MEMORY_BANK] = true;
  assert(rearm() && io_active && io_irq_enabled && blocks[1].rx_irq && bank_driver);
  const pio_sm_config *clock = &blocks[0].configs[clock_sm];
  const pio_sm_config *io = &blocks[1].configs[io_sm];
  assert(clock->wrap_bottom == 1 + banked_clock_wrap_target);
  assert(clock->wrap_top == 1 + banked_clock_wrap && clock->set_base == PIN_CLK);
  assert(io->wrap_bottom == 2 + banked_io_wrap_target && io->wrap_top == 2 + banked_io_wrap);
  assert(io->in_base == PIN_DATA_0 && io->out_base == PIN_MEMORY_BANK && io->out_count == 10);
  assert(io->set_base == PIN_DATA_UP_OE_N && io->set_count == 2);
  assert(io->side_base == PIN_IO_RELEASE && io->jmp_pin == PIN_RD_N);
  assert(io->right_shift && io->autopush && io->push_threshold == 32);
  assert(io->fifo_join == PIO_FIFO_JOIN_NONE); /* TX carries IN replies */
  assert(blocks[1].pcs[io_sm] == 2 + banked_io_offset_io_start);
  /* Idle hand-off: buffers isolated, WAIT armed, A16 kept, D0-D7 inputs. */
  uint32_t control = (1u << PIN_DATA_UP_OE_N) | (1u << PIN_DATA_DOWN_OE_N) |
                     (1u << PIN_MEMORY_BANK) | (1u << PIN_IO_RELEASE);
  assert((blocks[1].pins & control) ==
         ((1u << PIN_DATA_UP_OE_N) | (1u << PIN_DATA_DOWN_OE_N) | (1u << PIN_MEMORY_BANK)));
  assert((blocks[1].dirs & (control | 0xFFu << PIN_DATA_0)) == control);
  assert(io_inits == 11 + 12); /* GP6, GP7, GP9, D0-D7, then also GP8 */
  assert(!z80_banked_pio_clock_start(19000000));
  assert(z80_banked_pio_clock_start(20000000) && clock_running);
  assert(!pio_interrupt_get(pio0, CPU_HOLD_IRQ));
  assert(io_bank0_hw->io[PIN_CLK].ctrl == GPIO_FUNC_PIO0);
}

static void test_service_order(void) {
  /* OUTs PIO already completed, then an IN waiting for its reply. */
  fake_push(fake_snapshot(false, true, 0x00, 'A'));
  fake_push(fake_snapshot(false, true, 0x10, 0x01));
  fake_push(fake_snapshot(true, false, 0x17, 0xEE));
  io_irq();
  assert(write_count == 2 && read_count == 1 && read_ports[0] == 0x17);
  assert(write_ports[0] == 0x00 && write_values[0] == 'A');
  assert(write_ports[1] == 0x10 && write_values[1] == 0x01);
  uint32_t reply = fake_take_reply();
  assert(reply == ((uint32_t)(0xC0 | 0x17) << 2 | 1u | 0x3FFu << 10 | 0x003u << 20));
  /* A16 in the reply follows the current bank; GP9 bit stays LOW. */
  bank_driver(0);
  assert(!gpio_get(PIN_MEMORY_BANK) && blocks[1].pcs[io_sm] == 2 + banked_io_offset_io_start);
  fake_push(fake_snapshot(true, false, 0x07, 0));
  io_irq();
  reply = fake_take_reply();
  assert((reply & 3u) == 0 && (reply >> 2 & 0xFF) == (0xC0 | 0x07));
  assert(clock_running && blocks[1].enabled[io_sm]);
  z80_banked_pio_status_t status = z80_banked_pio_status();
  assert(status.io_pc == banked_io_offset_io_start && status.active && status.running);
  z80_banked_pio_latency_t l = z80_banked_pio_take_latency();
  assert(l.available && l.in_count == 2 && l.out_count == 2);
  l = z80_banked_pio_take_latency();
  assert(l.available && l.in_count == 0 && l.out_count == 0 && l.in_cycles == 0);
}

static void test_invalid_strobes_fail_closed(void) {
  for (int both = 0; both < 2; ++both) {
    uint32_t snapshot = fake_snapshot(both, both, 0x01, 0x55);
    fake_push(snapshot);
    if (setjmp(fault_exit) == 0) {
      io_irq();
      assert(false);
    }
    assert(fault_controls == snapshot << PIN_DATA_0);
    in_service = false; /* the real handler never returns */
  }
}

static void test_poll_timeout(void) {
  assert(z80_banked_pio_poll());
  blocks[0].flags |= 1u << IO_HOLD_IRQ; /* a cycle in progress */
  assert(z80_banked_pio_poll());
  now += 499999;
  assert(z80_banked_pio_poll());
  now += 3;
  assert(!z80_banked_pio_poll());
  /* Serviced snapshots are progress: a stalled-but-moving stream never trips. */
  for (uint i = 0; i < 4; ++i) fake_push(fake_snapshot(false, true, 0, (uint8_t)i));
  io_irq();
  assert(z80_banked_pio_poll());
  assert(z80_banked_pio_poll());
  now += 500001;
  assert(!z80_banked_pio_poll());
  blocks[0].flags &= ~(1u << IO_HOLD_IRQ);
  assert(z80_banked_pio_poll());
}

static void test_disable(void) {
  unsigned reads = read_count, writes = write_count;
  fake_push(fake_snapshot(false, true, 0x05, 0xC0));
  fake_push(fake_snapshot(true, false, 0x00, 0)); /* never answered */
  levels[0] = false; /* bus granted */
  z80_banked_pio_disable();
  assert(write_count == writes + 1 && write_ports[writes] == 0x05);
  assert(read_count == reads && blocks[1].tx_count == 0);
  assert(!pio_interrupt_get(pio0, IO_HOLD_IRQ) && clock_running);
  assert_sio_idle(false);
  /* Rearm after the grant preserves the selected bank. */
  levels[PIN_MEMORY_BANK] = true;
  assert(rearm() && gpio_get(PIN_MEMORY_BANK));
  uint32_t previous = save_and_disable_interrupts();
  z80_banked_pio_disable();
  assert(interrupts_disabled);
  restore_interrupts(previous);
  assert(!interrupts_disabled);
}

int main(void) {
  memset(levels, 1, sizeof(levels));
  levels[PIN_IO_RELEASE] = levels[PIN_MEMORY_BANK] = levels[PIN_CLK] = false;
  for (uint pin = 0; pin < 32; ++pin) io_bank0_hw->io[pin].ctrl = GPIO_FUNC_SIO;
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin) outputs[pin] = true;
  test_allocation();
  test_unbanked_ownership();
  test_configuration();
  test_service_order();
  test_invalid_strobes_fail_closed();
  test_poll_timeout();
  test_disable();
  puts("PASS: PIO0 clock/PIO1 engine allocation, ownership, IN/OUT service order, faults, "
       "timeout and bus-grant rearm");
}
