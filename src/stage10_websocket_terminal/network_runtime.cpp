#include "network_runtime.h"

#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"
#include "network_setup.h"
#include "z80sbc/io_ports.h"
#include "../common/io_service.h"

namespace {
constexpr uint32_t RETRY_DELAY_MS = 5000;
constexpr uint32_t ASSOCIATION_TIMEOUT_MS = 30000;
enum class WifiState { uninitialized, associating, online, association_backoff };
WifiState wifi_state = WifiState::uninitialized;
absolute_time_t retry_deadline = nil_time;
absolute_time_t association_deadline = nil_time;
absolute_time_t address_deadline = nil_time;
bool time_client_started;
z80_network_config_t active_config = {};
uint32_t network_generation;
char active_hostname[32];

void clear_address() {
  ++network_generation;
  z80_io_ports_set_network_address("");
  address_deadline = nil_time;
}

void start_association() {
  // lwIP copies this into the DHCP client options when it requests a lease.
  // Set it before starting each association so the router sees the same name
  // that the Z80 system information reports.
  if (z80_service_info(0, active_hostname, sizeof(active_hostname)) == 0)
    return;
  cyw43_arch_lwip_begin();
  if (netif_default != nullptr)
    netif_set_hostname(netif_default, active_hostname);
  cyw43_arch_lwip_end();
  int result = cyw43_arch_wifi_connect_async(
      active_config.ssid, active_config.password,
      active_config.password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
  if (result == PICO_OK) {
    association_deadline = make_timeout_time_ms(ASSOCIATION_TIMEOUT_MS);
    wifi_state = WifiState::associating;
  } else {
    retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
    wifi_state = WifiState::association_backoff;
    printf("[network] FAIL: association start=%d; retry in 5 seconds.\n", result);
  }
}

void refresh_network_address() {
  if (!time_reached(address_deadline))
    return;
  address_deadline = make_timeout_time_ms(1000);
  char address[16] = {};
  cyw43_arch_lwip_begin();
  if (netif_default != nullptr &&
      ip4addr_ntoa_r(netif_ip4_addr(netif_default), address,
                     sizeof(address)) == nullptr) {
    address[0] = '\0';
    printf("[network] FAIL: IPv4 address formatting.\n");
  }
  cyw43_arch_lwip_end();
  if (strcmp(address, "0.0.0.0") == 0)
    address[0] = '\0';
  char previous[16];
  z80_io_ports_get_network_address(previous, sizeof(previous));
  if (strcmp(previous, address) != 0)
    z80_io_ports_set_network_address(address);
}
}  // namespace

extern "C" uint32_t z80_network_generation(void) {
  return network_generation;
}

extern "C" bool z80_network_poll(void) {
  if (!z80_wireless_init())
    return false;
  cyw43_arch_poll();
  const z80_network_config_t *config = z80_network_config();
  if (config == nullptr) {
    if (wifi_state != WifiState::uninitialized) {
      clear_address();
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      wifi_state = WifiState::uninitialized;
      active_config = {};
    }
    return false;
  }
  if (strcmp(active_config.ssid, config->ssid) != 0 ||
      strcmp(active_config.password, config->password) != 0) {
    clear_address();
    if (wifi_state != WifiState::uninitialized)
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    active_config = *config;
    wifi_state = WifiState::uninitialized;
  }
  if (wifi_state == WifiState::uninitialized) {
    cyw43_arch_enable_sta_mode();
    start_association();
    return false;
  }
  if (wifi_state == WifiState::association_backoff) {
    if (time_reached(retry_deadline))
      start_association();
    return false;
  }

  cyw43_arch_lwip_begin();
  int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  if (wifi_state == WifiState::associating) {
    if (status == CYW43_LINK_UP) {
      int result = cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
      if (result != PICO_OK) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
        wifi_state = WifiState::association_backoff;
        printf("[network] FAIL: disabling Wi-Fi power saving=%d; retry in 5 seconds.\n",
               result);
        return false;
      }
      cyw43_arch_lwip_begin();
      if (!time_client_started) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        time_client_started = true;
      }
      cyw43_arch_lwip_end();
      wifi_state = WifiState::online;
    } else if (status < CYW43_LINK_DOWN || time_reached(association_deadline)) {
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
      wifi_state = WifiState::association_backoff;
      printf("[network] FAIL: association status=%d or timeout; retry in 5 seconds.\n",
             status);
      return false;
    } else {
      return false;
    }
  }
  if (status != CYW43_LINK_UP) {
    clear_address();
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    retry_deadline = make_timeout_time_ms(RETRY_DELAY_MS);
    wifi_state = WifiState::association_backoff;
    printf("[network] Wi-Fi link lost; retry in 5 seconds.\n");
    return false;
  }
  refresh_network_address();
  return true;
}
