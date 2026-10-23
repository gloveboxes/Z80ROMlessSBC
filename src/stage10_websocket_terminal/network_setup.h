#ifndef Z80_NETWORK_SETUP_H
#define Z80_NETWORK_SETUP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char ssid[33];
  char password[65];
  char fs_server[16];
} z80_network_config_t;

/* Unified USB configuration shell. Initialize before launching core 1;
 * all other functions are core 1 only. */
void z80_network_setup_init(void);
void z80_network_setup_request(void);
void z80_network_setup_usb_service(bool connected, bool running);
bool z80_network_setup_input(uint8_t value);
bool z80_network_setup_active(void);
bool z80_network_setup_reboot_requested(void);
void z80_network_setup_reboot_queued(void);

/* ENV access and persistence belong exclusively to core 1. */
void z80_network_setup_core1_service(void);
bool z80_network_setup_quiescent(void);
const z80_network_config_t *z80_network_config(void);

#ifdef __cplusplus
}
#endif
#endif
