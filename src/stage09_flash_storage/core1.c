#include <stdio.h>

#include "pico/flash.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/io_ports.h"
#include "z80sbc/terminal.h"
#include "ansi_input.h"
#include "usb_console.h"
#include "core_coordination.h"

#ifdef Z80_STAGE10_NETWORK
#include "network_runtime.h"
#include "network_setup.h"
#endif

static bool command_pending;
static bool usb_was_connected;
static uint32_t previous_cpu_state;
static bool previous_cr;
static uint32_t reported_rx_drops;
static uint32_t reported_tx_drops;
#ifdef Z80_STAGE10_NETWORK
static bool browser_was_connected;
static bool config_reboot_queue_full;
#endif

static void forward_terminal_byte(uint8_t value) {
  if (value == 0)
    return;
  if (value == '\n' && previous_cr) {
    previous_cr = false;
    return;
  }
  previous_cr = value == '\r';
#ifdef Z80_STAGE10_NETWORK
  z80_terminal_usb_receive(value);
#else
  z80_terminal_receive(&value, 1);
#endif
}

static void receive_usb_byte(uint8_t value) {
  uint32_t state = __atomic_load_n(&cpu_state, __ATOMIC_ACQUIRE);
  if (command_pending) {
    command_pending = false;
    if (value != USB_COMMAND_ESCAPE) {
#ifdef Z80_STAGE10_NETWORK
      if (value == 'c') {
        if ((state & (CPU_RUNNING | CPU_STOPPING)) == CPU_RUNNING)
          z80_network_setup_request();
        else
          printf("\n[config] Rejected: CP/M must be running, not stopping.\n");
        return;
      }
#endif
      if (value == 'x' && (state & CPU_LOADING) && !Z80_STAGE09_AUTOBOOT)
        __atomic_store_n(&load_cancelled, 1, __ATOMIC_RELEASE);
      else {
        uint16_t command = value;
        if (!queue_try_add(&control_commands, &command))
          printf("\n[diag] FAIL: supervisor command queue full; retry command.\n");
      }
      return;
    }
  } else if (value == USB_COMMAND_ESCAPE) {
    ansi_input_reset();
    command_pending = true;
    return;
  }
  if (state & CPU_LOADING) {
    if (value == 'x' && !Z80_STAGE09_AUTOBOOT)
      __atomic_store_n(&load_cancelled, 1, __ATOMIC_RELEASE);
    return;
  }
#ifdef Z80_STAGE10_NETWORK
  if ((state & (CPU_RUNNING | CPU_STOPPING)) == CPU_RUNNING &&
      z80_network_setup_input(value))
    return;
  if (z80_terminal_browser_connected())
    return;
#endif
  if (!(state & CPU_RUNNING)) {
    if (Z80_STAGE09_AUTOBOOT)
      printf("\n[diag] CPU held; Ctrl-] s for status, "
             "Ctrl-] r to retry automatic boot.\n");
    else
      printf("\n[diag] CPU held; use Ctrl-] l then Ctrl-] g.\n");
    return;
  }
  forward_terminal_byte(
      ansi_input_process(value, to_ms_since_boot(get_absolute_time())));
}

static void service_usb(void) {
  bool connected = stdio_usb_connected();
  uint32_t state = __atomic_load_n(&cpu_state, __ATOMIC_ACQUIRE);
  if (state != previous_cpu_state) {
    ansi_input_reset();
    previous_cr = false;
    previous_cpu_state = state;
  }
#ifdef Z80_STAGE10_NETWORK
  if (connected != usb_was_connected)
    z80_terminal_usb_connected(connected);
  bool browser_connected = z80_terminal_browser_connected();
  if (browser_connected != browser_was_connected) {
    ansi_input_reset();
    previous_cr = false;
    browser_was_connected = browser_connected;
  }
  z80_network_setup_usb_service(
      connected, (state & (CPU_RUNNING | CPU_STOPPING)) == CPU_RUNNING);
  if (z80_network_setup_reboot_requested()) {
    uint16_t command = USB_CONFIG_REBOOT;
    if (queue_try_add(&control_commands, &command)) {
      z80_network_setup_reboot_queued();
      config_reboot_queue_full = false;
    } else if (!config_reboot_queue_full) {
      printf("\n[config] Supervisor queue full; retrying orderly reboot.\n");
      config_reboot_queue_full = true;
    }
  }
#else
  z80_terminal_set_connected(connected);
#endif
  __atomic_store_n(&usb_state, (connected ? USB_CONNECTED : 0)
#ifdef Z80_STAGE10_NETWORK
                   | (browser_connected ? USB_BROWSER : 0)
                   | (z80_network_setup_active() ? USB_SETUP : 0)
#endif
                   , __ATOMIC_RELEASE);
  if (connected != usb_was_connected)
    ansi_input_reset();
  if (connected && !usb_was_connected) {
    command_pending = false;
    previous_cr = false;
    uint16_t command = USB_REPORT_CONNECTION;
    if (!queue_try_add(&control_commands, &command))
      printf("\n[diag] FAIL: connection report queue full; Ctrl-] s for status.\n");
  }
  usb_was_connected = connected;
  if (!connected)
    return;
  for (unsigned int count = 0; count < 32; ++count) {
    int input = getchar_timeout_us(0);
    if (input < 0)
      break;
    receive_usb_byte((uint8_t)input);
  }
  if ((state & CPU_RUNNING) && !command_pending
#ifdef Z80_STAGE10_NETWORK
      && !z80_network_setup_active() && !z80_terminal_browser_connected()
#endif
  )
    forward_terminal_byte(
        ansi_input_process(0, to_ms_since_boot(get_absolute_time())));
  if (connected && z80_terminal_output_pending()
#ifdef Z80_STAGE10_NETWORK
      && !z80_network_setup_active() && !z80_terminal_browser_connected()
#endif
  ) {
    uint8_t data[64];
    size_t capacity = tud_cdc_write_available();
    if (capacity > sizeof(data))
      capacity = sizeof(data);
    size_t count = z80_terminal_peek(data, capacity);
    if (count) {
      size_t accepted = z80_usb_console_write(data, count);
      z80_terminal_supply(data, accepted);
    }
  }
  uint32_t rx = z80_terminal_rx_drop_count();
  uint32_t tx = z80_terminal_tx_drop_count();
  if (rx != reported_rx_drops || tx != reported_tx_drops) {
    printf("\n[diag] FAIL: terminal overflow rx=%lu tx=%lu; "
           "reduce paste/output rate and repeat acceptance.\n",
           (unsigned long)rx, (unsigned long)tx);
    reported_rx_drops = rx;
    reported_tx_drops = tx;
  }
}

static void core1_service(void) {
  bool active = __atomic_load_n(&workers_ready, __ATOMIC_ACQUIRE);
  if (active) {
    if (__atomic_load_n(&flash_pause_request, __ATOMIC_ACQUIRE)) {
      if (!__atomic_load_n(&flash_paused, __ATOMIC_ACQUIRE)) {
        z80_flash_core1_service();
        z80_io_ports_core1_service();
#ifdef Z80_STAGE10_NETWORK
        z80_network_setup_core1_service();
#endif
        if (z80_flash_disk_quiescent() && z80_io_ports_quiescent()
#ifdef Z80_STAGE10_NETWORK
            && z80_network_setup_quiescent()
#endif
        )
          __atomic_store_n(&flash_paused, 1, __ATOMIC_RELEASE);
      }
    } else {
      __atomic_store_n(&flash_paused, 0, __ATOMIC_RELEASE);
      z80_flash_core1_service();
      z80_io_ports_core1_service();
#ifdef Z80_STAGE10_NETWORK
      z80_network_setup_core1_service();
#endif
    }
  }
  z80_usb_console_service();
  service_usb();
#ifdef Z80_STAGE10_NETWORK
  if (active) {
    if (__atomic_load_n(&flash_pause_request, __ATOMIC_ACQUIRE))
      z80_network_terminal_core1_service();
    else
      z80_network_core1_service();
  }
#endif
}

void core1_main(void) {
  bool flash_ready = flash_safe_execute_core_init();
  bool usb_ready = z80_usb_console_init();
  if (!flash_ready || !usb_ready)
    printf("\n[diag] FAIL: core 1 USB/flash-safe initialization.\n");
  __atomic_store_n(&core1_ready, flash_ready && usb_ready ? 1u : 2u,
                    __ATOMIC_RELEASE);
  while (true) {
    if (usb_ready)
      core1_service();
    tight_loop_contents();
  }
}
