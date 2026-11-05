#include <stdio.h>
#include <stdint.h>

#include "hardware/spi.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "attiny85_firmware.h"

#define PIN_MISO 16u
#define PIN_RESET 17u
#define PIN_SCK 18u
#define PIN_MOSI 19u
#define ISP_BAUD 10000u
#define FLASH_BYTES 8192u
#define PAGE_BYTES 64u /* ATtiny85: 32 words, not 32 bytes. */

static bool transfer_ok;

static uint8_t instruction(uint8_t opcode, uint8_t high, uint8_t low,
                           uint8_t data, uint8_t *echo) {
    uint8_t tx[4] = {opcode, high, low, data};
    uint8_t rx[4] = {0};
    if (spi_write_read_blocking(spi0, tx, rx, sizeof(tx)) != (int)sizeof(tx)) {
        transfer_ok = false;
    }
    if (echo) {
        *echo = rx[2];
    }
    return rx[3];
}

static bool wait_ready(const char *operation) {
    absolute_time_t deadline = make_timeout_time_ms(100);
    do {
        uint8_t busy = instruction(0xf0, 0, 0, 0, NULL);
        if (!transfer_ok) {
            printf("ERROR: SPI transfer failed during %s.\n", operation);
            return false;
        }
        if (!(busy & 1u)) {
            return true;
        }
        sleep_ms(1);
    } while (!time_reached(deadline));
    printf("ERROR: ATtiny85 timed out during %s.\n", operation);
    return false;
}

static bool enter_programming(void) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        gpio_put(PIN_RESET, 1);
        sleep_ms(1);
        gpio_put(PIN_RESET, 0);
        sleep_ms(30);
        uint8_t echo;
        instruction(0xac, 0x53, 0, 0, &echo);
        if (!transfer_ok) {
            printf("ERROR: SPI programming-enable transfer failed.\n");
            return false;
        }
        if (echo == 0x53) {
            return true;
        }
        printf("Programming enable attempt %u: expected 53, received %02x.\n",
               attempt + 1, echo);
    }
    printf("ERROR: No ISP response. Check power, wiring, RESET and clock fuses.\n");
    return false;
}

static bool identify_target(void) {
    uint8_t signature[3];
    for (uint8_t i = 0; i < 3; ++i) {
        signature[i] = instruction(0x30, 0, i, 0, NULL);
    }
    if (!transfer_ok) {
        printf("ERROR: SPI signature read failed.\n");
        return false;
    }
    printf("Signature: %02x %02x %02x\n",
           signature[0], signature[1], signature[2]);
    if (signature[0] != 0x1e || signature[1] != 0x93 || signature[2] != 0x0b) {
        printf("ERROR: Target is not an ATtiny85; nothing erased.\n");
        return false;
    }
    uint8_t low = instruction(0x50, 0, 0, 0, NULL);
    uint8_t high = instruction(0x58, 0x08, 0, 0, NULL);
    if (!transfer_ok) {
        printf("ERROR: SPI fuse read failed.\n");
        return false;
    }
    printf("Fuses: low=%02x high=%02x (not modified).\n", low, high);
    if ((low & 0x4f) != 0x42 || (high & 0xf0) != 0xd0) {
        printf("ERROR: Requires internal 8 MHz RC, RESET enabled, SPI enabled "
               "and debugWIRE/CKOUT/always-on watchdog disabled; nothing erased.\n");
        return false;
    }
    return true;
}

static bool write_flash(const uint8_t *firmware, size_t size) {
    for (size_t page = 0; page < size; page += PAGE_BYTES) {
        for (uint8_t word = 0; word < PAGE_BYTES / 2; ++word) {
            size_t offset = page + word * 2u;
            uint8_t low = offset < size ? firmware[offset] : 0xff;
            uint8_t high = offset + 1 < size ? firmware[offset + 1] : 0xff;
            instruction(0x40, 0, word, low, NULL);
            if (transfer_ok) {
                instruction(0x48, 0, word, high, NULL);
            }
            if (!transfer_ok) {
                printf("ERROR: SPI page load failed at byte 0x%04x; "
                       "page not committed.\n", (unsigned)offset);
                return false;
            }
        }
        uint16_t address = (uint16_t)(page / 2);
        instruction(0x4c, address >> 8, address & 0xff, 0, NULL);
        if (!wait_ready("flash page write")) {
            return false;
        }
        printf("Written page %u/%u.\n", (unsigned)(page / PAGE_BYTES + 1),
               (unsigned)((size + PAGE_BYTES - 1) / PAGE_BYTES));
    }
    return true;
}

static bool verify_flash(const uint8_t *firmware, size_t size) {
    for (size_t offset = 0; offset < FLASH_BYTES; ++offset) {
        uint16_t word = (uint16_t)(offset / 2);
        uint8_t actual = instruction((offset & 1u) ? 0x28 : 0x20,
                                     word >> 8, word & 0xff, 0, NULL);
        if (!transfer_ok) {
            printf("ERROR: SPI flash read failed at 0x%04x.\n", (unsigned)offset);
            return false;
        }
        uint8_t expected = offset < size ? firmware[offset] : 0xff;
        if (actual != expected) {
            printf("ERROR: Verify 0x%04x: expected %02x, received %02x.\n",
                   (unsigned)offset, expected, actual);
            return false;
        }
    }
    return true;
}

static bool program_target(const uint8_t *firmware, size_t size) {
    transfer_ok = true;
    bool success = false;
    if (!firmware || size == 0 || size > FLASH_BYTES) {
        printf("ERROR: Invalid firmware size: %u.\n", (unsigned)size);
        goto done;
    }
    if (!enter_programming() || !identify_target()) {
        goto done;
    }
    printf("Erasing flash (EEPROM is also erased unless EESAVE is programmed).\n");
    instruction(0xac, 0x80, 0, 0, NULL);
    if (!wait_ready("chip erase") || !write_flash(firmware, size)) {
        goto done;
    }
    printf("Verifying all 8192 flash bytes, including erased padding...\n");
    success = verify_flash(firmware, size);
done:
    /* Isolate shared ISP pins before the newly flashed USI/buzzer starts. */
    gpio_set_function(PIN_SCK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SIO);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_SCK, GPIO_IN);
    gpio_set_dir(PIN_MOSI, GPIO_IN);
    gpio_set_dir(PIN_MISO, GPIO_IN);
    gpio_put(PIN_RESET, 1);
    printf("%s RESET released high.\n",
           success ? "SUCCESS: Flash verified." : "FAILED: Flash not verified.");
    return success;
}

int main(void) {
    /* Hold the target stopped before waiting for a USB terminal. */
    gpio_init(PIN_RESET);
    gpio_put(PIN_RESET, 0);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_init(PIN_SCK);
    gpio_put(PIN_SCK, 0);
    gpio_set_dir(PIN_SCK, GPIO_OUT);
    gpio_init(PIN_MOSI);
    gpio_put(PIN_MOSI, 0);
    gpio_set_dir(PIN_MOSI, GPIO_OUT);
    gpio_init(PIN_MISO);
    gpio_set_dir(PIN_MISO, GPIO_IN);
    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    printf("\nStage 11 ATtiny85 ISP: %u-byte compiled image.\n",
           (unsigned)sizeof(attiny85_firmware));
    printf("SPI0: MISO GP16, RESET GP17, SCK GP18, MOSI GP19.\n");
    printf("WARNING: This erases the target. Type P to program once.\n");
    int input;
    do {
        input = getchar_timeout_us(100000);
    } while (input != 'P');

    unsigned baud = spi_init(spi0, ISP_BAUD);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    printf("ISP clock: %u Hz.\n", baud);
    program_target(attiny85_firmware, sizeof(attiny85_firmware));
    for (;;) {
        tight_loop_contents();
    }
}
