#include "z80sbc/sram.h"

#include <stddef.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/banked_transfer.h"
#include "z80sbc/banked_allocator.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static bool report_failure(uint16_t address, uint8_t expected,
                           uint8_t actual) {
  printf("FAIL %04x expected=%02x actual=%02x\n", address, expected, actual);
  z80_isolate_buses();
  return false;
}

bool z80_sram_prepare_loader(void) {
  z80_isolate_buses();
  gpio_put(PIN_BOOT_READ_DISABLE, 1);
  gpio_put(PIN_BUSREQ_N, 1);
  z80_reset_with_clock_cycles(6, 1);
  z80_memory_bank_select(0);
  gpio_put(PIN_RESET_N, 1);
  z80_clock_one_cycle(1);
  z80_clock_one_cycle(1);
  return true;
}

enum { BANK_RUNTIME_BYTES = Z80_SRAM_BANK_RUNTIME_BYTES,
       BANK_LOAD_BYTES = Z80_SRAM_BANK_INIT_BYTES };

/* Offsets count initialized bytes, skipping the application heap. */
static uint16_t bank_load_address(unsigned offset) {
  return offset < BANK_RUNTIME_BYTES ? (uint16_t)offset :
      (uint16_t)(BANKED_CODE_ADDRESS + offset - BANK_RUNTIME_BYTES);
}

static uint8_t bank_load_value(unsigned offset, const uint8_t *resident_page) {
  if (offset >= BANK_RUNTIME_BYTES)
    return resident_page[offset - BANK_RUNTIME_BYTES];
  if (offset < 8)
    return (const uint8_t[]){'B', 'K', 'M', '3', 'B', 'E', 'X', '1'}[offset];
  if (offset == 0x10)
    return 8; /* Initially one free extent, starting at block 8. */
  if (offset == 0x15 || offset == 0x108)
    return 247; /* Cached F700h free bytes and 247-block extent. */
  if (offset >= BANKED_ALLOCATOR_ADDRESS &&
      offset - BANKED_ALLOCATOR_ADDRESS < sizeof(banked_allocator_code))
    return banked_allocator_code[offset - BANKED_ALLOCATOR_ADDRESS];
  return 0;
}

static void bank_load_progress(const char *phase, unsigned completed) {
  printf("[boot] bank 1 SRAM %s: %u/%u\n", phase, completed,
         (unsigned)BANK_LOAD_BYTES);
}

void z80_sram_prepare_banked_memory(const uint8_t *resident_page) {
  z80_memory_bank_set_available(false);
  z80_memory_bank_select(0);
  if (resident_page == NULL) {
    printf("[boot] bank 1 SRAM skipped: no resident page\n");
    return;
  }
  /* Bank 0 is already loaded and verified. Preserve its reset-vector byte
   * across the A16 probe, including the aliased-bank failure path. */
  uint8_t original = 0, actual = 0;
  if (!z80_sram_read_byte(0, &original)) {
    printf("[boot] bank 1 SRAM unavailable: cannot save bank 0 probe byte\n");
    return;
  }
  z80_sram_write_byte(0, 0x5A);
  z80_memory_bank_select(1);
  z80_sram_write_byte(0, 0xA5);
  z80_memory_bank_select(0);
  bool isolated = z80_sram_read_byte(0, &actual) && actual == 0x5A;
  if (!z80_sram_write_byte(0, original) ||
      !z80_sram_read_byte(0, &actual) || actual != original) {
    printf("[boot] bank 1 SRAM unavailable: bank 0 probe-byte restore failed\n");
    return;
  }
  if (!isolated) {
    printf("[boot] bank 1 SRAM unavailable: A16 isolation probe failed\n");
    return; /* Aliased SRAM: retain 64 KiB operation. */
  }
  z80_memory_bank_select(1);
  bool valid = false;
  /* Write the complete runtime and mirrored resident page before readback,
   * just as the base-bank loader separates its load and verify passes. */
  for (unsigned offset = 0; offset < BANK_LOAD_BYTES; ++offset) {
    if (offset % 1024u == 0)
      bank_load_progress("load", offset);
    uint16_t address = bank_load_address(offset);
    uint8_t expected = bank_load_value(offset, resident_page);
    if (!z80_sram_write_byte(address, expected)) {
      printf("[boot] bank 1 SRAM load FAIL: address=%04x expected=%02x\n",
             address, expected);
      goto finish_bank;
    }
  }
  bank_load_progress("load", BANK_LOAD_BYTES);
  for (unsigned offset = 0; offset < BANK_LOAD_BYTES; ++offset) {
    if (offset % 1024u == 0)
      bank_load_progress("verify", offset);
    uint16_t address = bank_load_address(offset);
    uint8_t expected = bank_load_value(offset, resident_page);
    if (!z80_sram_read_byte(address, &actual)) {
      printf("[boot] bank 1 SRAM verify FAIL: address=%04x read failed\n", address);
      goto finish_bank;
    }
    if (actual != expected) {
      printf("[boot] bank 1 SRAM verify FAIL: address=%04x expected=%02x actual=%02x\n",
             address, expected, actual);
      goto finish_bank;
    }
  }
  bank_load_progress("verify", BANK_LOAD_BYTES);
  valid = true;
finish_bank:
  z80_memory_bank_select(0);
  z80_memory_bank_set_available(valid);
  if (valid)
    printf("[boot] bank 1 SRAM verified: extent allocator and resident page ready\n");
  else
    printf("[boot] bank 1 SRAM disabled: load/verify failed\n");
}

static void pulse_clock(unsigned cycles) {
  for (unsigned cycle = 0; cycle < cycles; ++cycle)
    z80_clock_one_cycle(1);
}

static void inject_read(uint8_t value, bool opcode) {
  pulse_clock(1);
  gpio_put(PIN_BOOT_READ_DISABLE, 1);
  busy_wait_us_32(1);
  z80_data_bus_drive(value);
  pulse_clock(2);
  z80_data_bus_isolate();
  if (opcode)
    pulse_clock(1);
}

static void load_hl(uint16_t address) {
  inject_read(0x21, true);
  inject_read((uint8_t)address, false);
  inject_read((uint8_t)(address >> 8), false);
}

bool z80_sram_write_byte(uint16_t address, uint8_t value) {
  load_hl(address);
  inject_read(0x36, true);
  inject_read(value, false);
  pulse_clock(3);
  return true;
}

bool z80_sram_read_byte(uint16_t address, uint8_t *value) {
  if (value == NULL)
    return false;

  load_hl(address);
  inject_read(0x7E, true);
  z80_data_bus_prepare_input();
  gpio_put(PIN_BOOT_READ_DISABLE, 0);
  pulse_clock(2);
  busy_wait_us_32(1);
  *value = z80_data_bus_sample();
  pulse_clock(1);
  z80_data_bus_isolate();
  gpio_put(PIN_BOOT_READ_DISABLE, 1);
  return true;
}

bool z80_sram_load(uint16_t address, const uint8_t *data, uint32_t length) {
  if (data == NULL || length > 65536u - address)
    return false;
  for (uint32_t index = 0; index < length; ++index) {
    if (!z80_sram_write_byte((uint16_t)(address + index), data[index]))
      return false;
  }
  z80_isolate_buses();
  return true;
}

bool z80_sram_verify(uint16_t address, const uint8_t *data, uint32_t length) {
  if (data == NULL || length > 65536u - address)
    return false;
  for (uint32_t index = 0; index < length; ++index) {
    uint8_t actual = 0;
    if (!z80_sram_read_byte((uint16_t)(address + index), &actual) ||
        actual != data[index])
      return report_failure((uint16_t)(address + index), data[index], actual);
  }
  z80_isolate_buses();
  return true;
}

bool z80_sram_pattern_test(bool complement) {
  for (uint32_t address = 0; address < 65536u; ++address) {
    uint8_t expected = (uint8_t)address ^ (uint8_t)(address >> 8);
    if (complement)
      expected = (uint8_t)~expected;
    if (!z80_sram_write_byte((uint16_t)address, expected))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    uint8_t expected = (uint8_t)address ^ (uint8_t)(address >> 8);
    uint8_t actual = 0;
    if (complement)
      expected = (uint8_t)~expected;
    if (!z80_sram_read_byte((uint16_t)address, &actual) || actual != expected)
      return report_failure((uint16_t)address, expected, actual);
  }
  z80_isolate_buses();
  return true;
}

static bool march_read_write(uint16_t address, uint8_t expected,
                             uint8_t replacement) {
  uint8_t actual = 0;
  if (!z80_sram_read_byte(address, &actual) || actual != expected)
    return report_failure(address, expected, actual);
  return z80_sram_write_byte(address, replacement);
}

bool z80_sram_march_test(void) {
  for (uint32_t address = 0; address < 65536u; ++address) {
    if (!z80_sram_write_byte((uint16_t)address, 0x00))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    if (!march_read_write((uint16_t)address, 0x00, 0xFF))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    if (!march_read_write((uint16_t)address, 0xFF, 0x00))
      return false;
  }
  for (uint32_t address = 65536u; address-- > 0;) {
    if (!march_read_write((uint16_t)address, 0x00, 0xFF))
      return false;
  }
  for (uint32_t address = 65536u; address-- > 0;) {
    if (!march_read_write((uint16_t)address, 0xFF, 0x00))
      return false;
  }
  for (uint32_t address = 0; address < 65536u; ++address) {
    uint8_t actual = 0;
    if (!z80_sram_read_byte((uint16_t)address, &actual) || actual != 0x00)
      return report_failure((uint16_t)address, 0x00, actual);
  }
  z80_isolate_buses();
  return true;
}
