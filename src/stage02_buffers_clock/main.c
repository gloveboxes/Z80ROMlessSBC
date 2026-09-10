#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static const uint BUFFER_INPUT_PINS[] = {
  PIN_CLK, PIN_BUSREQ_N, PIN_SPI_CS_N, PIN_SPI_SCK,
  PIN_SPI_MOSI, PIN_SRAM_WE_N, PIN_SRAM_OE_N, PIN_SRAM_CE_N,
};

static void toggle_buffer_inputs_10_hz(void) {
  z80_safe_startup();
  for (size_t index = 0;
       index < sizeof(BUFFER_INPUT_PINS) / sizeof(BUFFER_INPUT_PINS[0]);
       ++index) {
    uint pin = BUFFER_INPUT_PINS[index];
    bool safe_level = gpio_get(pin);
    printf("toggle GP%u\n", pin);
    for (unsigned int cycle = 0; cycle < 10; ++cycle) {
      gpio_put(pin, !safe_level);
      sleep_ms(50);
      gpio_put(pin, safe_level);
      sleep_ms(50);
    }
  }
  z80_safe_startup();
  printf("DONE: 10Hz toggles complete; electrical verification required\n");
}

static void set_clock(uint32_t requested_hz) {
  if (!z80_clock_set_hz(requested_hz)) {
    printf("FAIL: clock configuration rejected\n");
    return;
  }
  printf("stage=2 clock_requested=%lu clock_actual=%lu verification=unmeasured\n",
         (unsigned long)requested_hz, (unsigned long)z80_clock_get_hz());
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  printf("\nStage 2: GAL, AHCT244, and Z80 clock\n");
    printf("w=walking outputs, t=10Hz toggles, 1=1kHz, 2=100kHz, "
      "3=1MHz, s=status, x=stop\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 's') {
      printf("stage=2 clock_actual=%lu verification=unmeasured\n",
             (unsigned long)z80_clock_get_hz());
    } else if (command == 'w') {
      z80_walking_output_test(BUFFER_INPUT_PINS,
          sizeof(BUFFER_INPUT_PINS) / sizeof(BUFFER_INPUT_PINS[0]), 250);
      printf("DONE: walking outputs complete; electrical verification required\n");
    } else if (command == 't') {
      toggle_buffer_inputs_10_hz();
    } else if (command == '1') {
      set_clock(1000);
    } else if (command == '2') {
      set_clock(100000);
    } else if (command == '3') {
      set_clock(1000000);
    } else if (command == 'x') {
      z80_clock_stop();
      printf("clock stopped\n");
    }
    tight_loop_contents();
  }
}