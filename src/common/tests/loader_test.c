#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "z80sbc/pins.h"
#include "z80sbc/sram.h"

static uint8_t memory[65536];
static uint16_t address;
static unsigned cycle;
static unsigned reset_tail;
static uint8_t opcode;
static uint8_t immediate;
static uint8_t driven;
static bool read_disabled;
static bool driving;
static bool receiving;
static bool reset_n;
static unsigned writes;

void gpio_put(uint pin, bool value) {
  if (pin == PIN_BOOT_READ_DISABLE) {
    assert(value || !driving);
    read_disabled = value;
  } else if (pin == PIN_RESET_N) {
    reset_n = value;
  } else {
    assert(pin == PIN_BUSREQ_N && value);
  }
}
void busy_wait_us_32(uint32_t delay) { assert(delay > 0); }
void z80_data_bus_isolate(void) { driving = receiving = false; }
void z80_isolate_buses(void) { z80_data_bus_isolate(); }
void z80_data_bus_drive(uint8_t value) {
  assert(read_disabled && !receiving);
  driven = value;
  driving = true;
}
void z80_data_bus_prepare_input(void) {
  assert(!driving);
  receiving = true;
}
uint8_t z80_data_bus_sample(void) {
  assert(receiving && !driving && !read_disabled);
  assert(opcode == 0x7E && cycle == 6);
  return memory[address];
}
void z80_reset_with_clock_cycles(unsigned count, uint32_t half_period) {
  assert(count >= 3 && half_period > 0);
  reset_n = false;
  cycle = 0;
  reset_tail = 2;
}
void z80_clock_one_cycle(uint32_t half_period) {
  assert(half_period > 0 && reset_n);
  if (reset_tail) {
    --reset_tail;
    return;
  }
  ++cycle;
  if (cycle == 3) {
    assert(driving && read_disabled);
    opcode = driven;
    assert(opcode == 0x21 || opcode == 0x36 || opcode == 0x7E);
  }
  if (opcode == 0x21 && cycle == 7) {
    assert(driving && read_disabled);
    immediate = driven;
  }
  if (opcode == 0x21 && cycle == 10) {
    assert(driving && read_disabled);
    address = immediate | ((uint16_t)driven << 8);
    cycle = 0;
  }
  if (opcode == 0x36 && cycle == 7) {
    assert(driving && read_disabled);
    immediate = driven;
  }
  if (opcode == 0x36 && cycle == 10) {
    assert(!driving && read_disabled);
    memory[address] = immediate;
    ++writes;
    cycle = 0;
  }
  if (opcode == 0x7E && cycle == 7) {
    assert(receiving && !driving && !read_disabled);
    cycle = 0;
  }
}

int main(void) {
  assert(z80_sram_prepare_loader());
  assert(z80_sram_write_byte(0x1234, 0xA5));
  assert(memory[0x1234] == 0xA5 && writes == 1);
  uint8_t actual;
  assert(z80_sram_read_byte(0x1234, &actual) && actual == 0xA5);
  assert(!z80_sram_read_byte(0, NULL));
  const uint8_t payload[] = {0x00, 0xFF, 0x21, 0x36, 0x7E, 0xA5};
  assert(z80_sram_load(0xFFFA, payload, sizeof(payload)));
  assert(z80_sram_verify(0xFFFA, payload, sizeof(payload)));
  memory[0xFFFF] ^= 1;
  assert(!z80_sram_verify(0xFFFA, payload, sizeof(payload)));
  assert(!z80_sram_load(0xFFFF, payload, 2));
  assert(!z80_sram_verify(0, NULL, 1));
  assert(z80_sram_pattern_test(false));
  assert(z80_sram_pattern_test(true));
  assert(z80_sram_march_test());
  assert(cycle == 0 && !driving && !receiving && read_disabled);
  puts("PASS: injected cycles, CPU-only writes, readback, bounds and SRAM patterns");
  return 0;
}