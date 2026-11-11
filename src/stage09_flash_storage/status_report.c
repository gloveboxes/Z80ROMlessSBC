#include <stdio.h>

#include "hardware/clocks.h"
#include "z80sbc/banked_pio.h"
#include "z80sbc/clock.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/io_ports.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"
#include "z80sbc/terminal.h"
#include "usb_console.h"
#include "../common/io_service.h"
#include "core_coordination.h"
#include "status_report.h"

#ifdef Z80_STAGE10_NETWORK
#include "web_terminal.h"
#include "oled_display.h"
#include "speaker_io.h"
#endif

const char *status_boot_error_name(z80_boot_error_t error) {
  static const char *const names[] = {
    "none", "journal recovery",
    "manifest (requires 65536 bytes and JP FC00h)",
    "payload CRC", "loader controls", "SRAM write", "SRAM read",
    "SRAM mismatch", "flash-safe core initialization", "cancelled",
  };
  return (unsigned int)error < sizeof(names) / sizeof(names[0])
             ? names[(unsigned int)error] : "invalid";
}

static void print_reset_status(const core0_status_t *state) {
  const char *reason = "none";
  if (state->reset_fault.reason == Z80_IO_TRAP_RESET_RELEASE_TIMEOUT)
    reason = "release-timeout";
  else if (state->reset_fault.reason == Z80_IO_TRAP_RESET_INVALID_CONTROLS)
    reason = "invalid-controls";
  printf("[reset] watchdog=%u io_fault=%s gpio=%08lx\n",
         state->reset_was_watchdog, reason,
         (unsigned long)state->reset_fault.gpio);
}

void status_print_pio(void) {
  z80_banked_pio_status_t p = z80_banked_pio_status();
  printf("[pio] active=%u running=%u reads=%lu writes=%lu "
         "last=%s:%02x:%02x pc=%u/%u irq=%02x gpio=%08lx\n",
         p.active, p.running,
         (unsigned long)p.reads, (unsigned long)p.writes,
         p.read ? "IN" : "OUT", p.port, p.value, p.clock_pc, p.io_pc,
         p.irqs, (unsigned long)p.pins);
}

static void print_io_latency(void) {
  z80_banked_pio_latency_t l = z80_banked_pio_take_latency();
  if (!l.available) {
    printf("[iolat] unavailable (no DWT cycle counter)\n");
    return;
  }
  // Core-0 cost since the previous Ctrl-] s; clk_sys cycles to ns.
  const uint64_t mhz = clock_get_hz(clk_sys) / 1000000u;
  unsigned long long in_avg =
      l.in_count ? l.in_cycles * 1000u / mhz / l.in_count : 0;
  unsigned long long out_avg =
      l.out_count ? l.out_cycles * 1000u / mhz / l.out_count : 0;
  printf("[iolat] since last status: IN count=%lu avg=%lluns max=%lluns; "
         "OUT apply count=%lu avg=%lluns max=%lluns\n",
         (unsigned long)l.in_count, in_avg,
         (unsigned long long)l.in_max_cycles * 1000u / mhz,
         (unsigned long)l.out_count, out_avg,
         (unsigned long long)l.out_max_cycles * 1000u / mhz);
}

#ifdef Z80_STAGE10_NETWORK
static void print_network_status(bool requested) {
  if (requested) {
    char address[16];
    bool has_address = z80_io_ports_get_network_address(address, sizeof(address)) != 0;
    printf("[network] browser=%u setup=%u ip=%s\n",
           !!(__atomic_load_n(&usb_state, __ATOMIC_ACQUIRE) & USB_BROWSER),
           !!(__atomic_load_n(&usb_state, __ATOMIC_ACQUIRE) & USB_SETUP),
           has_address ? address : "unavailable");
    if (has_address)
      printf("[network] Terminal: http://%s/\n", address);
    z80_oled_print_status();
    z80_speaker_print_status();
  }
  z80_web_terminal_stats_t web = z80_web_terminal_get_stats();
  printf("[web] flush_failures=%lu ack_timeouts=%lu pong_timeouts=%lu "
         "last_disconnect=%s\n",
         (unsigned long)web.flush_failures, (unsigned long)web.ack_timeouts,
         (unsigned long)web.pong_timeouts,
         z80_web_terminal_disconnect_name(web.last_disconnect_reason));
  if (web.last_ws_close_reason != 0)
    printf("[webclose] reason=%s lwip_error=%ld\n",
           z80_web_terminal_ws_close_name(web.last_ws_close_reason),
           (long)web.last_ws_transport_error);
  printf("[webbuf] out_frames=%lu/%u peak=%lu staged=%lu/%u\n",
         (unsigned long)web.window_frames, Z80_WEB_TERMINAL_OUTPUT_WINDOW,
         (unsigned long)web.window_frames_peak, (unsigned long)web.staged_bytes,
         Z80_WEB_TERMINAL_FRAME_BYTES);
  z80_terminal_buffer_stats_t buffers = z80_terminal_get_buffer_stats();
  printf("[consolebuf] rx=%lu/%lu peak=%lu tx=%lu/%lu peak=%lu\n",
         (unsigned long)buffers.rx_used, (unsigned long)buffers.rx_capacity,
         (unsigned long)buffers.rx_peak, (unsigned long)buffers.tx_used,
         (unsigned long)buffers.tx_capacity, (unsigned long)buffers.tx_peak);
}
#endif

void status_print(const core0_status_t *state, bool requested) {
  if (requested)
    print_reset_status(state);
  uint64_t uptime_seconds = z80_service_millis() / 1000;
  printf("[system] uptime=%02llu:%02llu:%02llu\n",
         (unsigned long long)(uptime_seconds / 3600),
         (unsigned long long)((uptime_seconds / 60) % 60),
         (unsigned long long)(uptime_seconds % 60));
#ifdef Z80_STAGE10_NETWORK
  print_network_status(requested);
#endif
  status_print_pio();
  if (requested)
    print_io_latency();
  uint32_t readahead_hits, readahead_misses;
  z80_flash_disk_prefetch_counts(&readahead_hits, &readahead_misses);
  printf("[disk] readahead hits=%lu misses=%lu\n",
         (unsigned long)readahead_hits, (unsigned long)readahead_misses);
  printf("[diag] banked_sram=%02x (00=unavailable, B4/B5=ready bank 0/1)\n",
         (unsigned int)z80_memory_bank_status());
  const z80_boot_status_t *boot = z80_flash_boot_status();
  printf("\n[diag] running=%u verified=%u clock=%lu BIOS=%u CCP_handoff=%u "
         "disk=%02lx fatal=%u rx_drops=%lu tx_drops=%lu "
         "trap_timeouts=%lu control_errors=%lu boot_error=%s "
         "failure=%s log_drops=%lu\n",
         state->running, state->image_verified,
         (unsigned long)z80_clock_get_hz(),
         state->bios_announced, state->ccp_announced,
         (unsigned long)z80_flash_disk_status(),
         z80_flash_disk_has_fatal_error(),
         (unsigned long)z80_terminal_rx_drop_count(),
         (unsigned long)z80_terminal_tx_drop_count(),
         (unsigned long)z80_io_trap_timeout_count(),
         (unsigned long)z80_io_trap_control_error_count(),
         status_boot_error_name(boot->error),
         state->boot_failure != NULL ? state->boot_failure : "none",
         (unsigned long)z80_usb_console_log_drops());
  if (boot->error != Z80_BOOT_ERROR_NONE)
    printf("[boot] phase=%u completed=%lu address=%04x "
           "expected=%02x actual=%02x\n",
           (unsigned int)boot->phase, (unsigned long)boot->completed,
           boot->address, boot->expected, boot->actual);
}

void status_print_help(const core0_status_t *state) {
#ifdef Z80_STAGE10_NETWORK
  printf("\nStage 10: Phase 9 automatic CP/M loader plus Wi-Fi terminal.\n"
         "Ctrl-] c=system configuration; browser owns CP/M while connected, "
         "USB resumes on disconnect.\n");
#else
  printf("\nStage 9: USB CP/M terminal; nominal 20 MHz after slow loading.\n");
#endif
  printf("Ctrl-] then: l=load/verify only, g=run verified CP/M, s=status,\n"
         "t=read-only all-drive disk test after load (reboot afterward),\n"
         "x=flush then hold RESET, r=reboot when stopped, h=help,\n"
         "0=disarm, 1-8=arm one-shot flash fault (DISPOSABLE DISKS ONLY).\n"
         "Ctrl-] Ctrl-] sends literal Ctrl-]. Ordinary bytes go to CP/M.\n");
  if (!state->running && state->boot_attempted && !state->image_verified)
    printf("Stopped: Ctrl-] r=reboot, s=status, h=help.\n"
           "Loading/running/testing (l/g/t) requires a Pico reboot first; "
           "fault commands (0-8) require a running CPU.\n");
  else if (Z80_STAGE09_AUTOBOOT)
    printf("CP/M loads and starts automatically on Pico reset. "
           "Ctrl-] x then Ctrl-] r safely reboots into CP/M.\n");
  else
    printf("First: Ctrl-] l, then Ctrl-] g; confirm BIOS banner and A>.\n");
}
