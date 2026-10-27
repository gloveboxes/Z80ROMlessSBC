#include "z80sbc/supervisor.h"

#include "pico/stdlib.h"
#include "z80sbc/pins.h"
#include "z80sbc/banked_transfer.h"

static bool banked_memory_available;
static z80_memory_bank_driver_t bank_driver;

void z80_memory_bank_set_driver(z80_memory_bank_driver_t driver) {
  bank_driver = driver;
}

void __not_in_flash_func(z80_memory_bank_select)(unsigned bank) {
  if (bank_driver != NULL)
    bank_driver(bank & 1u);
  else
    gpio_put(PIN_MEMORY_BANK, bank & 1u);
}

void z80_memory_bank_set_available(bool available) {
  banked_memory_available = available;
}

unsigned __not_in_flash_func(z80_memory_bank_status)(void) {
  return banked_memory_available ? BANKED_ABI_STATUS | gpio_get(PIN_MEMORY_BANK) : 0;
}

void output_with_initial_level(uint pin, bool level) {
  gpio_init(pin);
  gpio_put(pin, level);
  gpio_set_dir(pin, GPIO_OUT);
}

void input_with_no_pull(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_disable_pulls(pin);
}

void z80_isolate_buses(void) {
  gpio_put(PIN_DATA_UP_OE_N, 1);
  gpio_put(PIN_DATA_DOWN_OE_N, 1);
}

void z80_safe_startup(void) {
  output_with_initial_level(PIN_DATA_UP_OE_N, 1);
  output_with_initial_level(PIN_DATA_DOWN_OE_N, 1);
  output_with_initial_level(PIN_RESET_N, 0);
  output_with_initial_level(PIN_MEMORY_BANK, 0);
  bank_driver = NULL;
  banked_memory_available = false;
  output_with_initial_level(PIN_BUSREQ_N, 1);
  output_with_initial_level(PIN_BOOT_READ_DISABLE, 1);
  output_with_initial_level(PIN_IO_RELEASE, 0);
  output_with_initial_level(PIN_CLK, 0);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin)
    input_with_no_pull(pin);
  input_with_no_pull(PIN_BUSACK_N);
  input_with_no_pull(PIN_IORQ_N);
  input_with_no_pull(PIN_RD_N);
  input_with_no_pull(PIN_WR_N);
  input_with_no_pull(PIN_PORT_A0);
  input_with_no_pull(PIN_PORT_A1);
  input_with_no_pull(PIN_PORT_A2);
  input_with_no_pull(PIN_PORT_A4);
}

void z80_walking_output_test(const uint *pins, size_t count,
                             uint32_t dwell_ms) {
  for (size_t index = 0; index < count; ++index)
    output_with_initial_level(pins[index], false);

  for (size_t active = 0; active < count; ++active) {
    for (size_t index = 0; index < count; ++index)
      gpio_put(pins[index], index == active);
    sleep_ms(dwell_ms);
  }

  z80_safe_startup();
}
