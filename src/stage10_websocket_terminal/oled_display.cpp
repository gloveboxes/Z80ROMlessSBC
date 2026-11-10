#include "oled_display.h"
#include "display_i2c.h"
#include "../common/io_service.h"
#include "pico/stdlib.h"
#include "z80sbc/cycle_stats.h"
#include "z80sbc/ram_func.h"
#include "z80sbc/ram_timer.h"
#include "z80sbc/terminal.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace oled_display {
namespace {
enum class State : uint8_t {
  uninitialized, scanning, initialize, ready, page, enable, failed,
};
enum class Failure : uint8_t { none, no_device, nack, bus };
constexpr display_i2c::Speed kBusSpeed = display_i2c::Speed::fast;
constexpr unsigned WIDTH = 128;
constexpr unsigned PAGES = 4;  // 128x32: four 8-pixel pages.
constexpr uint8_t ALL_PAGES = (1u << PAGES) - 1;
constexpr unsigned IP_Y = 2;       // Pages 0-1.
constexpr unsigned UPTIME_Y = 20;  // Pages 2-3.
constexpr unsigned FIELD_ROWS = 10;  // Scale-2 glyphs are 10 pixels tall.
// 2x2 dot in the bottom-right corner: rows 30-31 of page 3, below the uptime field.
constexpr unsigned HEARTBEAT_COLUMN = WIDTH - 2;
// A second 2x2 dot, two pixels to its left, blinks with it while a browser
// owns the web terminal.
constexpr unsigned WEB_DOT_COLUMN = HEARTBEAT_COLUMN - 4;
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
// Changed column range of each dirty page, found once when it is rendered.
uint8_t diff_first[PAGES];
uint8_t diff_last[PAGES];
uint64_t next_tick_us;
// Uptime is counted tick by tick, so a tick needs no 64-bit division.
bool clock_started;
uint64_t seconds;
uint64_t clock_minute;
uint8_t second_in_minute;
// True while next_tick_us is known to be within the 32-bit timer window.
bool gate_open;
uint64_t uptime_minute = UINT64_MAX;
z80_cycle_stats_t cpu_stats;
char cached_ip[16];
bool ip_valid;
uint32_t seen_address_generation;
bool heartbeat;
bool web_dot;
uint32_t published_status;
uint32_t last_published_status = UINT32_MAX;
bool address_error;

constexpr uint16_t glyph(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e) {
  return (a << 12) | (b << 9) | (c << 6) | (d << 3) | e;
}

constexpr uint16_t character(char c) {
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

constexpr unsigned SCALE = 2;

// Spacing is proportional: '.' and ':' use only their one-pixel middle column
// and ' ' is a one-column gap, so "IP: 255.255.255.255" fits 128 pixels exactly.
constexpr unsigned glyph_columns(char c) {
  return c == '.' || c == ':' ? 1 : c == ' ' ? 0 : 3;
}

// Pixels from the first column to the last ink column, without a trailing gap.
constexpr unsigned text_width(const char *value) {
  unsigned x = 0;
  for (; *value; ++value)
    x += (glyph_columns(*value) + 1) * SCALE;
  return x ? x - SCALE : 0;
}

constexpr bool supported(unsigned c) {
  return (c >= '0' && c <= '9') || c == 'E' || c == 'F' || c == 'I' ||
         c == 'L' || c == 'N' || c == 'O' || c == 'P' || c == 'R' ||
         c == 'U' || c == 'd' || c == ':' || c == '.' || c == ' ';
}

// Each glyph column pre-scaled to display pixels: bit n is pixel row n of a
// SCALE-times-taller column, ready to shift into place and OR into two pages.
struct Glyph {
  uint8_t columns = 3;
  uint16_t column[3] = {};
};
struct GlyphTable {
  Glyph glyph[128] = {};
};
constexpr GlyphTable build_glyphs() {
  GlyphTable table;
  for (unsigned c = 0; c < 128; ++c) {
    if (!supported(c))
      continue;
    Glyph &out = table.glyph[c];
    out.columns = static_cast<uint8_t>(glyph_columns(static_cast<char>(c)));
    unsigned first = out.columns == 1 ? 1 : 0;
    uint16_t pixels = character(static_cast<char>(c));
    for (unsigned col = 0; col < out.columns; ++col)
      for (unsigned row = 0; row < 5; ++row)
        if (pixels & (1u << (14 - row * 3 - first - col)))
          for (unsigned dy = 0; dy < SCALE; ++dy)
            out.column[col] |= static_cast<uint16_t>(1u << (row * SCALE + dy));
  }
  return table;
}
// RAM-resident: drawing never waits on an XIP cache miss.
constexpr GlyphTable GLYPHS __not_in_flash("oled_glyphs") = build_glyphs();

// Draws scale-2 3x5 glyphs into a page-organized bitmap `stride` columns wide.
void __not_in_flash_func(draw_text)(uint8_t *pixels, unsigned stride,
                                    [[maybe_unused]] unsigned rows, unsigned x,
                                    unsigned y, const char *value) {
  const unsigned shift = y % 8;
  assert(shift + 5 * SCALE <= 16);  // The glyph spans two pages.
  uint8_t *low = pixels + (y / 8) * stride;
  uint8_t *high = low + stride;
  for (; *value; ++value) {
    const Glyph &glyph = GLYPHS.glyph[static_cast<uint8_t>(*value) & 0x7f];
    assert(x + glyph.columns * SCALE <= stride && y + 5 * SCALE <= rows);
    for (unsigned col = 0; col < glyph.columns; ++col) {
      uint32_t bits = static_cast<uint32_t>(glyph.column[col]) << shift;
      uint8_t lower = static_cast<uint8_t>(bits);
      uint8_t upper = static_cast<uint8_t>(bits >> 8);
      for (unsigned dx = 0; dx < SCALE; ++dx, ++x) {
        low[x] |= lower;
        if (upper)
          high[x] |= upper;
      }
    }
    x += SCALE;
  }
}

void text(unsigned y, const char *value, unsigned x = 0) {
  draw_text(framebuffer, WIDTH, PAGES * 8, x, y, value);
}

// Clears the field's rows with one masked pass per page, four columns at a time.
void __not_in_flash_func(clear_field)(unsigned y) {
  for (unsigned p = y / 8; p <= (y + FIELD_ROWS - 1) / 8; ++p) {
    unsigned low = p * 8 > y ? 0 : y - p * 8;
    unsigned high = y + FIELD_ROWS - p * 8 < 8 ? y + FIELD_ROWS - p * 8 : 8;
    uint32_t mask = ((1u << high) - 1) & ~((1u << low) - 1);
    uint8_t *row = framebuffer + p * WIDTH;
    if (mask == 0xff) {
      memset(row, 0, WIDTH);
      continue;
    }
    uint32_t keep = ~(mask * 0x01010101u);
    for (unsigned x = 0; x < WIDTH; x += 4) {
      uint32_t word;
      memcpy(&word, row + x, 4);
      word &= keep;
      memcpy(row + x, &word, 4);
    }
  }
}

constexpr unsigned UPTIME_PAGE = UPTIME_Y / 8;
static_assert(UPTIME_Y + FIELD_ROWS <= (UPTIME_PAGE + 2) * 8);
static_assert(text_width("U: 99999999d 23:59") <= WIDTH);
static_assert(text_width("999999999d 23:59") <= WIDTH);

// Sets or clears one 2x2 dot in rows 30-31 without touching other bits.
void __not_in_flash_func(set_dot)(unsigned column, bool on) {
  for (unsigned x = column; x < column + 2; ++x) {
    if (on)
      framebuffer[3 * WIDTH + x] |= HEARTBEAT_BITS;
    else
      framebuffer[3 * WIDTH + x] &= static_cast<uint8_t>(~HEARTBEAT_BITS);
  }
}

// Appends `value` in decimal, zero-padded to `digits`; returns the end.
char *__not_in_flash_func(put_number)(char *out, uint32_t value, unsigned digits) {
  char reversed[10];
  unsigned count = 0;
  do {
    reversed[count++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value);
  for (; digits > count; --digits)
    *out++ = '0';
  while (count)
    *out++ = reversed[--count];
  return out;
}

char *__not_in_flash_func(put_text)(char *out, const char *value) {
  while (*value)
    *out++ = *value++;
  return out;
}

// "U: HH:MM", or "U: Dd HH:MM" from one day, dropping the label only when
// nine day digits need its space. `out` holds at least 24 characters.
void __not_in_flash_func(format_uptime)(char *out, uint64_t minutes_total) {
  // Hours exceed 32 bits only past 2^32 minutes, so the 64-bit divides stay
  // off the usual path.
  uint32_t days, hours, minutes;
  if (minutes_total <= UINT32_MAX) {
    uint32_t total = static_cast<uint32_t>(minutes_total);
    hours = total / 60;
    minutes = total % 60;
    days = hours / 24;
    hours %= 24;
  } else {
    uint64_t total_hours = minutes_total / 60;
    minutes = static_cast<uint32_t>(minutes_total - total_hours * 60);
    days = static_cast<uint32_t>(total_hours / 24);
    hours = static_cast<uint32_t>(total_hours - uint64_t{days} * 24);
  }
  char *end = put_text(out, "U: ");
  if (days) {
    end = put_number(end, days, 1);
    *end++ = 'd';
    *end++ = ' ';
  }
  end = put_number(end, hours, 2);
  *end++ = ':';
  end = put_number(end, minutes, 2);
  *end = '\0';
  if (days && text_width(out) > WIDTH)
    memmove(out, out + 3, static_cast<size_t>(end - out) - 2);
}

// Column of the first / last byte that differs between two pages, found four
// bytes at a time. Callers know the pages differ.
unsigned __not_in_flash_func(first_difference)(const uint8_t *a, const uint8_t *b) {
  unsigned i = 0;
  for (; i < WIDTH; i += 4) {
    uint32_t x, y;
    memcpy(&x, a + i, 4);
    memcpy(&y, b + i, 4);
    if (x != y)
      break;
  }
  if (i == WIDTH)
    return WIDTH;
  while (a[i] == b[i])
    ++i;
  return i;
}

unsigned __not_in_flash_func(last_difference)(const uint8_t *a, const uint8_t *b) {
  unsigned i = WIDTH;
  for (; i >= 4; i -= 4) {
    uint32_t x, y;
    memcpy(&x, a + i - 4, 4);
    memcpy(&y, b + i - 4, 4);
    if (x != y)
      break;
  }
  while (i && a[i - 1] == b[i - 1])
    --i;
  return i - 1;
}

// Redraws only fields whose text changed; unchanged pages are not compared.
void __not_in_flash_func(render)() {
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
      put_text(put_text(line, "IP: "), ip_text)[0] = '\0';
      clear_field(IP_Y);
      text(IP_Y, line);
      strncpy(cached_ip, ip_text, sizeof(cached_ip) - 1);
      cached_ip[sizeof(cached_ip) - 1] = '\0';
      ip_valid = true;
      changed_pages |= 0x03;
    }
  }
  if (clock_minute != uptime_minute) {
    char uptime[32];
    format_uptime(uptime, clock_minute);
    clear_field(UPTIME_Y);
    text(UPTIME_Y, uptime);
    uptime_minute = clock_minute;
    changed_pages |= 0x0c;
  }
  bool dot = (seconds & 1u) != 0;
  // One atomic load on lit ticks only; a connect or disconnect shows within
  // two seconds.
  bool web = dot && z80_terminal_browser_connected();
  if (dot != heartbeat || web != web_dot) {
    heartbeat = dot;
    web_dot = web;
    changed_pages |= 0x08;
  }
  set_dot(HEARTBEAT_COLUMN, heartbeat);
  set_dot(WEB_DOT_COLUMN, web_dot);
  dirty_pages = 0;
  for (unsigned p = 0; p < PAGES; ++p) {
    if (!(changed_pages & (1u << p)))
      continue;
    const uint8_t *drawn = framebuffer + p * WIDTH;
    const uint8_t *shown = confirmed + p * WIDTH;
    if (!(valid_pages & (1u << p))) {
      diff_first[p] = 0;
      diff_last[p] = WIDTH - 1;
    } else {
      unsigned first = first_difference(drawn, shown);
      if (first == WIDTH)
        continue;
      diff_first[p] = static_cast<uint8_t>(first);
      diff_last[p] = static_cast<uint8_t>(last_difference(drawn, shown));
    }
    dirty_pages |= 1u << p;
  }
}

void __not_in_flash_func(next_page)() {
  if (dirty_pages == 0) {
    state = enabled ? State::ready : State::enable;
    return;
  }
  for (page = 0; !(dirty_pages & (1u << page)); ++page) {}
  first_column = diff_first[page];
  column_count = diff_last[page] - first_column + 1;
  state = State::page;
}

void __not_in_flash_func(service)(bool updates_allowed) {
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
  if (!transfer_active && display_i2c::busy())
    return;
  if (state == State::uninitialized)
    state = State::scanning;
  if (state == State::ready) {
    uint64_t now_us = z80_time_us_64();
    if (now_us < next_tick_us) {
      gate_open = true;
      return;
    }
    // One tick per uptime second: heartbeat toggle, IP check; uptime text per minute.
    if (clock_started && now_us - next_tick_us < 1'000'000) {
      ++seconds;
      next_tick_us += 1'000'000;
      if (++second_in_minute == 60) {
        second_in_minute = 0;
        ++clock_minute;
      }
    } else {
      seconds = now_us / 1'000'000;
      clock_minute = seconds / 60;
      second_in_minute = static_cast<uint8_t>(seconds % 60);
      next_tick_us = (seconds + 1) * 1'000'000;
      clock_started = true;
    }
    gate_open = true;
    render();
    next_page();
  }
  if (state == State::scanning) {
    transfer_active = display_i2c::begin(address, nullptr, 0, kBusSpeed);
  } else if (state == State::initialize) {
    // SSD1315 128x32 with internal charge pump; the panel stays off until all
    // four RAM pages have confirmed contents.
    constexpr uint8_t setup[] = {
        0x00, 0xae, 0xd5, 0x80, 0xa8, 0x1f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x02, 0xa1, 0xc8, 0xda, 0x02, 0x81, CONTRAST,
        0xd9, 0xf1, 0xdb, 0x20, 0xa4, 0xa6,
    };
    transfer_active = display_i2c::begin(address, setup, sizeof(setup), kBusSpeed);
  } else if (state == State::page) {
    // Co=1 control bytes carry page/column commands, then 0x40 starts pixel data.
    uint8_t data[display_i2c::MAX_DATA_BYTES];
    data[0] = 0x80;
    data[1] = static_cast<uint8_t>(0xb0 | page);
    data[2] = 0x80;
    data[3] = static_cast<uint8_t>(first_column & 0xf);
    data[4] = 0x80;
    data[5] = static_cast<uint8_t>(0x10 | (first_column >> 4));
    data[6] = 0x40;
    memcpy(data + 7, framebuffer + page * WIDTH + first_column, column_count);
    transfer_active = display_i2c::begin(address, data, column_count + 7u, kBusSpeed);
  } else if (state == State::enable) {
    constexpr uint8_t command[] = {0x00, 0xaf};
    transfer_active = display_i2c::begin(address, command, sizeof(command), kBusSpeed);
  }
  // Other bus users have already been excluded above.
  if (!transfer_active && state != State::ready) {
    failure = Failure::bus;
    state = State::failed;
  }
}

void __not_in_flash_func(publish)(bool updates_allowed) {
  // Idle fast path, taken on almost every core-0 pass: nothing in flight and
  // no tick due, so nothing changes and the published status stays valid. The
  // 32-bit compare is valid only while the timer has not wrapped since
  // gate_open was set, so pausing updates closes the gate.
  if (state == State::failed)
    return;
  if (!updates_allowed) {
    gate_open = false;
    if (state == State::ready && !transfer_active)
      return;
  } else if (gate_open && state == State::ready && !transfer_active &&
             static_cast<int32_t>(z80_time_us_32() -
                                  static_cast<uint32_t>(next_tick_us)) < 0) {
    return;
  }
  uint32_t start = z80_cycle_count();
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
  z80_cycle_stats_add(&cpu_stats, z80_cycle_count() - start);
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
  uint32_t passes, max_cycles;
  z80_cycle_stats_take(&cpu_stats, &passes, &max_cycles);
  printf("[oled] driver=SSD1315 128x32 transport=PIO_I2C_DMA SCL=GP%u SDA=GP%u "
         "state=%s address=0x%02x ip_check_ms=1000 uptime_interval_ms=60000 heartbeat_ms=1000 "
         "cpu_passes=%lu cpu_max_cycles=%lu\n",
         display_i2c::PIN_SCL, display_i2c::PIN_SDA,
         states[static_cast<unsigned>(display_state)],
         static_cast<unsigned>((snapshot >> 8) & 0xff),
         static_cast<unsigned long>(passes), static_cast<unsigned long>(max_cycles));
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

extern "C" void __not_in_flash_func(z80_oled_service)(bool updates_allowed) {
  oled_display::publish(updates_allowed);
}

extern "C" void z80_oled_print_status(void) {
  oled_display::print_status();
}
