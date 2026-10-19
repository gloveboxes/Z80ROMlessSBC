#include <assert.h>
#include <stdio.h>
#include <string.h>

void stdio_init_all(void);
int getchar_timeout_us(unsigned timeout);
#define main bus_grant_firmware_main
#include "../../stage06_sram_dma/bus_grant_test.c"
#undef main

static bool levels[32];
static bool load_ok, run_ok, acquire_ok, release_ok;
static unsigned loads, starts, requests, releases, steps, resets, isolations;
static uint32_t last_hz;

void stdio_init_all(void) {}
int getchar_timeout_us(unsigned timeout) {
  (void)timeout;
  return -1;
}
void tight_loop_contents(void) {}
void z80_safe_startup(void) { z80_cpu_fail_closed(); }
void z80_cpu_fail_closed(void) {
  levels[PIN_RESET_N] = false;
  levels[PIN_BUSREQ_N] = true;
  levels[PIN_BOOT_READ_DISABLE] = true;
}
bool gpio_get(uint pin) { return levels[pin]; }
void gpio_put(uint pin, bool value) { levels[pin] = value; }
void z80_isolate_buses(void) { ++isolations; }
void z80_reset_with_clock_cycles(unsigned cycles, unsigned half_period_us) {
  assert((cycles == 3 || cycles == 6) && half_period_us == 1);
  levels[PIN_RESET_N] = false;
  ++resets;
}
void z80_clock_one_cycle(uint32_t half_period_us) {
  assert(half_period_us == 50000);
  assert(levels[PIN_RESET_N] && levels[PIN_BUSREQ_N]);
  assert(!levels[PIN_BOOT_READ_DISABLE]);
  ++steps;
}
bool z80_cpu_load_and_verify(const uint8_t *image, uint32_t length) {
  const uint8_t expected[] = {0, 0, 0xC3, 0, 0};
  assert(running_clock_hz == 0 && !step_image_loaded);
  assert(!levels[PIN_RESET_N]);
  assert(length == sizeof(expected) && memcmp(image, expected, length) == 0);
  ++loads;
  return load_ok;
}
bool z80_cpu_release_reset_and_run(uint32_t hz) {
  assert(hz == 10 || hz == 1000 || hz == 100000 || hz == 1000000);
  ++starts;
  last_hz = hz;
  levels[PIN_RESET_N] = run_ok;
  return run_ok;
}
bool z80_cpu_request_bus(uint32_t timeout_us) {
  assert(timeout_us == 500000 && running_clock_hz != 0);
  assert(levels[PIN_RESET_N]);
  ++requests;
  return acquire_ok;
}
bool z80_cpu_release_bus(uint32_t timeout_us) {
  assert(timeout_us == 500000 && acquire_ok);
  ++releases;
  return release_ok;
}

static void fixture(void) {
  memset(levels, 0, sizeof(levels));
  load_ok = run_ok = acquire_ok = release_ok = true;
  loads = starts = requests = releases = steps = resets = isolations = 0;
  stop_test();
}

int main(void) {
  fixture();
  assert(!single_step() && !bus_round_trip() && !reset_running_cpu());
  assert(!loads && !starts && !requests && !steps);
  const uint32_t rates[] = {10, 1000, 100000, 1000000};
  for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
    assert(load_and_start(rates[i]));
    assert(running_clock_hz == rates[i] && last_hz == rates[i]);
    assert(!step_image_loaded && levels[PIN_RESET_N]);
    assert(bus_round_trip());
    assert(reset_running_cpu());
    assert(running_clock_hz == rates[i] && levels[PIN_RESET_N]);
  }
  assert(loads == 4 && starts == 8 && requests == 4 && releases == 4);
  assert(load_for_single_step());
  assert(!running_clock_hz && step_image_loaded && !levels[PIN_RESET_N]);
  assert(!bus_round_trip());
  assert(single_step() && single_step());
  assert(steps == 2 && isolations == 1);
  stop_test();
  assert(!single_step() && !running_clock_hz && !step_image_loaded);

  for (unsigned fault = 0; fault < 5; ++fault) {
    fixture();
    assert(load_and_start(1000000));
    if (fault == 0) {
      load_ok = false;
      assert(!load_and_start(1000));
    } else if (fault == 1) {
      run_ok = false;
      assert(!load_and_start(1000));
    } else if (fault == 2) {
      run_ok = false;
      assert(!reset_running_cpu());
    } else {
      acquire_ok = fault != 3;
      release_ok = fault != 4;
      assert(!bus_round_trip());
      assert(requests == 1 && releases == (fault == 4 ? 1u : 0u));
    }
    assert(!running_clock_hz && !step_image_loaded && !levels[PIN_RESET_N]);
    assert(!bus_round_trip() && !reset_running_cpu() && !single_step());
    load_ok = run_ok = acquire_ok = release_ok = true;
    assert(load_and_start(1000) && bus_round_trip());
  }
  fixture();
  assert(load_for_single_step());
  load_ok = false;
  assert(!load_for_single_step() && !single_step() && !step_image_loaded);
  puts("Phase 6 bus grants: preserved rates/stepping/reset, round-trip and fail-closed states passed");
  return 0;
}
