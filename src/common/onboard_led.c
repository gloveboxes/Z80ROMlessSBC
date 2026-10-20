#include "io_service.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

static bool wireless_initialized;
static bool wireless_retry_pending;
static absolute_time_t wireless_retry_deadline;
static bool led_initialized;
static bool led_state;

bool z80_wireless_init(void) {
  if (wireless_initialized)
    return true;
  if (wireless_retry_pending && !time_reached(wireless_retry_deadline))
    return false;
  if (cyw43_arch_init() != PICO_OK) {
    cyw43_arch_deinit();
    wireless_retry_deadline = make_timeout_time_ms(5000);
    wireless_retry_pending = true;
    return false;
  }
  wireless_initialized = true;
  wireless_retry_pending = false;
  return true;
}

io_service_error_t z80_service_led(bool write, bool *state) {
  if (!z80_wireless_init())
    return IO_SERVICE_UNAVAILABLE;
  if (write || !led_initialized) {
    bool value = write && *state;
    if (cyw43_gpio_set(&cyw43_state, CYW43_WL_GPIO_LED_PIN, value) != 0)
      return IO_SERVICE_UNAVAILABLE;
    led_state = value;
    led_initialized = true;
  }
  *state = led_state;
  return IO_SERVICE_OK;
}
