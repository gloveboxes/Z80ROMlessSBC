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
  printf("PASS: 10Hz toggles complete\n");
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  printf("\nStage 2: GAL, AHCT244, and Z80 clock\n");
    printf("w=walking outputs, t=10Hz toggles, 1=1kHz, 2=100kHz, "
      "3=1MHz, x=stop\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'w') {
      z80_walking_output_test(BUFFER_INPUT_PINS,
          sizeof(BUFFER_INPUT_PINS) / sizeof(BUFFER_INPUT_PINS[0]), 250);
      printf("PASS: walking outputs complete\n");
    } else if (command == 't') {
      toggle_buffer_inputs_10_hz();
    } else if (command == '1') {
      printf(z80_clock_set_hz(1000) ? "clock=1kHz\n" : "FAIL\n");
    } else if (command == '2') {
      printf(z80_clock_set_hz(100000) ? "clock=100kHz\n" : "FAIL\n");
    } else if (command == '3') {
      printf(z80_clock_set_hz(1000000) ? "clock=1MHz\n" : "FAIL\n");
    } else if (command == 'x') {
      z80_clock_stop();
      printf("clock stopped\n");
    }
    tight_loop_contents();
  }
}