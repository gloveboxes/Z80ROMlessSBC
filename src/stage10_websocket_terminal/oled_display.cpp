#include "oled_display.h"
#include "display_i2c.h"
#include "../common/io_service.h"
#include "pico/stdlib.h"
#include "z80sbc/ram_timer.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace oled_display {
namespace {
enum class State : uint8_t {
  uninitialized, scanning, initialize, ready, page, enable, failed,
};
enum class Failure : uint8_t { none, no_device, nack, bus };
constexpr unsigned WIDTH = 128;
constexpr unsigned PAGES = 4;  // 128x32: four 8-pixel pages.
constexpr uint8_t ALL_PAGES = (1u << PAGES) - 1;
constexpr unsigned IP_Y = 2;       // Pages 0-1.
constexpr unsigned UPTIME_Y = 20;  // Pages 2-3.
constexpr unsigned FIELD_ROWS = 10;  // Scale-2 glyphs are 10 pixels tall.
// 2x2 dot in the bottom-right corner: rows 30-31 of page 3, below the uptime field.
constexpr unsigned HEARTBEAT_COLUMN = WIDTH - 2;
constexpr uint8_t HEARTBEAT_BITS = 0xc0;
// Reduced from the 0x7f reset level to slow burn-in of the static status layout.
constexpr uint8_t CONTRAST = 0x40;
State state = State::uninitialized;
Failure failure = Failure::none;
uint8_t address = 0x3c;
bool transfer_active;
bool enabled;
uint8_t framebuffer[PAGES * WIDTH];
uint8_t confirmed[PAGES * WIDTH];
uint8_t valid_pages;
uint8_t dirty_pages;
uint8_t page;
uint8_t first_column;
uint8_t column_count;
uint64_t next_tick_us;
uint64_t uptime_minute = UINT64_MAX;
char cached_ip[16];
bool ip_valid;
uint32_t seen_address_generation;
bool heartbeat;
uint32_t published_status;
uint32_t last_published_status = UINT32_MAX;
bool address_error;

constexpr uint16_t glyph(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e) {
  return (a << 12) | (b << 9) | (c << 6) | (d << 3) | e;
}

uint16_t character(char c) {
  switch (c) {
  case '0': return glyph(7, 5, 5, 5, 7);
  case '1': return glyph(2, 6, 2, 2, 7);
  case '2': return glyph(7, 1, 7, 4, 7);
  case '3': return glyph(7, 1, 7, 1, 7);
  case '4': return glyph(5, 5, 7, 1, 1);
  case '5': return glyph(7, 4, 7, 1, 7);
  case '6': return glyph(7, 4, 7, 5, 7);
  case '7': return glyph(7, 1, 1, 2, 2);
  case '8': return glyph(7, 5, 7, 5, 7);
  case '9': return glyph(7, 5, 7, 1, 7);
  case 'E': return glyph(7, 4, 6, 4, 7);
  case 'F': return glyph(7, 4, 6, 4, 4);
  case 'I': return glyph(7, 2, 2, 2, 7);
  case 'L': return glyph(4, 4, 4, 4, 7);
  case 'N': return glyph(5, 7, 7, 7, 5);
  case 'O': return glyph(7, 5, 5, 5, 7);
  case 'P': return glyph(7, 5, 7, 4, 4);
  case 'R': return glyph(6, 5, 6, 5, 5);
  case 'U': return glyph(5, 5, 5, 5, 7);
  case 'd': return glyph(1, 1, 7, 5, 7);
  case ':': return glyph(0, 2, 0, 2, 0);
  case '.': return glyph(0, 0, 0, 0, 2);
  case ' ': return 0;
  default: assert(false && "Unsupported status display character"); return 0;
  }
}

// Draws scale-2 text from 3x5 glyphs into the framebuffer. Spacing is
// proportional: '.' and ':' use only their one-pixel middle column and ' ' is a
// one-column gap, so "IP: 255.255.255.255" fits the 128-pixel width exactly.
void text(unsigned y, const char *value) {
  constexpr unsigned scale = 2;
  unsigned x = 0;
  for (; *value; ++value) {
    char c = *value;
    unsigned first = 0, columns = 3;
    if (c == '.' || c == ':') {
      first = 1;
      columns = 1;
    } else if (c == ' ') {
      columns = 0;
    }
    assert(x + columns * scale <= WIDTH && y + 5 * scale <= PAGES * 8);
    uint16_t pixels = character(c);
    for (unsigned row = 0; row < 5; ++row)
      for (unsigned col = 0; col < columns; ++col)
        if (pixels & (1u << (14 - row * 3 - first - col)))
          for (unsigned dy = 0; dy < scale; ++dy)
            for (unsigned dx = 0; dx < scale; ++dx) {
              unsigned py = y + row * scale + dy;
              framebuffer[(py / 8) * WIDTH + x + col * scale + dx] |= 1u << (py % 8);
            }
    x += (columns + 1) * scale;
  }
}

void clear_field(unsigned y) {
  for (unsigned row = y; row < y + FIELD_ROWS; ++row)
    for (unsigned x = 0; x < WIDTH; ++x)
      framebuffer[(row / 8) * WIDTH + x] &= ~(1u << (row % 8));
}

// Redraws only fields whose text changed; unchanged pages are not compared.
void render(uint64_t now_us) {
  uint8_t changed_pages = valid_pages == ALL_PAGES ? 0 : ALL_PAGES;
  // The lock-free counter skips the address lock and copy while it is unchanged.
  uint32_t generation = z80_io_ports_network_address_generation();
  if (!ip_valid || generation != seen_address_generation) {
    seen_address_generation = generation;
    char ip[16] = {};
    size_t ip_length = z80_io_ports_get_network_address(ip, sizeof(ip));
    address_error = ip_length == 0 && ip[0] != '\0';
    const char *ip_text = address_error ? "ERROR" : (ip_length ? ip : "OFFLINE");
    if (!ip_valid || strcmp(cached_ip, ip_text) != 0) {
      char line[24];
      snprintf(line, sizeof(line), "IP: %s", ip_text);
      clear_field(IP_Y);
      text(IP_Y, line);
      snprintf(cached_ip, sizeof(cached_ip), "%s", ip_text);
      ip_valid = true;
      changed_pages |= 0x03;
    }
  }
  uint64_t seconds = now_us / 1'000'000;
  if (seconds / 60 != uptime_minute) {
    char uptime[24];
    uint64_t hours = seconds / 3600;
    unsigned minutes = static_cast<unsigned>((seconds / 60) % 60);
    // HH:MM for the first day, then days: "UP: 4d 05:23".
    if (hours < 24)
      snprintf(uptime, sizeof(uptime), "UP: %02u:%02u",
               static_cast<unsigned>(hours), minutes);
    else
      snprintf(uptime, sizeof(uptime), "UP: %llud %02u:%02u",
               static_cast<unsigned long long>(hours / 24),
               static_cast<unsigned>(hours % 24), minutes);
    clear_field(UPTIME_Y);
    text(UPTIME_Y, uptime);
    uptime_minute = seconds / 60;
    changed_pages |= 0x0c;
  }
  bool dot = (seconds & 1u) != 0;
  if (dot != heartbeat) {
    heartbeat = dot;
    changed_pages |= 0x08;
  }
  for (unsigned x = HEARTBEAT_COLUMN; x < HEARTBEAT_COLUMN + 2; ++x) {
    if (heartbeat)
      framebuffer[3 * WIDTH + x] |= HEARTBEAT_BITS;
    else
      framebuffer[3 * WIDTH + x] &= static_cast<uint8_t>(~HEARTBEAT_BITS);
  }
  dirty_pages = 0;
  for (unsigned p = 0; p < PAGES; ++p)
    if ((changed_pages & (1u << p)) &&
        (!(valid_pages & (1u << p)) ||
         memcmp(framebuffer + p * WIDTH, confirmed + p * WIDTH, WIDTH) != 0))
      dirty_pages |= 1u << p;
}

void next_page() {
  if (dirty_pages == 0) {
    state = enabled ? State::ready : State::enable;
    return;
  }
  for (page = 0; !(dirty_pages & (1u << page)); ++page) {}
  first_column = 0;
  unsigned last_column = WIDTH - 1;
  if (valid_pages & (1u << page)) {
    while (framebuffer[page * WIDTH + first_column] ==
           confirmed[page * WIDTH + first_column])
      ++first_column;
    while (framebuffer[page * WIDTH + last_column] ==
           confirmed[page * WIDTH + last_column])
      --last_column;
  }
  column_count = last_column - first_column + 1;
  state = State::page;
}

void service(bool updates_allowed) {
  if (transfer_active) {
    auto result = display_i2c::poll();
    if (result == display_i2c::Result::pending)
      return;
    transfer_active = false;
    if (result == display_i2c::Result::failed) {
      failure = Failure::bus;
      state = State::failed;
    } else if (state == State::scanning) {
      if (result == display_i2c::Result::complete) {
        state = State::initialize;
      } else if (address == 0x3c) {
        address = 0x3d;
      } else {
        failure = Failure::no_device;
        state = State::failed;
      }
    } else if (result == display_i2c::Result::nack) {
      failure = Failure::nack;
      state = State::failed;
    } else if (state == State::initialize) {
      state = State::ready;
    } else if (state == State::page) {
      memcpy(confirmed + page * WIDTH + first_column,
             framebuffer + page * WIDTH + first_column, column_count);
      valid_pages |= 1u << page;
      dirty_pages &= ~(1u << page);
      next_page();
    } else {
      enabled = true;
      state = State::ready;
    }
    // Chain the next transaction in this pass.
    if (state == State::failed)
      return;
  }
  if (!updates_allowed)
    return;
  if (state == State::uninitialized)
    state = State::scanning;
  if (state == State::ready) {
    uint64_t now_us = z80_time_us_64();
    if (now_us < next_tick_us)
      return;
    render(now_us);
    // One tick per uptime second: heartbeat toggle, IP check; uptime text per minute.
    next_tick_us = (now_us / 1'000'000 + 1) * 1'000'000;
    next_page();
  }
  if (state == State::scanning) {
    transfer_active = display_i2c::begin(address, nullptr, 0);
  } else if (state == State::initialize) {
    // SSD1315 128x32 with internal charge pump; the panel stays off until all
    // four RAM pages have confirmed contents.
    constexpr uint8_t setup[] = {
        0x00, 0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x02, 0xa1, 0xc8, 0xda, 0x02, 0x81, CONTRAST,
        0xd9, 0xf1, 0xdb, 0x20, 0xa4, 0xa6,
    };
    transfer_active = display_i2c::begin(address, setup, sizeof(setup));
  } else if (state == State::page) {
    // Co=1 control bytes carry page/column commands, then 0x40 starts pixel data.
    uint8_t data[display_i2c::MAX_DATA_BYTES] = {
        0x80, static_cast<uint8_t>(0xb0 | page),
        0x80, static_cast<uint8_t>(first_column & 0xf),
        0x80, static_cast<uint8_t>(0x10 | (first_column >> 4)),
        0x40,
    };
    memcpy(data + 7, framebuffer + page * WIDTH + first_column, column_count);
    transfer_active = display_i2c::begin(address, data, column_count + 7u);
  } else if (state == State::enable) {
    constexpr uint8_t command[] = {0x00, 0xaf};
    transfer_active = display_i2c::begin(address, command, sizeof(command));
  }
  // begin() only refuses after a transport failure: this is the bus's sole user.
  if (!transfer_active && state != State::ready) {
    failure = Failure::bus;
    state = State::failed;
  }
}

void publish(bool updates_allowed) {
  // Idle fast path, taken on almost every core-1 pass: nothing in flight and
  // no tick due, so nothing changes and the published status stays valid.
  if (state == State::failed ||
      (state == State::ready && !transfer_active &&
       (!updates_allowed || z80_time_us_64() < next_tick_us)))
    return;
  service(updates_allowed);
  uint32_t snapshot = static_cast<uint32_t>(state) |
                      (static_cast<uint32_t>(address) << 8) |
                      (static_cast<uint32_t>(failure) << 16) |
                      (static_cast<uint32_t>(display_i2c::failure()) << 24) |
                      (static_cast<uint32_t>(address_error) << 28);
  if (snapshot != last_published_status) {
    last_published_status = snapshot;
    __atomic_store_n(&published_status, snapshot, __ATOMIC_RELEASE);
  }
}

void print_status() {
  constexpr const char *states[] = {
      "uninitialized", "scanning", "initializing", "ready",
      "page", "enabling", "failed",
  };
  uint32_t snapshot = __atomic_load_n(&published_status, __ATOMIC_ACQUIRE);
  auto display_state = static_cast<State>(snapshot & 0xff);
  auto reason = static_cast<Failure>((snapshot >> 16) & 0xff);
  auto bus_reason = static_cast<display_i2c::Failure>((snapshot >> 24) & 0xf);
  printf("[oled] driver=SSD1315 128x32 transport=PIO_I2C_DMA SCL=GP%u SDA=GP%u "
         "state=%s address=0x%02x ip_check_ms=1000 uptime_interval_ms=60000 heartbeat_ms=1000\n",
         display_i2c::PIN_SCL, display_i2c::PIN_SDA,
         states[static_cast<unsigned>(display_state)],
         static_cast<unsigned>((snapshot >> 8) & 0xff));
  if (display_state == State::failed) {
    constexpr const char *reasons[] = {
        "none", "no OLED ACK at 0x3c or 0x3d", "SSD1315 write was not acknowledged",
        "I2C bus failed",
    };
    printf("[oled] FAIL: %s; display service disabled.\n",
           reason == Failure::bus ? display_i2c::failure_reason(bus_reason)
                                  : reasons[static_cast<unsigned>(reason)]);
  }
  if (snapshot & (1u << 28))
    printf("[oled] FAIL: IP address formatting; screen shows ERROR.\n");
}
}
}

extern "C" void z80_oled_core1_service(bool updates_allowed) {
  oled_display::publish(updates_allowed);
}

extern "C" void z80_oled_print_status(void) {
  oled_display::print_status();
}
