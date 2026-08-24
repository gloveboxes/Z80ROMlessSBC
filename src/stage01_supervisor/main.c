#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

static const uint TEST_OUTPUT_PINS[] = {
  PIN_CLK,
  PIN_RESET_N,
  PIN_BUSREQ_N,
  PIN_SRAM_CE_N,
  PIN_DATA_DIR,
  PIN_DATA_ENABLE,
  PIN_ADDR_ENABLE,
  PIN_SPI_SCK,
  PIN_SPI_MOSI,
  PIN_SPI_CS_N,
  PIN_SRAM_WE_N,
  PIN_SRAM_OE_N,
};

static void print_status(void) {
  printf("BUSACK#=%u IORQ#=%u RD#=%u WR#=%u MISO=%u\n",
         gpio_get(PIN_BUSACK_N), gpio_get(PIN_IORQ_N),
         gpio_get(PIN_RD_N), gpio_get(PIN_WR_N),
         gpio_get(PIN_SPI_MISO));
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();

  printf("\nZ80 ROMless SBC - Stage 1 supervisor\n");
  printf("w: walking output test (Phase 1 hardware only)\n");
  printf("s: sample input status\n");

  while (true) {
    int command = getchar_timeout_us(0);
    if (command == 'w') {
      printf("walking %u outputs\n",
             (unsigned)(sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0])));
      z80_walking_output_test(TEST_OUTPUT_PINS,
                              sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0]),
                              250);
      printf("PASS: safe levels restored\n");
    } else if (command == 's') {
      print_status();
    }
    tight_loop_contents();
  }
}