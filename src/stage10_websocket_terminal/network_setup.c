#include "network_setup.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/util/queue.h"
#include "../common/chat_endpoint.h"
#include "../cpm/dcc_io_adapter/host/PortDrivers/environment_io.h"

enum { SETTINGS_UNKNOWN, SETTINGS_MISSING, SETTINGS_READY, SETTINGS_ERROR };
enum {
  SETUP_IDLE,
  SETUP_LOADING,
  SETUP_MENU,
  SETUP_EDITING,
  SETUP_SAVING,
  SETUP_REBOOT
};
enum { MENU_MAIN, MENU_WIFI, MENU_FS, MENU_CHAT, MENU_WEATHER, MENU_TIME };
enum {
  SSID,
  PASSWORD,
  SERVER,
  PROVIDER,
  ENDPOINT,
  API_KEY,
  MODEL,
  TOKENS,
  TEMPERATURE,
  WEATHER_KEY,
  LOCATION,
  UNITS,
  TIMEZONE,
  FIELD_COUNT
};
enum { REQUEST_LOAD, REQUEST_SAVE };

static const struct {
  const char *key;
  const char *label;
  unsigned limit;
  bool secret;
} fields[FIELD_COUNT] = {
    {"WIFI_SSID", "Wi-Fi SSID", 32, false},
    {"WIFI_PASSWORD", "Wi-Fi password", 64, true},
    {"FS_SERVER", "Remote FS server IPv4 (stored only)", 15, false},
    {"CHAT_PROVIDER", "Chat provider", 15, false},
    {"CHAT_ENDPOINT", "Full http(s) chat completions URL", 255, false},
    {"CHAT_OPENAI_KEY", "OpenAI API key", 255, true},
    {"CHAT_MODEL", "Chat model", 64, false},
    {"CHAT_MAX_TOKENS", "Maximum tokens (positive integer)", 8, false},
    {"CHAT_TEMPERATURE", "Temperature (0-2)", 8, false},
    {"OWM_KEY", "OpenWeatherMap API key", 40, true},
    {"OWM_LOCATION", "Location (city,country or lat=...&lon=...)", 64, false},
    {"OWM_UNITS", "Units (metric, imperial, standard)", 12, false},
    {"TZ", "POSIX timezone (e.g. AEST-10 or UTC0)", 255, false},
};

static queue_t requests;
static queue_t results;
static uint32_t settings_state;
static uint32_t save_pending;
static z80_network_config_t config;
static unsigned step, menu, edit_count, edit_position;
static unsigned edit_fields[3];
static char values[FIELD_COUNT][256];
static char editing[3][256];
static char line[256];
static size_t line_length;
static bool line_overflow, previous_cr, auto_prompted;
static bool settings_loaded, changed, reboot_requested;

static bool valid_server(const char *address) {
  unsigned first = 0;
  for (unsigned part = 0; part < 4; ++part) {
    unsigned value = 0, digits = 0;
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

static bool decimal(const char *text, double min, double max) {
  if (!*text)
    return false;
  for (const char *p = text; *p; ++p)
    if (!isdigit((unsigned char)*p) && *p != '.')
      return false;
  char *end;
  double value = strtod(text, &end);
  return !*end && isfinite(value) && value >= min && value <= max;
}

static bool valid_location(const char *text) {
  if (strncmp(text, "lat=", 4))
    return true;
  char *end;
  double latitude = strtod(text + 4, &end);
  if (end == text + 4 || !isfinite(latitude) || latitude < -90 ||
      latitude > 90 || strncmp(end, "&lon=", 5))
    return false;
  const char *lon = end + 5;
  double longitude = strtod(lon, &end);
  return end != lon && !*end && isfinite(longitude) && longitude >= -180 &&
         longitude <= 180;
}

static bool valid_value(unsigned field, const char *text) {
  if (strlen(text) > fields[field].limit)
    return false;
  if (!*text)
    return true;
  switch (field) {
  case PASSWORD:
    return valid_password(text);
  case SERVER:
    return valid_server(text);
  case PROVIDER:
    return !strcmp(text, "openai") || !strcmp(text, "compatible") ||
           !strcmp(text, "ollama");
  case ENDPOINT: {
    z80_chat_endpoint_t parsed = {0};
    return z80_chat_parse_endpoint(text, &parsed);
  }
  case TOKENS:
    for (const char *p = text; *p; ++p)
      if (!isdigit((unsigned char)*p))
        return false;
    return strtoul(text, NULL, 10) > 0;
  case TEMPERATURE:
    return decimal(text, 0, 2);
  case LOCATION:
    return valid_location(text);
  case UNITS:
    return !strcmp(text, "metric") || !strcmp(text, "imperial") ||
           !strcmp(text, "standard");
  case API_KEY:
  case WEATHER_KEY:
  case TIMEZONE:
    for (const char *p = text; *p; ++p)
      if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127)
        return false;
    return true;
  default:
    return true;
  }
}

static void clear_edit(void) {
  memset(editing, 0, sizeof(editing));
  memset(line, 0, sizeof(line));
  line_length = 0;
  line_overflow = false;
}

static void print_menu(void) {
  static const char *titles[] = {
      "System configuration", "Wi-Fi",
      "Remote filesystem",    "OpenAI / Ollama / compatible chat",
      "OpenWeatherMap",       "Timezone"};
  printf("\n%s\n", titles[menu]);
  switch (menu) {
  case MENU_MAIN:
    printf("  1 - Wi-Fi\n  2 - Remote filesystem\n"
           "  3 - OpenAI / Ollama / compatible chat\n"
           "  4 - OpenWeatherMap\n  5 - Timezone\n"
           "  S - show all settings (secrets masked)\n"
           "  Q - return to terminal\n");
    break;
  case MENU_WIFI:
    printf("  C - configure SSID and password\n"
           "  U - clear Wi-Fi credentials (disconnects Wi-Fi)\n");
    break;
  case MENU_FS:
    printf("  C - configure server IPv4\n  U - clear server address\n"
           "  Remote FS I/O is not implemented.\n");
    break;
  case MENU_CHAT:
    printf("  1 - configure OpenAI\n"
           "  2 - configure OpenAI-compatible endpoint\n"
           "  3 - configure Ollama (OpenAI-compatible /v1/chat/completions)\n"
           "  4 - configure model\n  5 - configure maximum tokens\n"
           "  6 - configure temperature\n"
           "  Only OpenAI uses CHAT_OPENAI_KEY; it is never sent to other "
           "providers.\n");
    break;
  case MENU_WEATHER:
    printf("  C - configure API key, location and units\n");
    break;
  case MENU_TIME:
    printf("  C - configure POSIX TZ\n"
           "  POSIX signs are reversed: AEST-10 means UTC+10.\n"
           "  Full daylight-saving rules are supported; clearing uses UTC.\n");
    break;
  }
  if (menu != MENU_MAIN)
    printf("  S - show current settings\n  Q - return to main menu\n");
  printf("config> ");
}

static bool in_menu(unsigned field) {
  return menu == MENU_MAIN || (menu == MENU_WIFI && field <= PASSWORD) ||
         (menu == MENU_FS && field == SERVER) ||
         (menu == MENU_CHAT && field >= PROVIDER && field <= TEMPERATURE) ||
         (menu == MENU_WEATHER && field >= WEATHER_KEY && field <= UNITS) ||
         (menu == MENU_TIME && field == TIMEZONE);
}

static void show_settings(void) {
  printf("\n");
  for (unsigned i = 0; i < FIELD_COUNT; ++i)
    if (in_menu(i))
      printf("  %s: %s\n", fields[i].key,
             !values[i][0]      ? "(not set; service default)"
             : fields[i].secret ? "(set; hidden)"
                                : values[i]);
}

static bool submit(unsigned request, unsigned state) {
  if (!queue_try_add(&requests, &request)) {
    printf("\n[config] FAIL: settings queue busy; retry Ctrl-] c.\n");
    return false;
  }
  step = state;
  __atomic_store_n(&save_pending, 1, __ATOMIC_RELEASE);
  return true;
}

void z80_network_setup_init(void) {
  queue_init(&requests, sizeof(unsigned), 1);
  queue_init(&results, sizeof(int), 1);
  __atomic_store_n(&settings_state, SETTINGS_UNKNOWN, __ATOMIC_RELEASE);
  __atomic_store_n(&save_pending, 0, __ATOMIC_RELEASE);
  step = SETUP_IDLE;
  auto_prompted = false;
  settings_loaded = changed = reboot_requested = false;
  clear_edit();
  memset(values, 0, sizeof(values));
}

void z80_network_setup_request(void) {
  if (step == SETUP_SAVING || step == SETUP_LOADING || step == SETUP_REBOOT) {
    printf("\n[config] Operation in progress; please wait.\n");
    return;
  }
  clear_edit();
  previous_cr = false;
  auto_prompted = true;
  settings_loaded = false;
  menu = MENU_MAIN;
  printf("\n[config] Loading ENV. Secrets are hidden; ENV storage is "
         "plaintext.\n");
  submit(REQUEST_LOAD, SETUP_LOADING);
}

bool z80_network_setup_active(void) { return step != SETUP_IDLE; }

bool z80_network_setup_reboot_requested(void) { return reboot_requested; }

void z80_network_setup_reboot_queued(void) { reboot_requested = false; }

void z80_network_setup_usb_service(bool connected, bool running) {
  int result;
  if (queue_try_remove(&results, &result)) {
    bool saved = step == SETUP_SAVING;
    clear_edit();
    step = SETUP_MENU;
    settings_loaded = result == 0;
    if (result)
      printf(
          "\n[config] FAIL: ENV status=%d. Settings may be partially updated; "
          "retry Ctrl-] c before editing.\n",
          result);
    else if (saved)
      printf("\n[config] Settings saved in ENV.%s\n",
             changed ? " A reboot is required when you quit this shell."
                     : " No changes; no reboot required.");
    if (connected && running)
      print_menu();
  }
  if (step == SETUP_REBOOT)
    return;
  if (!connected || !running) {
    if (step != SETUP_SAVING && step != SETUP_LOADING) {
      if (connected && step != SETUP_IDLE)
        printf("\n[config] Cancelled: CP/M is stopping or stopped.\n");
      step = SETUP_IDLE;
      clear_edit();
      memset(values, 0, sizeof(values));
    }
    if (!connected)
      auto_prompted = false;
    return;
  }
  unsigned state = __atomic_load_n(&settings_state, __ATOMIC_ACQUIRE);
  if (!auto_prompted && step == SETUP_IDLE && state != SETTINGS_UNKNOWN) {
    auto_prompted = true;
    if (state == SETTINGS_MISSING) {
      printf("\n[config] Wi-Fi is not configured. Select 1 to configure it; "
             "Q continues using USB only.\n");
      z80_network_setup_request();
    } else if (state == SETTINGS_ERROR) {
      printf("\n[config] FAIL: cannot read ENV. Use Ctrl-] c to retry.\n");
    }
  }
}

static void prompt(void) {
  memset(line, 0, sizeof(line));
  line_length = 0;
  line_overflow = false;
  unsigned field = edit_fields[edit_position];
  printf("\n%s [%s]: ", fields[field].label,
         !values[field][0]      ? "not set"
         : fields[field].secret ? "set; hidden"
                                : values[field]);
}

static void begin_edit(unsigned first, unsigned count, unsigned position) {
  clear_edit();
  edit_count = count;
  edit_position = position;
  for (unsigned i = 0; i < count; ++i) {
    edit_fields[i] = first + i;
    strcpy(editing[i], values[first + i]);
  }
  step = SETUP_EDITING;
  printf("\nEnter keeps the current value; '-' clears it "
         "(empty Wi-Fi password selects open network). ESC cancels unsaved "
         "edits.\n");
  prompt();
}

static void save_edits(void) {
  if (submit(REQUEST_SAVE, SETUP_SAVING))
    printf("\n[config] Saving to ENV...\n");
  else {
    clear_edit();
    step = SETUP_MENU;
    print_menu();
  }
}

static void menu_command(int command) {
  command = toupper((unsigned char)command);
  if (command == 'Q') {
    if (menu == MENU_MAIN) {
      if (changed) {
        step = SETUP_REBOOT;
        reboot_requested = true;
        clear_edit();
        memset(values, 0, sizeof(values));
        printf("\n[config] Configuration changed; reboot required. "
               "Flushing disks and stopping CP/M before reboot...\n");
        return;
      }
      step = SETUP_IDLE;
      clear_edit();
      memset(values, 0, sizeof(values));
      printf("\n[config] Returning to terminal.\n");
      return;
    }
    menu = MENU_MAIN;
  } else if (!settings_loaded) {
    printf(
        "\n[config] Cannot read ENV. Use Ctrl-] c to retry, or Q to quit.\n");
  } else if (command == 'S') {
    show_settings();
  } else if (menu == MENU_MAIN && command >= '1' && command <= '5') {
    menu = (unsigned)(command - '0');
  } else if (menu == MENU_WIFI && command == 'C') {
    begin_edit(SSID, 2, 0);
    return;
  } else if ((menu == MENU_WIFI || menu == MENU_FS) && command == 'U') {
    begin_edit(menu == MENU_WIFI ? SSID : SERVER, menu == MENU_WIFI ? 2 : 1, 0);
    memset(editing, 0, sizeof(editing));
    save_edits();
    return;
  } else if (menu == MENU_FS && command == 'C') {
    begin_edit(SERVER, 1, 0);
    return;
  } else if (menu == MENU_CHAT && command >= '1' && command <= '3') {
    begin_edit(PROVIDER, command == '1' ? 3 : 2, command == '1' ? 2 : 1);
    strcpy(editing[0], command == '1'   ? "openai"
                       : command == '2' ? "compatible"
                                        : "ollama");
    if (command == '1') {
      editing[1][0] = '\0';
    }
    return;
  } else if (menu == MENU_CHAT && command >= '4' && command <= '6') {
    begin_edit(MODEL + (unsigned)(command - '4'), 1, 0);
    return;
  } else if (menu == MENU_WEATHER && (command == 'C' || command == '1')) {
    begin_edit(WEATHER_KEY, 3, 0);
    return;
  } else if (menu == MENU_TIME && command == 'C') {
    begin_edit(TIMEZONE, 1, 0);
    return;
  } else if (command != '\r' && command != '\n') {
    printf("\n[config] Unknown command.\n");
  }
  print_menu();
}

bool z80_network_setup_input(uint8_t value) {
  if (step == SETUP_IDLE)
    return false;
  if (step == SETUP_SAVING || step == SETUP_LOADING || step == SETUP_REBOOT)
    return true;
  if (value == '\n' && previous_cr) {
    previous_cr = false;
    return true;
  }
  previous_cr = value == '\r';
  if (value == 0x1b) {
    clear_edit();
    step = SETUP_MENU;
    printf("\n[config] Unsaved edits cancelled.\n");
    print_menu();
    return true;
  }
  if (step == SETUP_MENU) {
    if (value != ' ' && value != '\t')
      menu_command(value);
    return true;
  }
  unsigned field = edit_fields[edit_position];
  if (value == '\r' || value == '\n') {
    const char *candidate = !line_length         ? editing[edit_position]
                            : !strcmp(line, "-") ? ""
                                                 : line;
    if (line_overflow || !valid_value(field, candidate)) {
      printf("\n[config] Invalid value or length (maximum %u bytes); retry.\n",
             fields[field].limit);
      prompt();
      return true;
    }
    if (*line)
      strcpy(editing[edit_position], candidate);
    if (++edit_position == edit_count) {
      if (edit_fields[0] == PROVIDER && strcmp(editing[0], "openai") &&
          !editing[1][0]) {
        printf("\n[config] A compatible/Ollama endpoint URL is required.\n");
        edit_position = 1;
        prompt();
      } else {
        save_edits();
      }
    } else {
      prompt();
    }
    return true;
  }
  if (value == 8 || value == 127) {
    if (line_length) {
      line[--line_length] = '\0';
      if (!fields[field].secret)
        printf("\b \b");
    }
  } else if (value >= 32 && value != 127) {
    if (line_length < sizeof(line) - 1) {
      line[line_length++] = (char)value;
      line[line_length] = '\0';
      if (!fields[field].secret)
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
    char value[256];
    bool found = environment_io_get("WIFI_SSID", value, sizeof(value));
    bool valid = found && value[0] && strlen(value) < sizeof(config.ssid);
    if (valid)
      strcpy(config.ssid, value);
    found = environment_io_get("WIFI_PASSWORD", value, sizeof(value));
    if (!found)
      value[0] = '\0';
    valid = valid && valid_password(value) &&
            strlen(value) < sizeof(config.password);
    if (valid)
      strcpy(config.password, value);
    found = environment_io_get("FS_SERVER", value, sizeof(value));
    if (found && valid_server(value))
      strcpy(config.fs_server, value);
    memset(value, 0, sizeof(value));
    __atomic_store_n(&settings_state, valid ? SETTINGS_READY : SETTINGS_MISSING,
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

static int delete_value(const char *key) {
  char value[256];
  if (!environment_io_get(key, value, sizeof(value)))
    return 0;
  memset(value, 0, sizeof(value));
  environment_output(ENVIRONMENT_PORT_COMMAND, 0, NULL, 0);
  for (size_t i = 0; i <= strlen(key); ++i)
    environment_output(ENVIRONMENT_PORT_DATA, (uint8_t)key[i], NULL, 0);
  environment_output(ENVIRONMENT_PORT_COMMAND, 4, NULL, 0);
  return (int8_t)environment_input(ENVIRONMENT_PORT_COMMAND);
}

void z80_network_setup_core1_service(void) {
  if (__atomic_load_n(&settings_state, __ATOMIC_ACQUIRE) == SETTINGS_UNKNOWN) {
    int status = reload_config();
    if (status != 0)
      printf("[config] FAIL: ENV read status=%d\n", status);
  }
  unsigned request;
  if (!queue_try_remove(&requests, &request))
    return;
  int status = reload_config();
  if (request == REQUEST_SAVE && status == 0) {
    for (unsigned i = 0; i < edit_count && status == 0; ++i) {
      unsigned field = edit_fields[i];
      char current[256];
      bool found =
          environment_io_get(fields[field].key, current, sizeof(current));
      bool same = found ? !strcmp(current, editing[i]) : !editing[i][0];
      memset(current, 0, sizeof(current));
      if (same)
        continue;
      status = editing[i][0] || field == PASSWORD
                   ? save_value(fields[field].key, editing[i])
                   : delete_value(fields[field].key);
      if (status == 0)
        changed = true;
    }
    int read_status = reload_config();
    if (status == 0)
      status = read_status;
  }
  memset(values, 0, sizeof(values));
  if (!status)
    for (unsigned i = 0; i < FIELD_COUNT; ++i)
      environment_io_get(fields[i].key, values[i], sizeof(values[i]));
  clear_edit();
  if (!queue_try_add(&results, &status))
    printf("[config] FAIL: result queue full; ENV status=%d\n", status);
  __atomic_store_n(&save_pending, 0, __ATOMIC_RELEASE);
}

bool z80_network_setup_quiescent(void) {
  return !__atomic_load_n(&save_pending, __ATOMIC_ACQUIRE);
}

const z80_network_config_t *z80_network_config(void) {
  return __atomic_load_n(&settings_state, __ATOMIC_ACQUIRE) == SETTINGS_READY
             ? &config
             : NULL;
}
