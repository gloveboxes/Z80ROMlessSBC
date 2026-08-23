#include <stdio.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "z80sbc/cpu.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/supervisor.h"
#include "z80sbc/terminal.h"

static uint8_t virtual_read(uint8_t port, void *context) {
  (void)context;
  if (port >= 0x10 && port <= 0x14)
    return z80_flash_disk_io_read(port);
  return z80_terminal_io_read(port);
}

static void virtual_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  if (port >= 0x10 && port <= 0x14)
    z80_flash_disk_io_write(port, value);
  else
    z80_terminal_io_write(port, value);
}

static void core1_main(void) {
  while (true) {
    z80_flash_core1_service();
    z80_terminal_core1_service();
    tight_loop_contents();
  }
}

static _Noreturn void fail_closed(const char *reason) {
  z80_io_trap_disable();
  z80_cpu_fail_closed();
  printf("FAIL: %s\n", reason);
  while (true)
    tight_loop_contents();
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 10: CP/M flash disks and WebSocket terminal\n");

  if (!z80_flash_storage_init())
    fail_closed("boot package or journal recovery");
  if (!z80_terminal_init())
    fail_closed("terminal timers");
  multicore_launch_core1(core1_main);
  if (!z80_io_trap_enable(virtual_read, virtual_write, NULL))
    fail_closed("I/O trap initialization");
  if (!z80_cpu_release_reset_and_run(1000000))
    fail_closed("CPU start");
  printf("PASS: CP/M started; WebSocket port 8088\n");

  while (true) {
    z80_flash_core0_service();
    int command = getchar_timeout_us(0);
    if (command == 's')
      printf("client=%u rx_drop=%lu tx_drop=%lu disk=%02lx fatal=%u\n",
             z80_terminal_client_connected(),
             (unsigned long)z80_terminal_rx_drop_count(),
             (unsigned long)z80_terminal_tx_drop_count(),
             (unsigned long)z80_flash_disk_status(),
             z80_flash_disk_has_fatal_error());
    tight_loop_contents();
  }
}