#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "z80sbc/cpu.h"
#include "z80sbc/io_trap.h"
#include "z80sbc/mcp23s17.h"
#include "z80sbc/supervisor.h"

typedef struct {
  uint8_t port;
  uint8_t value;
} output_event_t;

static queue_t output_events;
static uint32_t dropped_events;

static const uint8_t IO_TEST_PROGRAM[] = {
  0x16, 0x41,       /* LD D,'A' */
  0x7A,             /* LD A,D */
  0xD3, 0x00,       /* OUT (00h),A */
  0x01, 0xFF, 0xFF, /* LD BC,FFFFh */
  0x0B,             /* delay: DEC BC */
  0x78,             /* LD A,B */
  0xB1,             /* OR C */
  0x20, 0xFB,       /* JR NZ,delay */
  0x14,             /* INC D */
  0x7A,             /* LD A,D */
  0xFE, 0x5B,       /* CP '[' */
  0x20, 0xEF,       /* JR NZ,output */
  0x18, 0xEB,       /* JR start */
};

static uint8_t virtual_read(uint8_t port, void *context) {
  (void)context;
  if (port == 0x01)
    return 0x02;
  return 0x00;
}

static void virtual_write(uint8_t port, uint8_t value, void *context) {
  (void)context;
  output_event_t event = {port, value};
  if (!queue_try_add(&output_events, &event))
    __atomic_fetch_add(&dropped_events, 1, __ATOMIC_RELAXED);
}

static bool boot_test_program(void) {
  return z80_cpu_load_and_verify(IO_TEST_PROGRAM, sizeof(IO_TEST_PROGRAM)) &&
         z80_io_trap_enable(virtual_read, virtual_write, NULL) &&
         z80_cpu_release_reset_and_run(1000000);
}

int main(void) {
  z80_safe_startup();
  stdio_init_all();
  mcp23s17_init(4000000);
  queue_init(&output_events, sizeof(output_event_t), 32);
  printf("\nStage 8: virtual-ROM boot and synchronous I/O trap\n");
  if (!boot_test_program()) {
    z80_cpu_fail_closed();
    printf("FAIL: boot/trap initialization\n");
  } else {
    printf("PASS: test program running at 1MHz\n");
  }

  while (true) {
    output_event_t event;
    while (queue_try_remove(&output_events, &event))
      printf("OUT %02x = %02x '%c'\n", event.port, event.value,
             event.value >= 32 && event.value <= 126 ? event.value : '.');
    int command = getchar_timeout_us(0);
    if (command == 's') {
      printf("trap_timeouts=%lu control_errors=%lu dropped=%lu\n",
             (unsigned long)z80_io_trap_timeout_count(),
             (unsigned long)z80_io_trap_control_error_count(),
             (unsigned long)__atomic_load_n(&dropped_events,
                                            __ATOMIC_RELAXED));
    } else if (command == 'x') {
      z80_io_trap_disable();
      z80_cpu_fail_closed();
    }
    tight_loop_contents();
  }
}