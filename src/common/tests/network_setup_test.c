#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int setup_printf(const char *format, ...);
#define printf setup_printf
#include "../../stage10_websocket_terminal/network_setup.c"
#undef printf
#include "z80sbc/environment.h"

static uint8_t snapshot[Z80_ENV_SNAPSHOT_BYTES];
static size_t snapshot_length;
static bool on_core1;
static int read_error;
static int write_error;
static int write_fail_after = -1;
static unsigned writes;
static char output[131072];
static size_t output_length;

static int setup_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int count = vsnprintf(output + output_length,
                       sizeof(output) - output_length, format, args);
  va_end(args);
  assert(count >= 0 && (size_t)count < sizeof(output) - output_length);
  output_length += (size_t)count;
  return count;
}

void queue_init(queue_t *queue, unsigned size, unsigned capacity) {
  free(queue->data);
  queue->data = calloc(capacity, size);
  assert(queue->data);
  queue->element_size = size;
  queue->capacity = capacity;
  queue->count = 0;
}
bool queue_try_add(queue_t *queue, const void *item) {
  if (queue->count == queue->capacity)
    return false;
  memcpy(queue->data + queue->count++ * queue->element_size,
         item, queue->element_size);
  return true;
}
bool queue_try_remove(queue_t *queue, void *item) {
  if (!queue->count)
    return false;
  memcpy(item, queue->data, queue->element_size);
  --queue->count;
  memmove(queue->data, queue->data + queue->element_size,
          queue->count * queue->element_size);
  return true;
}
int z80_env_snapshot_load(uint8_t *data, size_t capacity, size_t *length) {
  assert(on_core1 && snapshot_length <= capacity);
  if (read_error)
    return read_error;
  memcpy(data, snapshot, snapshot_length);
  *length = snapshot_length;
  return 0;
}
int z80_env_snapshot_save(const uint8_t *data, size_t length) {
  assert(on_core1 && length <= sizeof(snapshot));
  if (write_error)
    return write_error;
  if (write_fail_after >= 0 && writes >= (unsigned)write_fail_after)
    return -3;
  memcpy(snapshot, data, length);
  snapshot_length = length;
  ++writes;
  return 0;
}
uint64_t z80_service_millis(void) { return 0; }

static void service(void) {
  on_core1 = true;
  z80_network_setup_core1_service();
  on_core1 = false;
}
static void type(const char *text) {
  for (; *text; ++text)
    assert(z80_network_setup_input((uint8_t)*text));
}
static void finish(void) {
  assert(step == SETUP_SAVING || step == SETUP_LOADING);
  assert(!z80_network_setup_quiescent());
  service();
  assert(z80_network_setup_quiescent());
  z80_network_setup_usb_service(true, true);
  assert(step == SETUP_MENU);
}

static void open_shell(void) {
  z80_network_setup_request();
  finish();
  assert(menu == MENU_MAIN);
}

static void expect_value(const char *key, const char *expected) {
  char value[256];
  assert(environment_io_get(key, value, sizeof(value)));
  assert(!strcmp(value, expected));
}

static void restart_shell(void) {
  z80_network_setup_init();
  service();
  open_shell();
}

int main(void) {
  z80_network_setup_init();
  assert(!z80_network_config());
  service();
  assert(__atomic_load_n(&settings_state, __ATOMIC_ACQUIRE) == SETTINGS_MISSING);
  z80_network_setup_usb_service(true, true);
  assert(step == SETUP_LOADING);
  finish();
  assert(strstr(output, "System configuration"));
  type("1cabcdefghijklmnopqrstuvwxyz123456789\r");
  assert(step == SETUP_EDITING && edit_position == 0);
  type("Test WiFi\r\nshort\r");
  assert(step == SETUP_EDITING && edit_position == 1);
  type("HiddenSecret\r\n");
  assert(writes == 0 && !strstr(output, "HiddenSecret"));
  finish();
  assert(writes == 2);
  assert(z80_network_config() && !z80_network_config()->fs_server[0]);
  assert(!strcmp(z80_network_config()->password, "HiddenSecret"));
  type("q2c999.1.1.1\r");
  assert(step == SETUP_EDITING);
  type("192.0.2.10junk\r");
  assert(step == SETUP_EDITING);
  type("192.0.2.10\r\n");
  finish();
  assert(writes == 3);
  assert(!strcmp(z80_network_config()->fs_server, "192.0.2.10"));
  type("qs");
  assert(!strstr(output, "HiddenSecret"));
  type("q");
  assert(step == SETUP_REBOOT && z80_network_setup_reboot_requested());
  assert(strstr(output, "reboot required"));
  z80_network_setup_reboot_queued();
  assert(!z80_network_setup_reboot_requested());
  assert(z80_network_setup_input('x'));

  restart_shell();
  assert(!changed && settings_loaded);
  type("1c\r\n\r\n");
  finish();
  assert(!changed && writes == 3);
  type("qq");
  assert(!z80_network_setup_active() && !z80_network_setup_reboot_requested());

  open_shell();
  type("3");
  type("1Fake-OpenAI-Secret\r\n");
  finish();
  expect_value("CHAT_PROVIDER", "openai");
  expect_value("CHAT_OPENAI_KEY", "Fake-OpenAI-Secret");
  assert(!strstr(output, "Fake-OpenAI-Secret"));
  type("2https://bad.example:65536/v1/chat/completions\r");
  assert(step == SETUP_EDITING);
  type("http://192.0.2.20:11434/v1/chat/completions\r\n");
  finish();
  expect_value("CHAT_PROVIDER", "compatible");
  expect_value("CHAT_ENDPOINT", "http://192.0.2.20:11434/v1/chat/completions");
  expect_value("CHAT_OPENAI_KEY", "Fake-OpenAI-Secret");
  type("3\r\n");
  finish();
  expect_value("CHAT_PROVIDER", "ollama");
  type("4phi4-mini:3.8b\r\n");
  finish();
  type("50\r1024\r\n");
  finish();
  type("6nan\r2.1\r0.7\r\n");
  finish();
  expect_value("CHAT_MODEL", "phi4-mini:3.8b");
  expect_value("CHAT_MAX_TOKENS", "1024");
  expect_value("CHAT_TEMPERATURE", "0.7");
  type("q4cWeatherSecret\r\nlat=91&lon=0\r");
  assert(step == SETUP_EDITING && edit_position == 1);
  type("lat=-33.8&lon=151.2\r\nkelvin\rmetric\r\n");
  finish();
  expect_value("OWM_KEY", "WeatherSecret");
  expect_value("OWM_LOCATION", "lat=-33.8&lon=151.2");
  expect_value("OWM_UNITS", "metric");
  assert(!strstr(output, "WeatherSecret"));
  type("q5cAEST-10AEDT,M10.1.0,M4.1.0/3\r\n");
  finish();
  expect_value("TZ", "AEST-10AEDT,M10.1.0,M4.1.0/3");
  type("q1c\r\n-\r\n");
  finish();
  assert(z80_network_config()->password[0] == '\0');
  expect_value("FS_SERVER", "192.0.2.10");
  type("q2u");
  finish();
  char value[256];
  assert(!environment_io_get("FS_SERVER", value, sizeof(value)));
  assert(z80_network_config());
  type("q5c-\r\n");
  finish();
  assert(!environment_io_get("TZ", value, sizeof(value)));
  type("q3");
  type("4");
  unsigned before = writes;
  for (unsigned i = 0; i < 256; ++i)
    type("x");
  type("\r");
  assert(step == SETUP_EDITING && writes == before);
  type("Discarded\033");
  assert(step == SETUP_MENU && writes == before);
  expect_value("CHAT_MODEL", "phi4-mini:3.8b");
  type("1-\r");
  finish();
  assert(!environment_io_get("CHAT_OPENAI_KEY", value, sizeof(value)));
  type("q1u");
  finish();
  assert(!z80_network_config());
  expect_value("OWM_KEY", "WeatherSecret");
  expect_value("CHAT_MODEL", "phi4-mini:3.8b");

  restart_shell();
  type("1cWillNotSave\033");
  assert(step == SETUP_MENU && !changed);
  z80_network_setup_usb_service(false, true);
  assert(!z80_network_setup_active());
  open_shell();
  type("1cDiscardWhenStopped");
  z80_network_setup_usb_service(true, false);
  assert(!z80_network_setup_active() && z80_network_setup_quiescent());
  assert(strstr(output, "CP/M is stopping or stopped"));

  read_error = -2;
  open_shell();
  assert(!settings_loaded && !z80_network_config());
  before = writes;
  type("1c");
  assert(step == SETUP_MENU && writes == before);
  read_error = 0;
  open_shell();
  type("1cRecoveredWiFi\rRecoveredSecret\r");
  write_error = -3;
  finish();
  assert(!settings_loaded && !changed);
  assert(strstr(output, "FAIL: ENV status=-3"));
  assert(!strstr(output, "RecoveredSecret"));
  write_error = 0;
  open_shell();
  type("1cRecoveredWiFi\rRecoveredSecret\r");
  finish();
  assert(z80_network_config());
  assert(changed);
  /* Persisted changes survive disconnect/re-entry until the requested reboot. */
  z80_network_setup_usb_service(false, true);
  open_shell();
  type("q");
  assert(z80_network_setup_reboot_requested());

  restart_shell();
  type("1cPartiallySaved\rReplacementSecret\r");
  write_fail_after = (int)writes + 1;
  finish();
  assert(changed && !settings_loaded);
  expect_value("WIFI_SSID", "PartiallySaved");
  expect_value("WIFI_PASSWORD", "RecoveredSecret");
  type("qq");
  assert(z80_network_setup_reboot_requested());
  write_fail_after = -1;

  restart_shell();
  type("1c12345678901234567890123456789012\r");
  for (unsigned i = 0; i < 64; ++i)
    type("a");
  type("\r\n");
  finish();
  assert(strlen(z80_network_config()->ssid) == 32);
  assert(strlen(z80_network_config()->password) == 64);
  type("q3");
  type("1");
  for (unsigned i = 0; i < 255; ++i)
    type("k");
  type("\r\n");
  finish();
  assert(environment_io_get("CHAT_OPENAI_KEY", value, sizeof(value)));
  assert(strlen(value) == 255);
  type("1");
  before = writes;
  for (unsigned i = 0; i < 256; ++i)
    type("z");
  type("\r\n");
  assert(step == SETUP_EDITING && writes == before);
  type("\033");
  type("4");
  for (unsigned i = 0; i < 64; ++i)
    type("m");
  type("\r\n");
  finish();
  assert(environment_io_get("CHAT_MODEL", value, sizeof(value)));
  assert(strlen(value) == 64);
  type("61.0\r\n");
  finish();
  type("6-\r\n");
  finish();
  assert(!environment_io_get("CHAT_TEMPERATURE", value, sizeof(value)));
  assert(!valid_value(TEMPERATURE, "1e0"));
  assert(valid_value(TEMPERATURE, "0") && valid_value(TEMPERATURE, "2"));
  assert(!valid_value(ENDPOINT, "http://host/v1/chat/completions#fragment"));
  assert(!valid_value(ENDPOINT, "http://host:0/v1/chat/completions"));
  assert(valid_value(ENDPOINT, "http://host:65535/v1/chat/completions"));
  puts("PASS: unified config menus, literal ENV persistence, validation, secrets, "
       "cancel/disconnect, partial failures and changed-only reboot requests");
}
