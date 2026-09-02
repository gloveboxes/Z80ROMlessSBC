#include <stdio.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "z80sbc/cpu.h"
#include "z80sbc/flash_disk.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/supervisor.h"

static uint8_t virtual_read(uint8_t port, void *context) {
  (void)context;
  return port >= 0x10 && port <= 0x14
             ? z80_flash_disk_io_read(port)
             : 0xFF;
}

static void virtual_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  if (port >= 0x10 && port <= 0x14)
    z80_flash_disk_io_write(port, value);
}

static void core1_main(void) {
  while (true) {
    z80_flash_core1_service();
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

static const char *fault_name(z80_flash_fault_point_t point) {
  static const char *const names[] = {
    "none",
    "after journal data",
    "after journal header",
    "after target erase",
    "after partial target program",
    "after target verification",
    "after journal header clear",
    "safe-execute entry failure",
    "safe-execute exit failure",
  };
  return point <= Z80_FLASH_FAULT_SAFE_EXECUTE_EXIT
             ? names[(unsigned int)point]
             : "invalid";
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  printf("\nStage 9: manifest boot and journaled flash disks\n");
  printf("1-8=arm one-shot flash fault, s=status\n");

  if (!z80_flash_storage_init())
    fail_closed("boot package or journal recovery");
  multicore_launch_core1(core1_main);
  if (!z80_io_trap_enable(virtual_read, virtual_write, NULL))
    fail_closed("I/O trap initialization");
  if (!z80_cpu_release_reset_and_run(1000000))
    fail_closed("CPU start");
  printf("PASS: CP/M image started\n");

  while (true) {
    z80_flash_core0_service();
    int command = getchar_timeout_us(0);
    if (command >= '1' && command <= '8') {
      z80_flash_fault_point_t point =
          (z80_flash_fault_point_t)(command - '0');
      z80_flash_disk_arm_fault(point);
      printf("armed: %s; trigger the next CP/M write/flush\n",
             fault_name(point));
    } else if (command == 's')
      printf("disk_status=%02lx fatal=%u\n",
             (unsigned long)z80_flash_disk_status(),
             z80_flash_disk_has_fatal_error());
    tight_loop_contents();
  }
}