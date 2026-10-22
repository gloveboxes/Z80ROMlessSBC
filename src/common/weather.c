#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"
#include "../cpm/dcc_io_adapter/port_drivers/json_scan.h"

enum {
  FIELD_COUNT = 15, FIELD_BYTES = 96,
  CONFIG_MS = 1000, SETTLE_MS = 5000,
  RETRY_MS = 120000, REFRESH_MS = 1200000,
};
typedef struct {
  char key[41], location[65], units[12], error[64];
} weather_config_t;
static weather_config_t config;
static char fields[FIELD_COUNT][FIELD_BYTES];
static char pending[FIELD_COUNT][FIELD_BYTES];
static bool enabled, have_data, was_online;
static uint8_t status;
static unsigned phase;
static unsigned startup_retries;
static uint64_t config_due, fetch_due, last_fetch;
static bool (*start_request)(const char *);
static void (*cancel_request)(void);
static char path[512];

static void fail(const char *message) {
  snprintf(fields[12], FIELD_BYTES, "%s", message);
  status = Z80_WEATHER_ERROR;
  phase = 0;
  fetch_due = z80_service_millis() +
              (!have_data && startup_retries++ < 5 ? RETRY_MS : REFRESH_MS);
  printf("[weather] %s\n", fields[12]);
}

static void read_setting(const char *key, char *dst, size_t capacity,
                          weather_config_t *next) {
  char value[256];
  if (!environment_io_get(key, value, sizeof(value)))
    return;
  if (strlen(value) >= capacity) {
    snprintf(next->error, sizeof(next->error), "%s is too long", key);
    return;
  }
  memcpy(dst, value, strlen(value) + 1);
}

static bool load_config(void) {
  weather_config_t next = {0};
  environment_io_close();
  environment_io_init(NULL);
  if (environment_input(ENVIRONMENT_PORT_COMMAND) != 0) {
    snprintf(next.error, sizeof(next.error), "Cannot read weather ENV settings");
  } else {
    read_setting("OWM_KEY", next.key, sizeof(next.key), &next);
    read_setting("OWM_LOCATION", next.location, sizeof(next.location), &next);
    read_setting("OWM_UNITS", next.units, sizeof(next.units), &next);
    if (!next.units[0])
      strcpy(next.units, "metric");
    if (!next.error[0] && (!next.key[0] || !next.location[0]))
      strcpy(next.error, "Set OWM_KEY and OWM_LOCATION in ENV");
    if (!next.error[0] && strcmp(next.units, "metric") &&
        strcmp(next.units, "imperial") && strcmp(next.units, "standard"))
      strcpy(next.error, "OWM_UNITS must be metric, imperial or standard");
  }
  if (memcmp(&next, &config, sizeof(next)) == 0)
    return false;
  cancel_request();
  config = next;
  memset(fields, 0, sizeof(fields));
  have_data = false;
  phase = 0;
  startup_retries = 0;
  status = Z80_WEATHER_NONE;
  strcpy(fields[11], strcmp(config.units, "imperial") == 0 ? "F" :
                     strcmp(config.units, "standard") == 0 ? "K" : "C");
  if (config.error[0])
    fail(config.error);
  return true;
}

static char *encode(char *dst, const char *src, size_t length) {
  static const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < length; ++i) {
    unsigned char c = (unsigned char)src[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
      *dst++ = (char)c;
    else {
      *dst++ = '%';
      *dst++ = hex[c >> 4];
      *dst++ = hex[c & 15];
    }
  }
  *dst = '\0';
  return dst;
}

static bool build_path(bool forecast) {
  char location[3 * sizeof(config.location) + 16];
  char key[3 * sizeof(config.key)];
  if (strncmp(config.location, "lat=", 4) == 0) {
    char *end;
    const char *lat = config.location + 4;
    double latitude = strtod(lat, &end);
    if (end == lat || !isfinite(latitude) || latitude < -90 ||
        latitude > 90 || strncmp(end, "&lon=", 5))
      return false;
    const char *lon = end + 5;
    double longitude = strtod(lon, &end);
    if (end == lon || *end || !isfinite(longitude) ||
        longitude < -180 || longitude > 180)
      return false;
    char *out = location;
    memcpy(out, "lat=", 4);
    out = encode(out + 4, lat, (size_t)(lon - 5 - lat));
    memcpy(out, "&lon=", 5);
    encode(out + 5, lon, strlen(lon));
  } else {
    memcpy(location, "q=", 2);
    encode(location + 2, config.location, strlen(config.location));
  }
  encode(key, config.key, strlen(config.key));
  int length = snprintf(path, sizeof(path),
      "/data/2.5/%s?%s&appid=%s&units=%s%s",
      forecast ? "forecast" : "weather", location, key, config.units,
      forecast ? "&cnt=1" : "");
  return length > 0 && (size_t)length < sizeof(path);
}

void z80_weather_init(bool (*start)(const char *), void (*cancel)(void)) {
  start_request = start;
  cancel_request = cancel;
  enabled = true;
}

void z80_weather_poll(bool online) {
  if (!enabled)
    return;
  uint64_t now = z80_service_millis();
  bool changed = false;
  if (now >= config_due) {
    config_due = now + CONFIG_MS;
    changed = load_config();
  }
  if (!online) {
    if (phase) {
      cancel_request();
      phase = 0;
      status = have_data ? Z80_WEATHER_READY : Z80_WEATHER_NONE;
      strcpy(fields[12], "Network or synchronized time unavailable");
    }
    was_online = false;
    return;
  }
  if (!was_online || changed)
    fetch_due = now + SETTLE_MS;
  was_online = true;
  if (config.error[0] || (phase != 0 && phase != 2) || now < fetch_due)
    return;
  bool forecast = phase == 2;
  if (!build_path(forecast)) {
    fail("Invalid OWM_LOCATION; use a city or lat=...&lon=...");
    return;
  }
  if (!forecast) {
    memset(pending, 0, sizeof(pending));
    strcpy(pending[11], fields[11]);
  }
  phase = forecast ? 3 : 1;
  status = Z80_WEATHER_FETCHING;
  if (!start_request(path))
    z80_weather_result(0, NULL, 0, "Cannot start weather HTTPS request");
}

static bool parse_conditions(json_scan_range_t item,
                             char output[FIELD_COUNT][FIELD_BYTES],
                             bool forecast) {
  json_scan_range_t main, weather, wind;
  unsigned base = forecast ? 6 : 1;
  if (!json_scan_object(item, "main", &main) ||
      !json_scan_first_array_object(item, "weather", &weather) ||
      !json_scan_get_string(weather, "main", output[base], FIELD_BYTES) ||
      !json_scan_get_string(weather, "description", output[base + 1], FIELD_BYTES) ||
      !json_scan_get_number(main, "temp", output[forecast ? 8 : 3], FIELD_BYTES) ||
      !json_scan_get_number(main, "feels_like", output[forecast ? 14 : 13], FIELD_BYTES))
    return false;
  char *description = output[base + 1];
  if (*description >= 'a' && *description <= 'z')
    *description -= 'a' - 'A';
  if (forecast)
    return json_scan_get_string(item, "dt_txt", output[9], FIELD_BYTES);
  return json_scan_get_string(item, "name", output[0], FIELD_BYTES) &&
         json_scan_get_number(main, "humidity", output[4], FIELD_BYTES) &&
         json_scan_object(item, "wind", &wind) &&
         json_scan_get_number(wind, "speed", output[5], FIELD_BYTES);
}

void z80_weather_result(unsigned http_status, const char *body, size_t length,
                        const char *error) {
  if (phase != 1 && phase != 3)
    return;
  char message[FIELD_BYTES] = "";
  if (error)
    snprintf(message, sizeof(message), "%s", error);
  else if (http_status != 200)
    snprintf(message, sizeof(message), "OpenWeatherMap HTTP %u", http_status);
  else if (!body || length == 0 || length > Z80_WEATHER_BODY_MAX ||
           memchr(body, 0, length))
    strcpy(message, "Invalid weather response");
  else {
    json_scan_range_t root = {body, body + length}, item;
    const char *first = json_scan_skip_ws(root.start, root.end);
    const char *end = root.end;
    while (end > first && (end[-1] == ' ' || end[-1] == '\r' ||
                          end[-1] == '\n' || end[-1] == '\t'))
      --end;
    int code;
    bool parsed = first < end && *first == '{' && end[-1] == '}' &&
                  json_scan_get_int(root, "cod", &code) && code == 200;
    if (phase == 3)
      parsed = parsed && json_scan_first_array_object(root, "list", &item) &&
               parse_conditions(item, pending, true);
    else
      parsed = parsed && parse_conditions(root, pending, false);
    if (!parsed)
      strcpy(message, "Invalid weather JSON fields");
  }
  if (phase == 1) {
    if (message[0]) {
      fail(message);
      return;
    }
    phase = 2;
    fetch_due = z80_service_millis();
    return;
  }
  if (message[0]) {
    for (unsigned i = 6; i <= 9; ++i)
      pending[i][0] = '\0';
    pending[14][0] = '\0';
    snprintf(pending[12], FIELD_BYTES, "Forecast: %.80s", message);
    printf("[weather] %s\n", pending[12]);
  }
  memcpy(fields, pending, sizeof(fields));
  have_data = true;
  last_fetch = z80_service_millis();
  status = Z80_WEATHER_READY;
  phase = 0;
  fetch_due = last_fetch + REFRESH_MS;
}

io_service_error_t z80_weather_service(const uint8_t *request, size_t length,
                                      io_service_result_t *result) {
  if (!enabled)
    return IO_SERVICE_UNAVAILABLE;
  if (request[0] > 1)
    return IO_SERVICE_UNKNOWN_OPERATION;
  if (length != (request[0] == 0 ? 2u : 1u))
    return IO_SERVICE_INVALID_LENGTH;
  result->has_response = true;
  if (request[0] == 1) {
    result->data[0] = status;
    result->length = 1;
    return IO_SERVICE_OK;
  }
  unsigned field = request[1];
  if (field >= FIELD_COUNT)
    return IO_SERVICE_INVALID_PARAMETER;
  if (field != 11 && field != 12 && !have_data)
    return IO_SERVICE_UNAVAILABLE;
  if (field == 10)
    snprintf(fields[10], FIELD_BYTES, "%llu",
             (unsigned long long)((z80_service_millis() - last_fetch) / 1000));
  result->length = strlen(fields[field]);
  memcpy(result->data, fields[field], result->length);
  return IO_SERVICE_OK;
}
