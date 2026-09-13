#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "hardware/spi.h"
#include "z80sbc/bus.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

static bool levels[32];
static bool initialized[32];
static bool outputs[32];
static bool pulls_disabled[32];
static bool preloaded[32];
static bool check_data_setup;
static unsigned data_enables;
static uint8_t registers[32];
static uint64_t now_us;
static bool clock_running;
static bool release_controls;
static bool irq_enabled;
static unsigned spi_writes;
static unsigned spi_reads;
static unsigned fail_write;
static unsigned fail_read;
static int corrupt_register;
static unsigned watchdog_count;
static jmp_buf reboot_target;
static void (*irq_callback)(uint, uint32_t);
spi_inst_t *spi0;

void gpio_init(uint pin) {
  if (pin >= PIN_DATA_0 && pin <= PIN_DATA_7)
    assert(!levels[PIN_DATA_ENABLE]);
  initialized[pin] = true;
  outputs[pin] = false;
  preloaded[pin] = false;
}
void gpio_put(uint pin, bool value) {
  levels[pin] = value;
  preloaded[pin] = true;
  if (pin == PIN_DATA_ENABLE && value)
    ++data_enables;
  if (pin == PIN_ADDR_ENABLE && !value) {
    memset(registers, 0, sizeof(registers));
    registers[0] = registers[1] = 0xFF;
  }
}
bool gpio_get(uint pin) { return levels[pin]; }
uint32_t gpio_get_all(void) {
  uint32_t result = 0;
  for (uint pin = 0; pin < 32; ++pin)
    result |= (uint32_t)levels[pin] << pin;
  return result;
}
void gpio_set_dir(uint pin, bool output) {
  if (check_data_setup && output &&
      pin >= PIN_DATA_0 && pin <= PIN_DATA_7) {
    assert(initialized[pin] && preloaded[pin]);
    assert(!levels[PIN_DATA_ENABLE]);
  }
  outputs[pin] = output;
}
void gpio_set_function(uint pin, uint function) { (void)pin; (void)function; }
void gpio_disable_pulls(uint pin) { pulls_disabled[pin] = true; }
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enabled) {
  (void)pin; (void)events; irq_enabled = enabled;
}
void gpio_acknowledge_irq(uint pin, uint32_t events) { (void)pin; (void)events; }
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enabled,
                                       void (*callback)(uint, uint32_t)) {
  gpio_set_irq_enabled(pin, events, enabled);
  irq_callback = callback;
}
void busy_wait_us_32(uint32_t delay) { now_us += delay; }
void sleep_ms(uint32_t delay) { now_us += (uint64_t)delay * 1000; }
absolute_time_t make_timeout_time_us(uint32_t delay) { return now_us + delay; }
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }
void tight_loop_contents(void) { now_us += 100000; }
uint spi_init(spi_inst_t *spi, uint hz) { (void)spi; return hz; }
void spi_set_format(spi_inst_t *spi, uint bits, uint polarity, uint phase, uint order) {
  (void)spi; (void)bits; (void)polarity; (void)phase; (void)order;
}
int spi_write_blocking(spi_inst_t *spi, const uint8_t *data, size_t count) {
  (void)spi;
  assert(levels[PIN_ADDR_ENABLE]);
  assert(!levels[PIN_SPI_CS_N] && count == 3 && data[0] == 0x40);
  registers[data[1]] = data[2];
  return ++spi_writes == fail_write ? 2 : (int)count;
}
int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *tx, uint8_t *rx, size_t count) {
  (void)spi;
  assert(levels[PIN_ADDR_ENABLE]);
  assert(!levels[PIN_SPI_CS_N] && count == 3 && tx[0] == 0x41);
  rx[2] = registers[tx[1]] ^ (tx[1] == corrupt_register ? 1 : 0);
  return ++spi_reads == fail_read ? 2 : (int)count;
}
bool z80_clock_set_hz(uint32_t hz) { clock_running = hz != 0; return clock_running; }
void z80_clock_stop(void) { clock_running = false; }
void z80_clock_resume(void) {
  assert(levels[PIN_DATA_ENABLE]);
  clock_running = true;
  if (release_controls) {
    levels[PIN_IORQ_N] = true;
    levels[PIN_RD_N] = true;
    levels[PIN_WR_N] = true;
  }
}
void z80_reset_with_clock_cycles(unsigned cycles, uint32_t half_period) {
  assert(cycles >= 3 && half_period > 0);
  gpio_put(PIN_RESET_N, 0);
  clock_running = false;
}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay) {
  (void)pc; (void)sp; (void)delay;
  ++watchdog_count;
  longjmp(reboot_target, 1);
}

static void reset_fixture(void) {
  memset(levels, 1, sizeof(levels));
  memset(initialized, 0, sizeof(initialized));
  memset(outputs, 0, sizeof(outputs));
  memset(pulls_disabled, 0, sizeof(pulls_disabled));
  memset(preloaded, 0, sizeof(preloaded));
  check_data_setup = false;
  data_enables = 0;
  memset(registers, 0, sizeof(registers));
  now_us = 0;
  spi_writes = spi_reads = fail_write = fail_read = watchdog_count = 0;
  corrupt_register = -1;
  clock_running = true;
  release_controls = false;
}

static void assert_isolated(void) {
  assert(!levels[PIN_ADDR_ENABLE] && !levels[PIN_DATA_ENABLE]);
}

static void assert_fail_closed(void) {
  assert_isolated();
  assert(!levels[PIN_RESET_N] && levels[PIN_BUSREQ_N]);
  assert(levels[PIN_SRAM_CE_N] && levels[PIN_SRAM_OE_N] && levels[PIN_SRAM_WE_N]);
  assert(!clock_running);
}

static void test_safe_startup_and_first_write(void) {
  reset_fixture();
  check_data_setup = true;
  z80_safe_startup();
  assert_isolated();
  assert(!levels[PIN_RESET_N] && !levels[PIN_CLK] && !levels[PIN_DATA_DIR]);
  assert(levels[PIN_BUSREQ_N] && levels[PIN_SPI_CS_N]);
  assert(levels[PIN_SRAM_CE_N] && levels[PIN_SRAM_OE_N] && levels[PIN_SRAM_WE_N]);
  assert(data_enables == 0);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin) {
    assert(initialized[pin]);
    assert(!outputs[pin] && pulls_disabled[pin]);
  }

  /* Exercise the cold-boot write before any read configures the data pins. */
  assert(z80_sram_prepare_dma());
  assert(z80_sram_write_byte(0x1234, 0xA5));
  assert(levels[PIN_DATA_ENABLE] && levels[PIN_DATA_DIR]);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin) {
    assert(outputs[pin]);
    assert(levels[pin] == ((0xA5u >> (pin - PIN_DATA_0)) & 1u));
  }
  assert(data_enables == 1);

  z80_safe_startup();
  assert_isolated();
  assert(data_enables == 1);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin)
    assert(!outputs[pin] && pulls_disabled[pin]);
}

static void test_direction_verification(void) {
  reset_fixture();
  assert(mcp23s17_set_directions(0x55, 0xAA));
  assert(spi_reads == 2 && levels[PIN_SPI_CS_N]);
  for (int reg = 0; reg < 2; ++reg) {
    reset_fixture();
    corrupt_register = reg;
    assert(!mcp23s17_set_directions(0xFF, 0xFF));
    assert(!levels[PIN_ADDR_ENABLE] && levels[PIN_SPI_CS_N]);
  }
  for (unsigned transfer = 1; transfer <= 2; ++transfer) {
    reset_fixture();
    fail_write = transfer;
    assert(!mcp23s17_set_directions(0, 0));
    assert(!levels[PIN_ADDR_ENABLE] && levels[PIN_SPI_CS_N]);
    reset_fixture();
    fail_read = transfer;
    assert(!z80_address_bus_prepare_input());
    assert(!levels[PIN_ADDR_ENABLE] && levels[PIN_SPI_CS_N]);
  }
  reset_fixture();
  fail_write = 2;
  assert(!z80_address_bus_drive(0x55AA));
  assert(!levels[PIN_ADDR_ENABLE]);
}

static void test_bus_handshake(void) {
  reset_fixture();
  assert(!z80_cpu_request_bus(10));
  assert_isolated();
  assert(levels[PIN_BUSREQ_N]);
  reset_fixture();
  levels[PIN_BUSACK_N] = false;
  assert(z80_cpu_request_bus(10));
  assert(!levels[PIN_BUSREQ_N]);
  assert_isolated();
  assert(!z80_cpu_release_bus(10));
  assert_fail_closed();
  reset_fixture();
  assert(z80_cpu_release_bus(10));
  assert_isolated();
  reset_fixture();
  levels[PIN_RD_N] = false;
  assert(!z80_cpu_prepare_reset_dma());
  assert_fail_closed();
}

static void test_dma_preparation(void) {
  reset_fixture();
  assert(z80_cpu_prepare_reset_dma());
  assert_fail_closed();
  assert(spi_writes == 0 && spi_reads == 0);
  assert(z80_sram_write_byte(0x1234, 0x5A));
  assert(registers[0] == 0 && registers[1] == 0);
  assert(registers[0x14] == 0x34 && registers[0x15] == 0x12);
  assert(!levels[PIN_RESET_N]);
}

static void test_trap_fault(uint pin, bool io_read) {
  reset_fixture();
  uint32_t baseline = z80_io_trap_timeout_count();
  levels[PIN_IORQ_N] = pin != PIN_IORQ_N;
  levels[PIN_RD_N] = !io_read;
  levels[PIN_WR_N] = io_read;
  z80_io_trap_enable(NULL, NULL, NULL);
  if (setjmp(reboot_target) == 0) {
    irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
    assert(false);
  }
  assert(watchdog_count == 1);
  assert(z80_io_trap_timeout_count() == baseline + 1);
  assert_fail_closed();
}

static void test_traps(void) {
  test_trap_fault(PIN_IORQ_N, true);
  test_trap_fault(PIN_RD_N, true);
  test_trap_fault(PIN_WR_N, false);
  reset_fixture();
  fail_read = 1;
  z80_io_trap_enable(NULL, NULL, NULL);
  if (setjmp(reboot_target) == 0) {
    irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
    assert(false);
  }
  assert_fail_closed();
  reset_fixture();
  uint32_t baseline = z80_io_trap_control_error_count();
  levels[PIN_IORQ_N] = levels[PIN_RD_N] = levels[PIN_WR_N] = false;
  z80_io_trap_enable(NULL, NULL, NULL);
  if (setjmp(reboot_target) == 0) {
    irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
    assert(false);
  }
  assert(z80_io_trap_control_error_count() == baseline + 1);
  assert_fail_closed();
  reset_fixture();
  levels[PIN_IORQ_N] = levels[PIN_RD_N] = false;
  release_controls = true;
  z80_io_trap_enable(NULL, NULL, NULL);
  irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
  assert(clock_running && watchdog_count == 0);
  assert_isolated();
  reset_fixture();
  levels[PIN_BUSACK_N] = false;
  z80_io_trap_enable(NULL, NULL, NULL);
  irq_callback(PIN_IORQ_N, GPIO_IRQ_EDGE_FALL);
  assert(!irq_enabled && spi_writes == 0 && spi_reads == 0);
}

int main(void) {
  test_safe_startup_and_first_write();
  test_direction_verification();
  test_bus_handshake();
  test_dma_preparation();
  test_traps();
  puts("PASS: safe startup, first SRAM write, direction, partial SPI, BUSACK and I/O fault paths");
  return 0;
}