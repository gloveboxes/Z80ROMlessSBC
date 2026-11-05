#include <assert.h>
#include <string.h>

#define main pico_programmer_main
#include "../flash_ATTiny85/main.c"
#undef main

static uint8_t flash[FLASH_BYTES], page_buffer[PAGE_BYTES];
static unsigned reset_high, reset_low, erases, pages, polls, attempts, commands;
static unsigned load_word;
static bool high_next, pins_input[32], reset_state;
static uint64_t now_ms;
static uint8_t signature_last, fuse_low, fuse_high;
static bool no_echo, busy_forever, busy_after_page, corrupt, spi_failure, fail_load;

void gpio_init(unsigned pin) { (void)pin; }
void gpio_put(unsigned pin, bool value) {
    assert(pin == PIN_RESET);
    reset_state = value;
    if (value) { ++reset_high; } else { ++reset_low; }
}
void gpio_set_dir(unsigned pin, bool output) { pins_input[pin] = !output; }
void gpio_set_function(unsigned pin, unsigned function) {
    (void)pin; (void)function;
}
void sleep_ms(unsigned ms) { now_ms += ms; }
absolute_time_t make_timeout_time_ms(unsigned ms) { return now_ms + ms; }
bool time_reached(absolute_time_t deadline) { return now_ms >= deadline; }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) { return true; }
int getchar_timeout_us(unsigned us) { (void)us; return 'P'; }
void tight_loop_contents(void) {}
unsigned spi_init(void *spi, unsigned baud) { (void)spi; return baud; }
void spi_set_format(void *spi, unsigned bits, unsigned polarity,
                    unsigned phase, unsigned order) {
    (void)spi; (void)bits; (void)polarity; (void)phase; (void)order;
}

int spi_write_read_blocking(void *spi, const uint8_t *tx, uint8_t *rx, size_t size) {
    (void)spi;
    assert(size == 4 && !reset_state);
    ++commands;
    memset(rx, 0, size);
    if (spi_failure) { return 0; }
    if (fail_load && tx[0] == 0x40) { return 0; }
    unsigned word = ((unsigned)tx[1] << 8) | tx[2];
    switch (tx[0]) {
    case 0xac:
        if (tx[1] == 0x53) {
            ++attempts;
            rx[2] = no_echo ? 0 : 0x53;
        } else {
            assert(tx[1] == 0x80);
            ++erases;
            memset(flash, 0xff, sizeof(flash));
        }
        break;
    case 0x30:
        assert(tx[2] <= 2);
        rx[3] = tx[2] == 0 ? 0x1e : tx[2] == 1 ? 0x93 : signature_last;
        break;
    case 0x50: rx[3] = fuse_low; break;
    case 0x58: rx[3] = fuse_high; break;
    case 0xf0:
        ++polls;
        rx[3] = busy_forever || (busy_after_page && pages > 0) || polls % 2 == 1;
        break;
    case 0x40:
        assert(!high_next && word == load_word && word < PAGE_BYTES / 2);
        page_buffer[word * 2] = tx[3];
        high_next = true;
        break;
    case 0x48:
        assert(high_next && word == load_word && word < PAGE_BYTES / 2);
        page_buffer[word * 2 + 1] = tx[3];
        high_next = false;
        ++load_word;
        break;
    case 0x4c:
        assert(load_word == PAGE_BYTES / 2 && !high_next);
        assert(word * 2 == pages * PAGE_BYTES);
        assert(word * 2 + PAGE_BYTES <= FLASH_BYTES);
        memcpy(flash + word * 2, page_buffer, PAGE_BYTES);
        ++pages;
        load_word = 0;
        break;
    case 0x20:
    case 0x28: {
        unsigned offset = word * 2 + (tx[0] == 0x28);
        assert(offset < FLASH_BYTES);
        rx[3] = flash[offset] ^ (corrupt && offset == 3 ? 1u : 0u);
        break;
    }
    default: assert(false);
    }
    return (int)size;
}

static void setup(void) {
    memset(flash, 0xa5, sizeof(flash));
    memset(pins_input, 0, sizeof(pins_input));
    reset_high = reset_low = erases = pages = polls = attempts = commands = 0;
    load_word = 0;
    high_next = reset_state = false;
    now_ms = 0;
    signature_last = 0x0b;
    fuse_low = 0x62;
    fuse_high = 0xdf;
    no_echo = busy_forever = busy_after_page = corrupt = spi_failure = fail_load = false;
}

static void check_released(void) {
    assert(reset_state && pins_input[PIN_SCK] && pins_input[PIN_MOSI] &&
           pins_input[PIN_MISO]);
}

int main(void) {
    uint8_t image[FLASH_BYTES];
    for (size_t i = 0; i < sizeof(image); ++i) { image[i] = (uint8_t)(i * 37); }
    const size_t lengths[] = {1, 32, 63, 64, 65, 529, FLASH_BYTES};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        setup();
        size_t length = lengths[i];
        assert(program_target(image, length));
        assert(erases == 1 && pages == (length + PAGE_BYTES - 1) / PAGE_BYTES);
        assert(memcmp(image, flash, length) == 0);
        for (size_t j = length; j < FLASH_BYTES; ++j) { assert(flash[j] == 0xff); }
        check_released();
    }
    setup(); no_echo = true;
    assert(!program_target(image, 2) && erases == 0 && attempts == 3);
    check_released();
    setup(); signature_last = 0x0a;
    assert(!program_target(image, 2) && erases == 0);
    check_released();
    setup(); fuse_low = 0x63;
    assert(!program_target(image, 2) && erases == 0);
    setup(); fuse_high = 0x9f;
    assert(!program_target(image, 2) && erases == 0);
    setup(); fuse_low &= (uint8_t)~0x40;
    assert(!program_target(image, 2) && erases == 0); /* CKOUT steals PB4. */
    setup(); fuse_high &= (uint8_t)~0x10;
    assert(!program_target(image, 2) && erases == 0); /* Watchdog cannot be disabled. */
    setup(); busy_forever = true;
    assert(!program_target(image, 2) && erases == 1 && pages == 0);
    check_released();
    setup(); busy_after_page = true;
    assert(!program_target(image, 65) && erases == 1 && pages == 1);
    check_released();
    setup(); fail_load = true;
    assert(!program_target(image, 65) && erases == 1 && pages == 0);
    check_released();
    setup(); corrupt = true;
    assert(!program_target(image, 65));
    check_released();
    setup(); spi_failure = true;
    assert(!program_target(image, 2) && erases == 0);
    check_released();
    setup();
    assert(!program_target(image, 0) && commands == 0);
    check_released();
    setup();
    assert(!program_target(image, FLASH_BYTES + 1) && commands == 0);
    setup();
    assert(!program_target(NULL, 2) && commands == 0);
    puts("ISP page boundaries, full image verification and failure tests passed.");
    return 0;
}
