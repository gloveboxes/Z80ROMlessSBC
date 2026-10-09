#include "z80sbc/cpu.h"

#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"

void z80_cpu_fail_closed(void) {
  z80_isolate_buses();
  gpio_put(PIN_RESET_N, 0);
  gpio_put(PIN_BUSREQ_N, 1);
  gpio_put(PIN_BOOT_READ_DISABLE, 1);
  gpio_put(PIN_IO_RELEASE, 0);
  z80_clock_stop();
}

bool z80_cpu_prepare_loader(void) {
  z80_cpu_fail_closed();
  z80_reset_with_clock_cycles(3, 1);
  if (!gpio_get(PIN_BUSACK_N) || !gpio_get(PIN_IORQ_N) ||
      !gpio_get(PIN_RD_N) || !gpio_get(PIN_WR_N))
    return false;
  return z80_sram_prepare_loader();
}

bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length) {
  if (image == NULL || length == 0 || length > 65536u ||
      !z80_cpu_prepare_loader())
    return false;
  bool loaded = z80_sram_load(0, image, length) &&
                z80_sram_verify(0, image, length);
  if (!loaded)
    z80_cpu_fail_closed();
  return loaded;
}

bool z80_cpu_release_reset_and_run(uint32_t clock_hz) {
  if (clock_hz < Z80_CLOCK_MIN_HZ || clock_hz > Z80_CLOCK_MAX_HZ) {
    z80_cpu_fail_closed();
    return false;
  }
  z80_isolate_buses();
  z80_reset_with_clock_cycles(6, 1);
  gpio_put(PIN_BOOT_READ_DISABLE, 0);
  gpio_put(PIN_IO_RELEASE, 0);
  gpio_put(PIN_BUSREQ_N, 1);
  gpio_put(PIN_RESET_N, 1);
  busy_wait_us_32(1);
  if (!z80_clock_set_hz(clock_hz)) {
    z80_cpu_fail_closed();
    return false;
  }
  return true;
}

bool z80_cpu_request_bus(uint32_t timeout_us) {
  absolute_time_t deadline = make_timeout_time_us(timeout_us);
  z80_isolate_buses();
  gpio_put(PIN_BUSREQ_N, 0);
  while (gpio_get(PIN_BUSACK_N)) {
    if (time_reached(deadline)) {
      gpio_put(PIN_BUSREQ_N, 1);
      return false;
    }
    tight_loop_contents();
  }
  return true;
}

bool z80_cpu_release_bus(uint32_t timeout_us) {
  absolute_time_t deadline = make_timeout_time_us(timeout_us);
  z80_isolate_buses();
  gpio_put(PIN_BUSREQ_N, 1);
  while (!gpio_get(PIN_BUSACK_N)) {
    if (time_reached(deadline)) {
      z80_cpu_fail_closed();
      return false;
    }
    tight_loop_contents();
  }
  return true;
}