#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "z80sbc/pins.h"
#include "z80sbc/sram.h"

static uint8_t banks[2][65536];
static unsigned selected_bank;
static bool bank_wired = true;
static bool bank_available;
static bool corrupt_bank_read;
#define memory banks[bank_wired ? selected_bank : 0]
void z80_memory_bank_select(unsigned bank) { selected_bank = bank & 1u; }
void z80_memory_bank_set_available(bool available) { bank_available = available; }
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
  return memory[address] ^ (selected_bank && corrupt_bank_read ? 1 : 0);
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
  /* A package-specific page proves initialization mirrors its input instead
   * of embedding another independently versioned BIOS in Pico firmware. */
  uint8_t resident_page[256];
  for (unsigned i = 0; i < sizeof(resident_page); ++i)
    resident_page[i] = (uint8_t)(i ^ 0xA5);
  resident_page[0] = 0xC3;
  z80_sram_prepare_banked_memory(resident_page);
  assert(bank_available && selected_bank == 0);
  assert(banks[0][0] == 0x5A && memcmp(banks[1], "BKM2", 4) == 0);
  assert(banks[1][0x20] == 3 && banks[1][0xFF00] == 0xC3 && banks[1][0x5F] == 0xC0);
  assert(memcmp(banks[1] + 0xFF00, resident_page, sizeof(resident_page)) == 0);
  resident_page[19] ^= 0xFF;
  resident_page[255] ^= 0xFF;
  z80_sram_prepare_banked_memory(resident_page);
  assert(bank_available && selected_bank == 0);
  assert(memcmp(banks[1] + 0xFF00, resident_page, sizeof(resident_page)) == 0);
  bank_wired = false;
  z80_sram_prepare_banked_memory(resident_page);
  assert(!bank_available && selected_bank == 0);
  bank_wired = true;
  corrupt_bank_read = true;
  z80_sram_prepare_banked_memory(resident_page);
  assert(!bank_available && selected_bank == 0);
  z80_sram_prepare_banked_memory(NULL);
  assert(!bank_available && selected_bank == 0);
  puts("PASS: injected cycles, CPU-only writes, readback, bounds and SRAM patterns");
  return 0;
}
