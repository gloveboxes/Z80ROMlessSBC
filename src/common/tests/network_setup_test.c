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
static unsigned writes;
static char output[16384];
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
static void provision(const char *password) {
  z80_network_setup_request();
  type("Test WiFi\r\n");
  type(password);
  type("\r\n192.0.2.10\r\n");
  assert(step == SETUP_SAVING && !z80_network_setup_quiescent());
  service();
  assert(z80_network_setup_quiescent());
  z80_network_setup_usb_service(true, true);
  assert(!z80_network_setup_active());
}

int main(void) {
  z80_network_setup_init();
  assert(!z80_network_config());
  service();
  assert(__atomic_load_n(&settings_state, __ATOMIC_ACQUIRE) == SETTINGS_MISSING);
  z80_network_setup_usb_service(true, true);
  assert(step == SETUP_SSID);
  type("\r");
  assert(step == SETUP_SSID && strstr(output, "Invalid"));
  type("abcdefghijklmnopqrstuvwxyz123456789\r");
  assert(step == SETUP_SSID);
  type("WiFi\rshort\r");
  assert(step == SETUP_PASSWORD);
  type("HiddenSecret\r999.1.1.1\r");
  assert(step == SETUP_SERVER);
  type("192.0.2.10junk\r");
  assert(step == SETUP_SERVER);
  type("192.0.2.10\r\n");
  assert(writes == 0);
  service();
  assert(writes == 3);
  z80_network_setup_usb_service(true, true);
  assert(strstr(output, "Saved in ENV"));
  assert(!strstr(output, "HiddenSecret"));
  assert(strcmp(z80_network_config()->password, "HiddenSecret") == 0);
  assert(strcmp(z80_network_config()->fs_server, "192.0.2.10") == 0);
  on_core1 = true;
  assert(save_value("TZ", "UTC0") == 0);
  on_core1 = false;
  provision("");
  assert(z80_network_config()->password[0] == '\0');
  char value[256];
  assert(environment_io_get("TZ", value, sizeof(value)));
  assert(strcmp(value, "UTC0") == 0);
  z80_network_setup_init();
  service();
  assert(z80_network_config() && strcmp(z80_network_config()->ssid, "Test WiFi") == 0);
  z80_network_setup_usb_service(true, true);
  assert(!z80_network_setup_active());
  z80_network_setup_request();
  type("CancelMe\033");
  assert(!z80_network_setup_active());
  write_error = -3;
  provision("OtherSecret");
  assert(strstr(output, "FAIL: ENV status=-3"));
  assert(!strstr(output, "OtherSecret"));
  assert(z80_network_config()->password[0] == '\0');
  write_error = 0;
  read_error = -2;
  z80_network_setup_init();
  service();
  z80_network_setup_usb_service(true, true);
  assert(!z80_network_config() && strstr(output, "cannot read ENV"));
  read_error = 0;
  provision("RecoveredSecret");
  assert(z80_network_config());
  z80_network_setup_request();
  type("Discarded");
  z80_network_setup_usb_service(false, true);
  assert(!z80_network_setup_active());
  z80_network_setup_request();
  type("DiscardWhenStopped");
  z80_network_setup_usb_service(true, false);
  assert(!z80_network_setup_active() && z80_network_setup_quiescent());
  assert(strstr(output, "CP/M is stopping or stopped"));
  puts("PASS: USB setup validation/masking, core-1 ENV persistence, failures and recovery");
}
