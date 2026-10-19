#include <inttypes.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/sram.h"
#include "z80sbc/supervisor.h"
#include "scope_test_program.h"

enum { SCOPE_HZ = Z80_CLOCK_MAX_HZ, SCOPE_SYSTEM_HZ = 120000000 };
_Static_assert(SCOPE_HZ == 20000000, "Use the isolated 20 MHz chain");
typedef enum {
  SCOPE_IDLE, SCOPE_LOAD, SCOPE_VERIFY, SCOPE_RUN, SCOPE_READ
} scope_state_t;
static scope_state_t scope_state;
static uint8_t scope_image[SOAK_IMAGE_SIZE], snapshot[SCOPE_MAILBOX_SIZE];
static unsigned scope_index, command_length;
static bool was_connected, discard_line, has_run;
static char command;
static absolute_time_t run_started;
static uint64_t elapsed_ms;

static void configure_system_clock(void) {
  set_sys_clock_pll(1200000000, 5, 2);
}

static void stop_test(const char *reason) {
  z80_cpu_fail_closed();
  if (scope_state == SCOPE_RUN)
    elapsed_ms = (uint64_t)absolute_time_diff_us(
        run_started, get_absolute_time()) / 1000;
  scope_state = SCOPE_IDLE;
  printf("STOP: %s; elapsed_ms=%" PRIu64 "; RESET# LOW; no timed PASS\n",
         reason, elapsed_ms);
}

static void fail_test(const char *reason) {
  stop_test(reason);
  printf("FAIL: %s; no valid scope-test result\n", reason);
}

static void print_snapshot(void) {
  if (!scope_snapshot_valid(snapshot)) {
    printf("INCONCLUSIVE: invalid, unstarted or interrupted mailbox; "
           "state=%02x busy=%u; no PASS\n",
           snapshot[SCOPE_STATE], snapshot[SCOPE_BUSY]);
    return;
  }
  uint64_t phases = scope_read_phases(snapshot);
  printf("SNAPSHOT: phases=%" PRIu64 " completed_cycles=%" PRIu64
         " sram_byte_checks=%" PRIu64 " math_vectors=%" PRIu64
         " completed_phase_checks=%" PRIu64 "\n",
         phases, phases / 4, phases * SOAK_WORK_BYTES,
         phases * SOAK_VECTOR_COUNT * SOAK_REPEATS,
         phases * (SOAK_WORK_BYTES + SOAK_VECTOR_COUNT * SOAK_REPEATS *
                   SOAK_ASSERTIONS_PER_VECTOR + SOAK_STACK_CHECKS +
                   SOAK_LOOP_CHECKS));
  if (snapshot[SCOPE_STATE] == SCOPE_FAILED) {
    printf("FAIL: Z80 mismatch stage=%02x vector=%u address=%04x "
           "actual=%04x expected=%04x; clock continued until stopped\n",
           snapshot[SCOPE_STAGE], snapshot[SCOPE_VECTOR],
           scope_read_word(snapshot + SCOPE_ADDRESS),
           scope_read_word(snapshot + SCOPE_ACTUAL),
           scope_read_word(snapshot + SCOPE_EXPECTED));
  } else if (phases == 0) {
    printf("INCONCLUSIVE: no completed phases; no proof of workload progress\n");
  } else {
    printf("RESULT: no mismatch recorded in retrieved mailbox; "
           "not proof of live progress, not a timed PASS or timing qualification\n");
  }
}

static void begin_readback(void) {
  stop_test("stopped for SRAM result retrieval");
  if (!z80_cpu_prepare_loader()) {
    fail_test("mailbox reader initialization");
    return;
  }
  scope_index = 0;
  scope_state = SCOPE_READ;
  printf("READ: PWM ended; slow Z80-assisted mailbox read, no SRAM writes\n");
}

static void start_test(void) {
  stop_test("preparing a fresh scope run");
  has_run = false;
  elapsed_ms = scope_index = 0;
  if (clock_get_hz(clk_sys) != SCOPE_SYSTEM_HZ ||
      !z80_clock_set_hz(SCOPE_HZ) || z80_clock_get_hz() != SCOPE_HZ) {
    fail_test("unexpected system/PWM clock");
    return;
  }
  z80_cpu_fail_closed();
  scope_build_image(scope_image);
  if (!z80_cpu_prepare_loader()) {
    fail_test("loader initialization");
    return;
  }
  scope_state = SCOPE_LOAD;
  printf("LOAD: %u-byte scope image; slow assisted clocks, then verify\n",
         SOAK_IMAGE_SIZE);
}

static void service_test(void) {
  if (scope_state == SCOPE_IDLE || scope_state == SCOPE_RUN)
    return;
  if (scope_state == SCOPE_READ) {
    if (!z80_sram_read_byte((uint16_t)(SCOPE_MAILBOX_BASE + scope_index),
                            snapshot + scope_index)) {
      fail_test("mailbox read");
      return;
    }
    if (++scope_index == SCOPE_MAILBOX_SIZE) {
      stop_test("mailbox read complete");
      print_snapshot();
    }
    return;
  }
  uint8_t expected = scope_image[scope_index];
  if (scope_state == SCOPE_LOAD) {
    if (!z80_sram_write_byte((uint16_t)scope_index, expected)) {
      fail_test("assisted image load");
      return;
    }
  } else {
    uint8_t actual;
    if (!z80_sram_read_byte((uint16_t)scope_index, &actual)) {
      fail_test("assisted image read");
      return;
    }
    if (actual != expected) {
      printf("IMAGE MISMATCH: address=%04x actual=%02x expected=%02x\n",
             scope_index, actual, expected);
      fail_test("assisted image verification");
      return;
    }
  }
  if (++scope_index != SOAK_IMAGE_SIZE)
    return;
  scope_index = 0;
  if (scope_state == SCOPE_LOAD) {
    scope_state = SCOPE_VERIFY;
    return;
  }
  if (!z80_cpu_release_reset_and_run(SCOPE_HZ) ||
      z80_clock_get_hz() != SCOPE_HZ) {
    fail_test("20 MHz execution start");
    return;
  }
  has_run = true;
  run_started = get_absolute_time();
  scope_state = SCOPE_RUN;
  printf("RUN: nominal=20000000Hz, 50%% duty; no Z80 IN/OUT, no I/O trap; "
         "x stops and reads results; live workload status unavailable\n");
}

static void print_help(void) {
  printf("20MHz no-I/O CPU/SRAM scope diagnostic; manual start, no timed PASS.\n");
  printf("Pico clk_sys=%luHz; nominal PWM=20000000Hz, not measured.\n",
         (unsigned long)clock_get_hz(clk_sys));
  printf("DTR required. f + Enter loads/starts; s + Enter state only; "
         "h + Enter help; x immediately stops and reads SRAM results.\n");
  printf("r + Enter reads stopped results after reconnect; never reads live SRAM.\n");
  printf("Failure stores SRAM details and HALTs; clock continues. "
         "Disconnect stops safely, never restarts automatically.\n");
}

static void poll_console(void) {
  if (!stdio_usb_connected()) {
    if (scope_state != SCOPE_IDLE)
      stop_test("console disconnected; reconnect and use r to retrieve if started");
    was_connected = false;
    command_length = 0;
    discard_line = false;
    return;
  }
  if (!was_connected) {
    was_connected = true;
    print_help();
  }
  int value = getchar_timeout_us(0);
  if (value < 0)
    return;
  if (value == 'x') {
    if (scope_state == SCOPE_RUN)
      begin_readback();
    else
      stop_test("operator abort; no result retrieval");
    command_length = 0;
    discard_line = true;
  } else if (value == '\r' || value == '\n') {
    if (!discard_line && command_length != 0) {
      if (command_length != 1)
        printf("ERROR: use one command then Enter.\n");
      else if (command == 'h')
        print_help();
      else if (command == 's') {
        printf("STATE: %s; workload result unavailable until stopped/read\n",
               scope_state == SCOPE_RUN ? "CLOCK RUNNING (nominal 20MHz)" :
               scope_state == SCOPE_IDLE ? "STOPPED" : "ASSISTED LOAD/VERIFY/READ");
      } else if (command == 'f' && scope_state == SCOPE_IDLE)
        start_test();
      else if (command == 'r' && scope_state == SCOPE_IDLE && has_run)
        begin_readback();
      else
        printf("ERROR: unknown command or invalid state; x stops; "
               "r requires a previously started, stopped run.\n");
    }
    command_length = 0;
    discard_line = false;
  } else if (!discard_line) {
    if (command_length == 0)
      command = (char)value;
    if (command_length < 2)
      ++command_length;
  }
}

int main(void) {
  z80_safe_startup();
  configure_system_clock();
  stdio_init_all();
  while (true) {
    poll_console();
    service_test();
    tight_loop_contents();
  }
}
