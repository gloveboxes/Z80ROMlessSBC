#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static const uint8_t CPU_TEST_PROGRAM[] = {
  0x00,             /* NOP */
  0x00,             /* NOP */
  0xC3, 0x00, 0x00, /* JP 0000h */
};

static bool step_image_loaded;
static uint32_t running_clock_hz;

static bool load_and_start(uint32_t clock_hz) {
  step_image_loaded = false;
  if (!z80_cpu_load_and_verify(CPU_TEST_PROGRAM, sizeof(CPU_TEST_PROGRAM)))
    return false;
  if (!z80_cpu_release_reset_and_run(clock_hz))
    return false;
  running_clock_hz = clock_hz;
  return true;
}

static bool load_for_single_step(void) {
  step_image_loaded = z80_cpu_load_and_verify(
      CPU_TEST_PROGRAM, sizeof(CPU_TEST_PROGRAM));
  running_clock_hz = 0;
  return step_image_loaded;
}

static bool single_step(void) {
  if (!step_image_loaded)
    return false;
  if (!gpio_get(PIN_RESET_N)) {
    gpio_put(PIN_SRAM_CE_N, 1);
    gpio_put(PIN_SRAM_OE_N, 1);
    gpio_put(PIN_SRAM_WE_N, 1);
    z80_isolate_buses();
    gpio_put(PIN_BUSREQ_N, 1);
    gpio_put(PIN_RESET_N, 1);
  }
  z80_clock_one_cycle(50000);
  return true;
}

static bool reset_running_cpu(void) {
  if (running_clock_hz == 0)
    return false;
  z80_reset_with_clock_cycles(3, 1);
  return z80_cpu_release_reset_and_run(running_clock_hz);
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 7: Z80 CPU execution and bus ownership\n");
    printf("l=load for stepping, s=single 10Hz-equivalent cycle, "
      "0=run 10Hz, 1=run 1kHz, 2=run 100kHz, 3/r=run 1MHz, "
      "q=BUSREQ round-trip, z=reset/restart, x=fail closed\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'l') {
      printf(load_for_single_step() ? "PASS: image loaded; RESET# held\n"
                                    : "FAIL: image load\n");
    } else if (command == 's') {
      printf(single_step() ? "step complete\n"
                           : "FAIL: use l before stepping\n");
    } else if (command == '0') {
      printf(load_and_start(10) ? "PASS: CPU running at 10Hz\n"
                                : "FAIL: load/start\n");
    } else if (command == '1') {
      printf(load_and_start(1000) ? "PASS: CPU running at 1kHz\n"
                                  : "FAIL: load/start\n");
    } else if (command == '2') {
      printf(load_and_start(100000) ? "PASS: CPU running at 100kHz\n"
                                    : "FAIL: load/start\n");
    } else if (command == '3' || command == 'r') {
      printf(load_and_start(1000000) ? "PASS: CPU running at 1MHz\n"
                                     : "FAIL: load/start\n");
    } else if (command == 'q') {
      bool acquired = running_clock_hz != 0 &&
                      z80_cpu_request_bus(500000);
      bool released = acquired && z80_cpu_release_bus(500000);
      printf(acquired && released ? "PASS: bus round-trip\n"
                                  : "FAIL: bus round-trip\n");
    } else if (command == 'z') {
      printf(reset_running_cpu() ? "PASS: reset and restart\n"
                                 : "FAIL: start a run mode first\n");
    } else if (command == 'x') {
      z80_cpu_fail_closed();
      step_image_loaded = false;
      running_clock_hz = 0;
      printf("CPU held in reset\n");
    }
    tight_loop_contents();
  }
}