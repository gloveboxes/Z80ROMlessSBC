#include "network_setup.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pico/util/queue.h"
#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

enum { SETTINGS_UNKNOWN, SETTINGS_MISSING, SETTINGS_READY, SETTINGS_ERROR };
enum { SETUP_IDLE, SETUP_SSID, SETUP_PASSWORD, SETUP_SERVER, SETUP_SAVING };
static queue_t requests;
static queue_t results;
static uint32_t settings_state;
static uint32_t save_pending;
static z80_network_config_t config;
static z80_network_config_t editing;
static unsigned step;
static char line[65];
static size_t line_length;
static bool line_overflow;
static bool previous_cr;
static bool auto_prompted;

static bool valid_server(const char *address) {
  unsigned first = 0;
  for (unsigned part = 0; part < 4; ++part) {
    unsigned value = 0;
    unsigned digits = 0;
    while (*address >= '0' && *address <= '9') {
      value = value * 10 + (unsigned)(*address++ - '0');
      if (++digits > 3 || value > 255)
        return false;
    }
    if (!digits)
      return false;
    if (!part)
      first = value;
    if (part < 3 && *address++ != '.')
      return false;
  }
  return *address == '\0' && first > 0 && first < 224 && first != 127;
}

static bool valid_password(const char *password) {
  size_t length = strlen(password);
  if (length == 0 || (length >= 8 && length <= 63))
    return true;
  if (length != 64)
    return false;
  for (size_t i = 0; i < length; ++i)
    if (!isxdigit((unsigned char)password[i]))
      return false;
  return true;
}

static bool valid_config(const z80_network_config_t *value) {
  return value->ssid[0] && valid_password(value->password) &&
         valid_server(value->fs_server);
}

void z80_network_setup_init(void) {
  queue_init(&requests, sizeof(z80_network_config_t), 1);
  queue_init(&results, sizeof(int), 1);
  __atomic_store_n(&settings_state, SETTINGS_UNKNOWN, __ATOMIC_RELEASE);
  __atomic_store_n(&save_pending, 0, __ATOMIC_RELEASE);
  step = SETUP_IDLE;
  auto_prompted = false;
}

static void prompt(void) {
  line_length = 0;
  line_overflow = false;
  memset(line, 0, sizeof(line));
  const char *text = step == SETUP_SSID ? "Wi-Fi SSID (1-32 bytes): " :
      step == SETUP_PASSWORD ? "Wi-Fi password (hidden; empty for open network): " :
      "Remote FS server IPv4 address (stored only): ";
  printf("\n[network setup] %s", text);
}

void z80_network_setup_request(void) {
  if (step == SETUP_SAVING) {
    printf("\n[network setup] Save in progress; please wait.\n");
    return;
  }
  memset(&editing, 0, sizeof(editing));
  previous_cr = false;
  step = SETUP_SSID;
  printf("\n[network setup] ESC cancels. Existing settings remain until saving.\n");
  prompt();
}

bool z80_network_setup_active(void) { return step != SETUP_IDLE; }

void z80_network_setup_usb_service(bool connected, bool running) {
  int result;
  if (queue_try_remove(&results, &result)) {
    step = SETUP_IDLE;
    if (result == 0)
      printf("\n[network setup] Saved in ENV; Wi-Fi will reconnect. "
             "Remote FS I/O is not implemented.\n");
    else
      printf("\n[network setup] FAIL: ENV status=%d. Settings may be partially "
             "updated; use Ctrl-] n to retry.\n", result);
  }
  if (!connected) {
    auto_prompted = false;
    if (step != SETUP_SAVING) {
      step = SETUP_IDLE;
      memset(&editing, 0, sizeof(editing));
      memset(line, 0, sizeof(line));
    }
    return;
  }
  if (!running && step != SETUP_IDLE && step != SETUP_SAVING) {
    step = SETUP_IDLE;
    memset(&editing, 0, sizeof(editing));
    memset(line, 0, sizeof(line));
    printf("\n[network setup] Cancelled: CP/M is stopping or stopped.\n");
  }
  unsigned state = __atomic_load_n(&settings_state, __ATOMIC_ACQUIRE);
  if (running && !auto_prompted && step == SETUP_IDLE &&
      state != SETTINGS_UNKNOWN) {
    auto_prompted = true;
    if (state == SETTINGS_MISSING)
      z80_network_setup_request();
    else if (state == SETTINGS_ERROR)
      printf("\n[network setup] FAIL: cannot read ENV. "
             "Use Ctrl-] n to retry provisioning.\n");
  }
}

bool z80_network_setup_input(uint8_t value) {
  if (step == SETUP_IDLE)
    return false;
  if (step == SETUP_SAVING)
    return true;
  if (value == 0x1b) {
    step = SETUP_IDLE;
    memset(&editing, 0, sizeof(editing));
    memset(line, 0, sizeof(line));
    printf("\n[network setup] Cancelled.\n");
    return true;
  }
  if (value == '\n' && previous_cr) {
    previous_cr = false;
    return true;
  }
  previous_cr = value == '\r';
  if (value == '\r' || value == '\n') {
    line[line_length] = '\0';
    bool valid = !line_overflow;
    if (step == SETUP_SSID)
      valid = valid && line_length > 0 && line_length <= 32;
    else if (step == SETUP_PASSWORD)
      valid = valid && valid_password(line);
    else
      valid = valid && valid_server(line);
    if (!valid) {
      printf("\n[network setup] Invalid value or length; please retry.\n");
      prompt();
      return true;
    }
    if (step == SETUP_SSID)
      memcpy(editing.ssid, line, line_length + 1);
    else if (step == SETUP_PASSWORD)
      memcpy(editing.password, line, line_length + 1);
    else
      memcpy(editing.fs_server, line, line_length + 1);
    if (++step == SETUP_SAVING) {
      __atomic_store_n(&save_pending, 1, __ATOMIC_RELEASE);
      if (!queue_try_add(&requests, &editing)) {
        __atomic_store_n(&save_pending, 0, __ATOMIC_RELEASE);
        step = SETUP_IDLE;
        printf("\n[network setup] FAIL: settings queue busy; retry Ctrl-] n.\n");
      } else {
        printf("\n[network setup] Saving to ENV...\n");
      }
      memset(&editing, 0, sizeof(editing));
      memset(line, 0, sizeof(line));
    } else {
      prompt();
    }
    return true;
  }
  if (value == 8 || value == 127) {
    if (line_length) {
      line[--line_length] = '\0';
      if (step != SETUP_PASSWORD)
        printf("\b \b");
    }
  } else if (value >= 32 && value != 127) {
    if (line_length < sizeof(line) - 1) {
      line[line_length++] = (char)value;
      if (step != SETUP_PASSWORD)
        printf("%c", value);
    } else {
      line_overflow = true;
    }
  }
  return true;
}

static int reload_config(void) {
  environment_io_close();
  environment_io_init(NULL);
  int status = (int8_t)environment_input(ENVIRONMENT_PORT_COMMAND);
  memset(&config, 0, sizeof(config));
  if (status == 0) {
    /* Read full ENV values so overlong credentials cannot be silently truncated. */
    char value[256];
    bool found = environment_io_get("WIFI_SSID", value, sizeof(value));
    bool valid = found && strlen(value) < sizeof(config.ssid);
    if (valid)
      strcpy(config.ssid, value);
    found = environment_io_get("WIFI_PASSWORD", value, sizeof(value));
    valid = valid && found && strlen(value) < sizeof(config.password);
    if (valid)
      strcpy(config.password, value);
    found = environment_io_get("FS_SERVER", value, sizeof(value));
    valid = valid && found && strlen(value) < sizeof(config.fs_server);
    if (valid)
      strcpy(config.fs_server, value);
    memset(value, 0, sizeof(value));
    __atomic_store_n(&settings_state,
        valid && valid_config(&config) ? SETTINGS_READY : SETTINGS_MISSING,
        __ATOMIC_RELEASE);
  } else {
    __atomic_store_n(&settings_state, SETTINGS_ERROR, __ATOMIC_RELEASE);
  }
  return status;
}

static int save_value(const char *key, const char *value) {
  environment_output(ENVIRONMENT_PORT_COMMAND, 0, NULL, 0);
  for (size_t i = 0; i <= strlen(key); ++i)
    environment_output(ENVIRONMENT_PORT_DATA, (uint8_t)key[i], NULL, 0);
  for (size_t i = 0; value[i]; ++i)
    environment_output(ENVIRONMENT_PORT_DATA, (uint8_t)value[i], NULL, 0);
  environment_output(ENVIRONMENT_PORT_COMMAND, 3, NULL, 0);
  return (int8_t)environment_input(ENVIRONMENT_PORT_COMMAND);
}

void z80_network_setup_core1_service(void) {
  if (__atomic_load_n(&settings_state, __ATOMIC_ACQUIRE) == SETTINGS_UNKNOWN) {
    int status = reload_config();
    if (status != 0)
      printf("[network] FAIL: ENV read status=%d\n", status);
  }
  z80_network_config_t request;
  if (!queue_try_remove(&requests, &request))
    return;
  int status = reload_config();
  if (status == 0)
    status = save_value("WIFI_SSID", request.ssid);
  if (status == 0)
    status = save_value("WIFI_PASSWORD", request.password);
  if (status == 0)
    status = save_value("FS_SERVER", request.fs_server);
  memset(&request, 0, sizeof(request));
  int read_status = reload_config();
  if (status == 0)
    status = read_status;
  if (!queue_try_add(&results, &status))
    printf("[network setup] FAIL: result queue full; ENV status=%d\n", status);
  __atomic_store_n(&save_pending, 0, __ATOMIC_RELEASE);
}

bool z80_network_setup_quiescent(void) {
  return !__atomic_load_n(&save_pending, __ATOMIC_ACQUIRE);
}

const z80_network_config_t *z80_network_config(void) {
  return __atomic_load_n(&settings_state, __ATOMIC_ACQUIRE) == SETTINGS_READY
             ? &config : NULL;
}
