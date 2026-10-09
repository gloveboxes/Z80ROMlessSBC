#include <stdio.h>
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static const uint BUFFER_INPUT_PINS[] = {PIN_CLK, PIN_BUSREQ_N, PIN_BOOT_READ_DISABLE};
static const uint BUFFER_OUTPUT_CONTACTS[] = {18, 16, 14};
static const uint BUFFER_INPUT_CONTACTS[] = {2, 4, 6};
static int held_step = -1;

static void restore_test_outputs(void) {
  z80_clock_stop();
  z80_safe_startup();
  held_step = -1;
}

static void step_buffer_output(void) {
  int next_step = held_step + 1;
  restore_test_outputs();
  if (next_step >= 6) {
    printf("DONE: safe levels restored\n");
    return;
  }
  held_step = next_step;
  unsigned index = (unsigned)held_step / 2;
  bool high = held_step % 2 != 0;
  gpio_put(BUFFER_INPUT_PINS[index], high);
  printf("GP%u -> AHCT244 input %u -> output %u: HELD %s; verification=unmeasured\n",
         BUFFER_INPUT_PINS[index], BUFFER_INPUT_CONTACTS[index],
         BUFFER_OUTPUT_CONTACTS[index], high ? "HIGH" : "LOW");
}

int main(void) {
  restore_test_outputs();
  stdio_init_all();
  printf("\nStage 2: AHCT244 clock and boot-read inhibit; CPU/SRAM absent\n");
  printf("d=held LOW/HIGH steps, 1=1kHz, 2=100kHz, 3=1MHz, x=safe restore\n");
  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'd') {
      step_buffer_output();
    } else if (command >= '1' && command <= '3') {
      restore_test_outputs();
      uint32_t hz = command == '1' ? 1000 : command == '2' ? 100000 : 1000000;
      printf(z80_clock_set_hz(hz) ? "clock_actual=%lu verification=unmeasured\n"
                                : "FAIL: clock configuration %lu\n",
             (unsigned long)z80_clock_get_hz());
    } else if (command == 'x') {
      restore_test_outputs();
    }
    tight_loop_contents();
  }
}