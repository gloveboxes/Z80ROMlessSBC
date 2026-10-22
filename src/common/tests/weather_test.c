#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../weather.h"
#include "../weather_http.h"
#include "../../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

static uint64_t now;
static unsigned starts, cancels;
static uint8_t env_error;
static bool can_start = true;
static const char *key = "test-key", *location = "New York,US", *units;
static char requested[512];
static io_service_result_t reply;
static const char current[] =
  "{\"cod\":200,\"name\":\"New York\",\"weather\":[{\"main\":\"Clouds\","
  "\"description\":\"broken clouds\"}],\"main\":{\"temp\":-3.6,"
  "\"feels_like\":-5.5,\"humidity\":82},\"wind\":{\"speed\":2.6}}";
static const char forecast[] =
  "{\"cod\":\"200\",\"list\":[{\"weather\":[{\"main\":\"Rain\","
  "\"description\":\"light rain\"}],\"main\":{\"temp\":4.5,"
  "\"feels_like\":1.2},\"dt_txt\":\"2026-10-22 03:00:00\"}]}";

uint64_t z80_service_millis(void) { return now; }
void environment_io_init(const char *path) { (void)path; }
void environment_io_close(void) {}
uint8_t environment_input(uint8_t port) {
  assert(port == ENVIRONMENT_PORT_COMMAND);
  return env_error;
}
bool environment_io_get(const char *name, char *value, size_t capacity) {
  const char *text = strcmp(name, "OWM_KEY") == 0 ? key :
                     strcmp(name, "OWM_LOCATION") == 0 ? location : units;
  assert(strcmp(name, "OWM_KEY") == 0 || strcmp(name, "OWM_LOCATION") == 0 ||
         strcmp(name, "OWM_UNITS") == 0);
  if (!text)
    return false;
  assert(strlen(text) < capacity);
  strcpy(value, text);
  return true;
}
static bool start(const char *path) {
  ++starts;
  assert(strlen(path) < sizeof(requested));
  strcpy(requested, path);
  return can_start;
}
static void cancel(void) { ++cancels; }
static unsigned state(void) {
  const uint8_t request[] = {1};
  assert(z80_weather_service(request, sizeof(request), &reply) == IO_SERVICE_OK);
  assert(reply.has_response && reply.length == 1);
  return reply.data[0];
}
static void field(unsigned index, const char *expected) {
  const uint8_t request[] = {0, (uint8_t)index};
  assert(z80_weather_service(request, sizeof(request), &reply) == IO_SERVICE_OK);
  assert(reply.has_response && reply.length == strlen(expected));
  assert(memcmp(reply.data, expected, reply.length) == 0);
}
static void result(const char *body) {
  z80_weather_result(200, body, strlen(body), NULL);
}
static void advance(uint64_t ms) {
  now += ms;
  z80_weather_poll(true);
}

static void test_engine(void) {
  uint8_t request[] = {1, 0};
  assert(z80_weather_service(request, 1, &reply) == IO_SERVICE_UNAVAILABLE);
  z80_weather_init(start, cancel);
  z80_weather_poll(false);
  assert(state() == Z80_WEATHER_NONE && starts == 0);
  z80_weather_poll(true);
  advance(4999);
  assert(starts == 0);
  advance(1);
  assert(state() == Z80_WEATHER_FETCHING && starts == 1);
  assert(strstr(requested, "q=New%20York%2CUS&appid=test-key&units=metric"));
  result(current);
  z80_weather_poll(true);
  assert(starts == 2 && strstr(requested, "forecast?") &&
         strstr(requested, "&cnt=1"));
  result(forecast);
  assert(state() == Z80_WEATHER_READY);
  static const char *expected[] = {
    "New York", "Clouds", "Broken clouds", "-4", "82", "3",
    "Rain", "Light rain", "5", "2026-10-22 03:00:00", "0", "C", "", "-6", "1",
  };
  for (unsigned i = 0; i < 15; ++i)
    field(i, expected[i]);
  unsigned before = starts;
  advance(1199999);
  assert(starts == before);
  field(10, "1199");
  advance(1);
  assert(state() == Z80_WEATHER_FETCHING && starts == before + 1);
  z80_weather_result(401, NULL, 0, NULL);
  assert(state() == Z80_WEATHER_ERROR);
  field(12, "OpenWeatherMap HTTP 401");
  field(0, "New York");
  advance(1199999);
  assert(starts == before + 1);
  advance(1);
  assert(starts == before + 2);
  result("{}");
  assert(state() == Z80_WEATHER_ERROR);
  field(12, "Invalid weather JSON fields");

  units = "imperial";
  advance(1000);
  assert(state() == Z80_WEATHER_NONE);
  field(11, "F");
  advance(5000);
  assert(strstr(requested, "&units=imperial"));
  result(current);
  z80_weather_poll(true);
  z80_weather_result(0, NULL, 0, "Weather HTTPS request timed out");
  assert(state() == Z80_WEATHER_READY);
  field(6, "");
  field(14, "");
  field(12, "Forecast: Weather HTTPS request timed out");

  location = "lat=-33.8&lon=151.2";
  advance(1000);
  advance(5000);
  assert(strstr(requested, "lat=-33.8&lon=151.2&appid="));
  before = cancels;
  z80_weather_poll(false);
  assert(cancels == before + 1 && state() == Z80_WEATHER_NONE);
  result(current);
  assert(state() == Z80_WEATHER_NONE);
  location = "lat=91&lon=10";
  z80_weather_poll(true);
  advance(1000);
  advance(5000);
  assert(state() == Z80_WEATHER_ERROR);
  field(12, "Invalid OWM_LOCATION; use a city or lat=...&lon=...");

  location = "London,UK";
  units = "kelvin";
  advance(1000);
  assert(state() == Z80_WEATHER_ERROR);
  field(12, "OWM_UNITS must be metric, imperial or standard");
  units = "standard";
  key = NULL;
  advance(1000);
  field(11, "K");
  field(12, "Set OWM_KEY and OWM_LOCATION in ENV");
  key = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  advance(1000);
  field(12, "OWM_KEY is too long");
  key = "key&injected=1";
  env_error = 253;
  advance(1000);
  field(12, "Cannot read weather ENV settings");
  env_error = 0;
  advance(1000);
  can_start = false;
  advance(5000);
  assert(strstr(requested, "appid=key%26injected%3D1"));
  field(12, "Cannot start weather HTTPS request");
  can_start = true;
  advance(120000);
  result("{\"cod\":200,\"name\":\"X\",\"weather\":[{\"main\":\"Rain\","
         "\"description\":\"rain\"}],\"main\":{\"temp\":1e999,"
         "\"feels_like\":0,\"humidity\":80},\"wind\":{\"speed\":1}}");
  assert(state() == Z80_WEATHER_ERROR);

  for (unsigned retry = 0; retry < 4; ++retry) {
    before = starts;
    advance(119999);
    assert(starts == before);
    advance(1);
    assert(starts == before + 1);
    result("{}");
  }
  before = starts;
  advance(1199999);
  assert(starts == before);
  advance(1);
  assert(starts == before + 1);

  request[0] = 2;
  assert(z80_weather_service(request, 1, &reply) == IO_SERVICE_UNKNOWN_OPERATION);
  request[0] = 1;
  assert(z80_weather_service(request, 2, &reply) == IO_SERVICE_INVALID_LENGTH);
  request[0] = 0;
  request[1] = 15;
  assert(z80_weather_service(request, 2, &reply) == IO_SERVICE_INVALID_PARAMETER);
  request[1] = 0;
  assert(z80_weather_service(request, 2, &reply) == IO_SERVICE_UNAVAILABLE);
}

static bool decode(const char *input, const char *expected) {
  char wire[20000];
  assert(strlen(input) < sizeof(wire));
  strcpy(wire, input);
  unsigned status = 0;
  size_t length = 0;
  const char *body = NULL;
  bool ok = z80_weather_http_decode(wire, strlen(wire), &status, &body, &length);
  if (expected) {
    assert(ok && status == 200);
    assert(length == strlen(expected) && strcmp(body, expected) == 0);
  }
  return ok;
}

static void test_http(void) {
  assert(decode("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", "{}"));
  assert(decode("HTTP/1.0 200 OK\r\n\r\n{}", "{}"));
  assert(decode("HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n"
                "1;test=yes\r\n{\r\n1\r\n}\r\n0\r\nX-End: yes\r\n\r\n", "{}"));
  static const char *bad[] = {
    "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\nX",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n{}\r\n0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nfffffff\r\n",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n;foo\r\n",
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n{}",
    "HTTP/1.1 20X OK\r\n\r\n{}",
  };
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    assert(!decode(bad[i], NULL));
  char oversized[9000] = "HTTP/1.1 200 OK\r\n\r\n";
  memset(oversized + strlen(oversized), 'X', 8193);
  assert(!decode(oversized, NULL));
}

int main(void) {
  test_engine();
  test_http();
  puts("PASS: weather cache, fields, ENV, retries, parsing and HTTP framing");
}
