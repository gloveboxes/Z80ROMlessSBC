#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/flash_layout.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/io_ports.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"
#include "z80sbc/terminal.h"
#include "ansi_input.h"
#include "disk_diagnostic_image.h"

#ifdef Z80_STAGE10_NETWORK
#include "network_runtime.h"
#include "network_setup.h"
#endif

#ifndef Z80_STAGE09_AUTOBOOT
#define Z80_STAGE09_AUTOBOOT 0
#endif

enum {
  RUN_CLOCK_HZ = 20000000,
  SYSTEM_CLOCK_HZ = 120000000,
  USB_COMMAND_ESCAPE = 0x1D,
  DISK_READY = 1,
  DISK_BUSY = 8,
  DIAGNOSTIC_BYTES = Z80_FLASH_DISK_SLOT_COUNT * Z80_FLASH_DISK_SLOT_BYTES,
};

static bool boot_attempted;
static bool image_verified;
static bool running;
static bool core1_started;
static bool command_pending;
static bool usb_was_connected;
#ifdef Z80_STAGE10_NETWORK
static bool browser_was_connected;
_Alignas(8) static uint32_t core1_network_stack[8192 / sizeof(uint32_t)];
#endif
static bool stop_requested;
static bool flush_submitted;
static bool bios_announced;
static bool ccp_announced;
static bool previous_cr;
static bool boot_timed_out;
static bool reset_was_watchdog;
static bool reset_report_pending;
static z80_io_trap_reset_fault_t reset_fault;
static const char *boot_failure;
static uint32_t boot_markers;
static uint32_t flash_pause_request;
static uint32_t flash_paused;
static bool flash_resuming;
static uint32_t reported_rx_drops;
static uint32_t reported_tx_drops;
static absolute_time_t boot_deadline;
static volatile bool diagnostic_active;
static volatile uint32_t diagnostic_count;
static volatile uint32_t diagnostic_result;
static uint32_t diagnostic_next_progress;
static uint32_t diagnostic_fault_offset;
static uint8_t diagnostic_expected;
static uint8_t diagnostic_actual;

static uint8_t virtual_read(uint8_t port, void *context) {
  (void)context;
  if (z80_io_ports_handles(port))
    return z80_io_port_in(port);
  return port >= 0x10 && port <= 0x14
             ? z80_flash_disk_io_read(port)
             : z80_terminal_io_read(port);
}

static void virtual_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  if (diagnostic_active && port == 0x06) {
    uint32_t offset = diagnostic_count;
    if (offset < DIAGNOSTIC_BYTES) {
      const uint8_t *source =
          (const uint8_t *)(XIP_BASE + Z80_FLASH_DISK_OFFSET);
      if (value != source[offset] && diagnostic_result == 0) {
        diagnostic_fault_offset = offset;
        diagnostic_expected = source[offset];
        diagnostic_actual = value;
        diagnostic_result = 0xE1;
      }
    } else {
      diagnostic_result = 0xE1;
    }
    diagnostic_count = offset + 1;
    return;
  }
  if (diagnostic_active && port == Z80_BOOT_REPORT_PORT) {
    if (diagnostic_result == 0)
      diagnostic_result = value == 0x5A &&
                          diagnostic_count == DIAGNOSTIC_BYTES ? 0x5A : 0xE1;
    return;
  }
  if (port == Z80_BOOT_REPORT_PORT) {
    if (value == Z80_BOOT_REPORT_BIOS)
      __atomic_fetch_or(&boot_markers, 1u, __ATOMIC_RELEASE);
    else if (value == Z80_BOOT_REPORT_CCP)
      __atomic_fetch_or(&boot_markers, 2u, __ATOMIC_RELEASE);
  } else if (port >= 0x10 && port <= 0x14) {
    z80_flash_disk_io_write(port, value);
  } else if (z80_io_ports_handles(port)) {
    z80_io_port_out(port, value);
  } else {
    z80_terminal_io_write(port, value);
  }
}

static void core1_main(void) {
  while (true) {
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
      z80_network_core1_service();
#endif
    }
    tight_loop_contents();
  }
}

static void hold_cpu(void) {
  z80_io_trap_disable();
  z80_cpu_fail_closed();
  ansi_input_reset();
  running = false;
  image_verified = false;
  stop_requested = false;
  diagnostic_active = false;
}

static const char *fault_name(z80_flash_fault_point_t point) {
  static const char *const names[] = {
    "none", "after journal data", "after journal header",
    "after target erase", "after partial target program",
    "after target verification", "after journal header clear",
    "safe-execute entry failure", "safe-execute exit failure",
  };
  return point <= Z80_FLASH_FAULT_SAFE_EXECUTE_EXIT
             ? names[(unsigned int)point] : "invalid";
}

static const char *boot_error_name(z80_boot_error_t error) {
  static const char *const names[] = {
    "none", "journal recovery",
    "manifest (requires 65536 bytes and JP FD00h)",
    "payload CRC", "loader controls", "SRAM write", "SRAM read",
    "SRAM mismatch", "flash-safe core initialization", "cancelled",
  };
  return (unsigned int)error < sizeof(names) / sizeof(names[0])
             ? names[(unsigned int)error] : "invalid";
}

static bool observe_boot(const z80_boot_status_t *status, void *context) {
  (void)context;
  static const char *const phases[] = {
    "journal recovery", "manifest/CRC validation", "SRAM load",
    "SRAM verify", "verified; RESET held LOW",
  };
  if (status->completed % 4096u == 0 &&
      (!Z80_STAGE09_AUTOBOOT || stdio_usb_connected()))
    printf("[boot] %s: %lu/%lu\n", phases[status->phase],
           (unsigned long)status->completed, (unsigned long)status->total);
  if (Z80_STAGE09_AUTOBOOT)
    return true;
  for (unsigned int count = 0; count < 32; ++count) {
    int input = getchar_timeout_us(0);
    if (input < 0)
      break;
    if (input == 'x')
      return false;
  }
  return stdio_usb_connected();
}

static void load_image(void) {
  if (boot_attempted || running || core1_started) {
    printf("\n[diag] Load already attempted; stop and reboot Pico to reload.\n");
    return;
  }
  boot_attempted = true;
  if (!Z80_STAGE09_AUTOBOOT)
    printf("\n[boot] Load/verify only; x cancels at a 256-byte boundary.\n");
  if (!z80_flash_storage_init_observed(observe_boot, NULL)) {
    const z80_boot_status_t *status = z80_flash_boot_status();
    boot_failure = boot_error_name(status->error);
    hold_cpu();
    printf("[boot] FAIL: %s phase=%u completed=%lu address=%04x "
           "expected=%02x actual=%02x; RESET held LOW\n",
           boot_error_name(status->error), (unsigned int)status->phase,
           (unsigned long)status->completed, status->address,
           status->expected, status->actual);
    return;
  }
  image_verified = true;
  if (!Z80_STAGE09_AUTOBOOT)
    printf("[boot] PASS: 65536 bytes loaded and independently read back; "
           "CPU not running. Ctrl-] g starts CP/M at nominal 20 MHz.\n");
}

static void start_cpm(void) {
  if (!image_verified || running || core1_started) {
    printf("\n[diag] Start rejected: first load/verify with Ctrl-] l; "
           "a stopped run requires Pico reboot.\n");
    return;
  }
  __atomic_store_n(&boot_markers, 0, __ATOMIC_RELEASE);
  bios_announced = ccp_announced = false;
  boot_timed_out = false;
  z80_io_ports_init();
#ifdef Z80_STAGE10_NETWORK
  multicore_launch_core1_with_stack(core1_main, core1_network_stack,
                                   sizeof(core1_network_stack));
#else
  multicore_launch_core1(core1_main);
#endif
  core1_started = true;
  if (!z80_io_trap_enable(virtual_read, virtual_write, NULL) ||
      !z80_cpu_release_reset_and_run(RUN_CLOCK_HZ)) {
    boot_failure = "trap/clock start";
    hold_cpu();
    printf("[boot] FAIL: trap/clock start; RESET held LOW\n");
    return;
  }
  running = true;
  boot_deadline = make_timeout_time_ms(10000);
  if (!Z80_STAGE09_AUTOBOOT)
    printf("\n[boot] CPU released from reset; configured clock=%lu Hz. "
           "Waiting for BIOS evidence, not yet a CP/M PASS.\n",
           (unsigned long)z80_clock_get_hz());
}

static void start_disk_diagnostic(void) {
  if (!image_verified || core1_started || running) {
    printf("\n[diag] Disk test requires Ctrl-] l and no prior run.\n");
    return;
  }
  image_verified = false;
  diagnostic_count = diagnostic_result = 0;
  diagnostic_fault_offset = UINT32_MAX;
  diagnostic_next_progress = 65536;
  if (!z80_cpu_load_and_verify(z80_disk_diagnostic,
                               sizeof(z80_disk_diagnostic))) {
    hold_cpu();
    printf("\n[diag] FAIL: disk diagnostic load/readback; RESET held LOW\n");
    return;
  }
  diagnostic_active = true;
  if (!z80_io_trap_enable(virtual_read, virtual_write, NULL) ||
      !z80_cpu_release_reset_and_run(RUN_CLOCK_HZ)) {
    hold_cpu();
    printf("\n[diag] FAIL: disk diagnostic start; RESET held LOW\n");
    return;
  }
  running = true;
  boot_deadline = make_timeout_time_ms(300000);
  printf("\n[diag] Read-only Z80 INIR/OTIR disk test at nominal 20 MHz: "
         "all 4 drives x 2560 records x 128 bytes. No disk writes. "
         "CP/M SRAM image replaced; reboot afterward.\n");
}

static void print_reset_status(void) {
  const char *reason = "none";
  if (reset_fault.reason == Z80_IO_TRAP_RESET_RELEASE_TIMEOUT)
    reason = "release-timeout";
  else if (reset_fault.reason == Z80_IO_TRAP_RESET_INVALID_CONTROLS)
    reason = "invalid-controls";
  printf("[reset] watchdog=%u io_fault=%s gpio=%08lx\n",
         reset_was_watchdog, reason, (unsigned long)reset_fault.gpio);
}

static void print_status(void) {
  print_reset_status();
#ifdef Z80_STAGE10_NETWORK
  char address[16];
  bool has_address = z80_io_ports_get_network_address(address, sizeof(address)) != 0;
  printf("[network] browser=%u setup=%u ip=%s\n",
         z80_terminal_browser_connected(), z80_network_setup_active(),
         has_address ? address : "unavailable");
#endif
  const z80_boot_status_t *boot = z80_flash_boot_status();
  printf("\n[diag] running=%u verified=%u clock=%lu BIOS=%u CCP_handoff=%u "
         "disk=%02lx fatal=%u rx_drops=%lu tx_drops=%lu "
         "trap_timeouts=%lu control_errors=%lu inactive_irqs=%lu boot_error=%s "
         "failure=%s\n",
         running, image_verified, (unsigned long)z80_clock_get_hz(),
         bios_announced, ccp_announced,
         (unsigned long)z80_flash_disk_status(),
         z80_flash_disk_has_fatal_error(),
         (unsigned long)z80_terminal_rx_drop_count(),
         (unsigned long)z80_terminal_tx_drop_count(),
         (unsigned long)z80_io_trap_timeout_count(),
         (unsigned long)z80_io_trap_control_error_count(),
         (unsigned long)z80_io_trap_inactive_irq_count(),
         boot_error_name(boot->error),
         boot_failure != NULL ? boot_failure : "none");
  if (boot->error != Z80_BOOT_ERROR_NONE)
    printf("[boot] phase=%u completed=%lu address=%04x "
           "expected=%02x actual=%02x\n",
           (unsigned int)boot->phase, (unsigned long)boot->completed,
           boot->address, boot->expected, boot->actual);
}

static void print_help(void) {
#ifdef Z80_STAGE10_NETWORK
  printf("\nStage 10: Phase 9 automatic CP/M loader plus Wi-Fi terminal.\n"
         "Ctrl-] n=network setup; browser owns CP/M while connected, "
         "USB resumes on disconnect.\n");
#else
  printf("\nStage 9: USB CP/M terminal; nominal 20 MHz after slow loading.\n");
#endif
  printf("Ctrl-] then: l=load/verify only, g=run verified CP/M, s=status,\n"
         "t=read-only all-drive disk test after load (reboot afterward),\n"
         "x=flush then hold RESET, r=reboot when stopped, h=help,\n"
         "0=disarm, 1-8=arm one-shot flash fault (DISPOSABLE DISKS ONLY).\n"
         "Ctrl-] Ctrl-] sends literal Ctrl-]. Ordinary bytes go to CP/M.\n");
  if (!running && boot_attempted && !image_verified)
    printf("Stopped: Ctrl-] r=reboot, s=status, h=help.\n"
           "Loading/running/testing (l/g/t) requires a Pico reboot first; "
           "fault commands (0-8) require a running CPU.\n");
  else if (Z80_STAGE09_AUTOBOOT)
    printf("CP/M loads and starts automatically on Pico reset. "
           "Ctrl-] x then Ctrl-] r safely reboots into CP/M.\n");
  else
    printf("First: Ctrl-] l, then Ctrl-] g; confirm BIOS banner and A>.\n");
}

static void diagnostic_command(int command) {
  if (command == 'l') {
    load_image();
  } else if (command == 'g') {
    start_cpm();
  } else if (command == 't') {
    start_disk_diagnostic();
  } else if (command == 's') {
    print_status();
  } else if (command == 'h') {
    print_help();
#ifdef Z80_STAGE10_NETWORK
  } else if (command == 'n') {
    if (running && !stop_requested)
      z80_network_setup_request();
    else
      printf("\n[network setup] Rejected: CP/M must be running, not stopping.\n");
#endif
  } else if (command == 'x') {
    if (!running || diagnostic_active) {
      hold_cpu();
      printf("\n[diag] RESET held LOW; reboot required before a new run.\n");
      print_help();
    } else {
      stop_requested = true;
      flush_submitted = false;
      __atomic_store_n(&flash_pause_request, 1, __ATOMIC_RELEASE);
      printf("\n[diag] Stop requested; waiting for disk completion/flush.\n");
    }
  } else if (command == 'r') {
    if (running || stop_requested)
      printf("\n[diag] Reboot rejected: stop and flush with Ctrl-] x first.\n");
    else
      watchdog_reboot(0, 0, 0);
  } else if (command >= '0' && command <= '8') {
    if (!running) {
      printf("\n[diag] Fault command rejected: CPU is not running.\n");
      return;
    }
    z80_flash_fault_point_t point =
        (z80_flash_fault_point_t)(command - '0');
    z80_flash_disk_arm_fault(point);
    printf("\n[diag] Armed: %s; next disk write/flush may reboot Pico.\n",
           fault_name(point));
  } else {
    printf("\n[diag] Unknown command; Ctrl-] h for help.\n");
  }
}

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
  if (command_pending) {
    command_pending = false;
    if (value != USB_COMMAND_ESCAPE) {
      diagnostic_command(value);
      return;
    }
  } else if (value == USB_COMMAND_ESCAPE) {
    ansi_input_reset();
    command_pending = true;
    return;
  }
#ifdef Z80_STAGE10_NETWORK
  if (running && !stop_requested && z80_network_setup_input(value))
    return;
  if (z80_terminal_browser_connected())
    return;
#endif
  if (!running) {
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
#ifdef Z80_STAGE10_NETWORK
  z80_terminal_usb_connected(connected);
  bool browser_connected = z80_terminal_browser_connected();
  if (browser_connected != browser_was_connected) {
    ansi_input_reset();
    previous_cr = false;
    browser_was_connected = browser_connected;
  }
  z80_network_setup_usb_service(connected, running && !stop_requested);
#else
  z80_terminal_set_connected(connected);
#endif
  if (connected != usb_was_connected)
    ansi_input_reset();
  if (connected && !usb_was_connected) {
    command_pending = false;
    previous_cr = false;
    if (reset_report_pending) {
      print_reset_status();
      reset_report_pending = false;
    }
    if (!Z80_STAGE09_AUTOBOOT) {
      print_help();
      print_status();
    } else if (!running) {
      printf("\n[boot] %s; RESET held LOW. Ctrl-] s for status, "
             "Ctrl-] r to reboot.\n",
             boot_failure != NULL ? "FAIL: automatic CP/M boot stopped"
                                  : "CP/M stopped");
      print_status();
    }
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
  if (running && !command_pending
#ifdef Z80_STAGE10_NETWORK
      && !z80_network_setup_active() && !z80_terminal_browser_connected()
#endif
  )
    forward_terminal_byte(
        ansi_input_process(0, to_ms_since_boot(get_absolute_time())));
  for (unsigned int count = 0;
       count < 32 && stdio_usb_connected() &&
       tud_cdc_write_available() != 0; ++count) {
    uint8_t value;
#ifdef Z80_STAGE10_NETWORK
    if (z80_network_setup_active() ||
        z80_terminal_usb_supply(&value, 1) == 0)
#else
    if (z80_terminal_supply(&value, 1) == 0)
#endif
      break;
    putchar_raw(value);
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

static void service_run(void) {
  if (!running)
    return;
  if (diagnostic_active) {
    uint32_t interrupts = save_and_disable_interrupts();
    uint32_t count = diagnostic_count;
    uint32_t result = diagnostic_result;
    restore_interrupts(interrupts);
    if (result != 0 || time_reached(boot_deadline)) {
      hold_cpu();
      printf("\n[diag] %s: disk bytes checked=%lu/%u "
             "fault_offset=%08lx expected=%02x actual=%02x; "
             "RESET held LOW; Ctrl-] r then load CP/M again.\n",
             result == 0x5A ? "PASS" : "FAIL",
             (unsigned long)count, DIAGNOSTIC_BYTES,
             (unsigned long)diagnostic_fault_offset,
             diagnostic_expected, diagnostic_actual);
    } else if (count >= diagnostic_next_progress) {
      printf("[diag] disk bytes checked=%lu/%u\n",
             (unsigned long)count, DIAGNOSTIC_BYTES);
      diagnostic_next_progress += 65536;
    }
    return;
  }
  uint32_t markers = __atomic_load_n(&boot_markers, __ATOMIC_ACQUIRE);
  if ((markers & 1u) && !bios_announced) {
    bios_announced = true;
    if (!Z80_STAGE09_AUTOBOOT)
      printf("\n[boot] Observed BIOS cold-entry report from Z80.\n");
  }
  if ((markers & 2u) && !ccp_announced) {
    ccp_announced = true;
    if (!Z80_STAGE09_AUTOBOOT)
      printf("\n[boot] Observed BIOS handoff to CCP; "
             "confirm A>, DIR and transient/warm-boot tests manually.\n");
  }
  if (!boot_timed_out && (!bios_announced || !ccp_announced) &&
      time_reached(boot_deadline)) {
    boot_failure = "BIOS/CCP evidence timeout";
    printf("\n[boot] FAIL: BIOS/CCP evidence timeout; stopping safely. "
           "Rebuild/provision the matching boot package if markers are absent.\n");
    stop_requested = true;
    boot_timed_out = true;
    __atomic_store_n(&flash_pause_request, 1, __ATOMIC_RELEASE);
  }
  if (z80_flash_disk_has_fatal_error()) {
    boot_failure = "fatal storage error";
    hold_cpu();
    printf("\n[diag] FAIL: fatal storage error; RESET held LOW. "
           "Reboot to recover; preserve USB log.\n");
    return;
  }
  if (!stop_requested)
    return;
  if (flash_resuming) {
    if (!__atomic_load_n(&flash_paused, __ATOMIC_ACQUIRE)) {
      flash_resuming = false;
      flush_submitted = false;
      __atomic_store_n(&flash_pause_request, 1, __ATOMIC_RELEASE);
    }
    return;
  }
  if (__atomic_load_n(&flash_paused, __ATOMIC_ACQUIRE)) {
    // Exclude a last I/O IRQ between checking the queue and asserting reset.
    uint32_t interrupts = save_and_disable_interrupts();
    bool quiescent = z80_flash_disk_quiescent();
    if (quiescent)
      hold_cpu();
    restore_interrupts(interrupts);
    if (quiescent) {
      printf("\n[diag] Disk quiescent; RESET held LOW, PWM stopped. "
             "Ctrl-] r reboots/reloads the supervisor.\n");
      print_help();
    } else {
      flash_resuming = true;
      __atomic_store_n(&flash_pause_request, 0, __ATOMIC_RELEASE);
    }
  } else if (!flush_submitted &&
             z80_flash_disk_status() == DISK_READY) {
    z80_flash_disk_io_write(0x10, 5);
    flush_submitted = true;
  }
}

int main(void) {
  z80_safe_startup();
  reset_was_watchdog = watchdog_caused_reboot();
  reset_fault = z80_io_trap_take_reset_fault();
  reset_report_pending = reset_was_watchdog;
  set_sys_clock_pll(1200000000, 5, 2);
  stdio_init_all();
  z80_terminal_queue_init();
#ifdef Z80_STAGE10_NETWORK
  z80_network_setup_init();
#endif
  if (clock_get_hz(clk_sys) != SYSTEM_CLOCK_HZ) {
    z80_cpu_fail_closed();
    while (true) {
      printf("FAIL: expected 120 MHz supervisor clock; CPU held reset\n");
      sleep_ms(1000);
    }
  }
  if (Z80_STAGE09_AUTOBOOT) {
    load_image();
    if (image_verified)
      start_cpm();
  }
  while (true) {
    if (core1_started)
      z80_flash_core0_service();
    if (core1_started && z80_io_ports_reboot_requested()) {
      hold_cpu();
      watchdog_reboot(0, 0, 0);
      while (true)
        tight_loop_contents();
    }
    service_usb();
    service_run();
    tight_loop_contents();
  }
}
