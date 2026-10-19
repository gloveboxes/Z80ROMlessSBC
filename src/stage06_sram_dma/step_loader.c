#include <stdio.h>

#include "full_memory_test.h"
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/clock.h"
#include "z80sbc/cpu.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

typedef struct {
  uint16_t address;
  uint8_t value;
} test_byte_t;

static const test_byte_t TEST_BYTES[] = {
  {0x0000, 0x00},
  {0x0001, 0x01},
  {0x0002, 0x02},
};

typedef enum {
  OPCODE,
  OPERAND,
  CPU_WRITE,
  SRAM_READ,
} transfer_kind_t;

typedef struct {
  transfer_kind_t kind;
  uint8_t byte;
  const char *description;
  const char *expected;
} transfer_t;

/* The final retention pass skips transfers 3-5; it must not rewrite SRAM. */
static const transfer_t TRANSFERS[] = {
  {OPCODE, 0x21, "LD HL,nn: opcode",
   "Z80 fetches injected 21; SRAM output inhibited"},
  {OPERAND, 0, "LD HL,nn: address LOW",
   "Z80 fetches target address low byte"},
  {OPERAND, 0, "LD HL,nn: address HIGH",
   "Z80 fetches target address high byte; HL becomes target"},
  {OPCODE, 0x36, "LD (HL),n: opcode",
   "Z80 fetches injected 36; no SRAM write yet"},
  {OPERAND, 0, "LD (HL),n: test byte",
   "Z80 fetches test byte; no SRAM write yet"},
  {CPU_WRITE, 0, "LD (HL),n: Z80 writes SRAM",
   "Z80 alone drives target address and data; SRAM OE# HIGH"},
  {OPCODE, 0x3E, "LD A,00: opcode",
   "Z80 fetches injected 3E; does not write SRAM"},
  {OPERAND, 0x00, "LD A,00: zero operand",
   "Z80 accumulator becomes 00; no separate physical data-bus clear"},
  {OPCODE, 0x7E, "LD A,(HL): opcode",
   "Z80 fetches injected 7E; HL still holds original target"},
  {SRAM_READ, 0, "LD A,(HL): Z80 reads SRAM",
   "SRAM alone drives test byte; Z80 reads it; Pico observes via U10"},
};

typedef enum {
  STOPPED,
  RESET_CLOCK,
  RELEASE_RESET,
  RESET_EXIT,
  SETUP,
  T1,
  DRIVE,
  T2,
  T3,
  T4,
  COMPLETE,
} phase_t;

enum {
  TEST_COUNT = sizeof(TEST_BYTES) / sizeof(TEST_BYTES[0]),
  TRANSFER_COUNT = sizeof(TRANSFERS) / sizeof(TRANSFERS[0]),
  STEP_HALF_PERIOD_US = 1000,
};

static phase_t phase = STOPPED;
static unsigned reset_clocks;
static unsigned exit_clocks;
static unsigned total_clocks;
static unsigned test_index;
static unsigned transfer_index;
static unsigned verified_reads;
static uint16_t injected_pc;
static bool retention;
static int pending_command;
static bool invalid_line;

static bool injected(void) {
  return TRANSFERS[transfer_index].kind == OPCODE ||
         TRANSFERS[transfer_index].kind == OPERAND;
}

static uint8_t transfer_byte(void) {
  if (transfer_index == 1)
    return (uint8_t)TEST_BYTES[test_index].address;
  if (transfer_index == 2)
    return (uint8_t)(TEST_BYTES[test_index].address >> 8);
  if (transfer_index == 4 || transfer_index == 5 || transfer_index == 9)
    return TEST_BYTES[test_index].value;
  return TRANSFERS[transfer_index].byte;
}

static void safe_stop(void) {
  z80_cpu_fail_closed();
  phase = STOPPED;
}

static void print_observed(void) {
  printf("OBSERVED via U7: BUSACK#=%u IORQ#=%u RD#=%u WR#=%u "
         "A[4,2,1,0]=%02x (mask 17 only)\n",
         (unsigned)gpio_get(PIN_BUSACK_N), (unsigned)gpio_get(PIN_IORQ_N),
         (unsigned)gpio_get(PIN_RD_N), (unsigned)gpio_get(PIN_WR_N),
         z80_port_bus_sample());
  printf("PICO commands: CLK=%u RESET#=%u inhibit=%u UP_OE#=%u DOWN_OE#=%u\n",
         (unsigned)gpio_get(PIN_CLK), (unsigned)gpio_get(PIN_RESET_N),
         (unsigned)gpio_get(PIN_BOOT_READ_DISABLE),
         (unsigned)gpio_get(PIN_DATA_UP_OE_N),
         (unsigned)gpio_get(PIN_DATA_DOWN_OE_N));
  printf("Full A0-A15, MREQ#, actual SRAM CE#/OE#/WE# and actual U9 OE#\n"
         "are NOT monitored by Pico; verify at physical pins.\n");
}

static void fail(const char *reason) {
  printf("\nFAIL: %s; clock=%u target=%04x expected=%02x\n",
         reason, total_clocks, TEST_BYTES[test_index].address,
         TEST_BYTES[test_index].value);
  print_observed();
  safe_stop();
  printf("STOPPED: both data paths off, RESET# LOW, CLK LOW, inhibit HIGH.\n"
         "Record the failure. Do not continue; fix wiring/timing then b.\n");
}

static bool check_controls(bool rd_high, bool wr_high, bool check_address) {
  bool controls_ok = gpio_get(PIN_BUSACK_N) && gpio_get(PIN_IORQ_N) &&
                     gpio_get(PIN_RD_N) == rd_high &&
                     gpio_get(PIN_WR_N) == wr_high;
  uint16_t expected_address = injected() ? injected_pc :
                                         TEST_BYTES[test_index].address;
  uint8_t actual_address = z80_port_bus_sample();
  if (!controls_ok ||
      (check_address &&
       actual_address != (expected_address & Z80_PORT_ADDRESS_MASK))) {
    printf("EXPECTED: BUSACK#=1 IORQ#=1 RD#=%u WR#=%u",
           (unsigned)rd_high, (unsigned)wr_high);
    if (check_address)
      printf(" address=%04x masked=%02x", expected_address,
             expected_address & Z80_PORT_ADDRESS_MASK);
    printf("\n");
    fail("control or monitored-address mismatch");
    return false;
  }
  return true;
}

static void print_help(void) {
  printf("\nSTAGE 6 CONSOLE-STEPPED Z80 SRAM LOADER HARNESS\n"
         "Requires ALL eight active packages fitted, powered and checked.\n"
         "Destructive test: 0000=00, 0001=01, 0002=02; no scratch writes.\n"
         "Mimics CP/M assisted loading; does NOT load or execute CP/M.\n"
         "Per byte: LD HL,nn; LD (HL),n; LD A,00; LD A,(HL).\n"
         "Then final read-only retention pass over all three addresses.\n"
         "LD A,00 clears A, NOT a held physical data-bus value.\n"
         "Its injected 00 operand drives zero only during that fetch.\n"
         "A is not directly observable; reports distinguish expectation\n"
         "from data-bus samples. Three bytes do not qualify all of SRAM.\n"
         "ONE letter then Enter (CR/LF/CRLF); no pasting or held keys:\n"
         "  b = begin/restart, held in reset with NO automatic clocks\n"
         "  f = automatic full 64 KiB test: fill/read low-byte, then high-byte\n"
         "  n = next action, at most ONE full clock cycle, ending LOW\n"
         "  r = repeat status/instructions, NO clocks or ownership changes\n"
         "  h = help; x = immediate abort, no Enter required\n"
         "No-clock actions explicitly handle RESET and data ownership.\n"
         "Each clock uses 1ms half-periods; meter sees held LOW clock,\n"
         "not its pulse amplitude. Read/write T2 stops hold valid data.\n"
         "Power: external 5 V BEFORE USB; disconnect USB BEFORE 5 V.\n"
         "Never use USB alone, bridge pins, or force a CPU output LOW.\n"
         "Use DC volts and common-GND black lead; power off to move clips.\n"
         "PROBE MAP (IC package pin numbers, not breadboard rows):\n"
         "  U1 Z80: CLK=6 RESET#=26; U2 SRAM: CE#=22 OE#=24 WE#=29.\n"
         "  U2 address: A0=12 A1=11 A2=10 A3=9 A4=8 A5=7 A6=6 A7=5\n"
         "              A8=27 A9=26 A10=23 A11=25 A12=4 A13=28 A14=3 A15=31.\n"
         "  U2 data: D0=13 D1=14 D2=15 D3=17 D4=18 D5=19 D6=20 D7=21.\n"
         "  U9 actual OE#=19 (RD# OR GP6); U10 actual OE#=19 (GP7).\n"
         "  At held write T2: CE# LOW, WE# LOW, OE# HIGH; CPU drives data.\n"
         "  At held SRAM read T2: CE# LOW, WE# HIGH, OE# LOW; SRAM drives.\n"
         "  HIGH/LOW are expected logic levels, not measured voltages.\n");
}

static void print_bits(const char *label, uint16_t value, unsigned count) {
  printf("%s", label);
  for (unsigned bit = count; bit > 0; --bit)
    printf("%u", (unsigned)((value >> (bit - 1)) & 1u));
  printf("\n");
}

static void print_status(void) {
  static const char *const PHASE_NAMES[] = {
    "STOPPED", "reset clock", "release RESET", "reset exit",
    "ownership setup", "T1", "inject after T1", "T2", "T3", "T4", "COMPLETE",
  };
  printf("\nSTATE next=%s clocks=%u verified_reads=%u/6\n",
         PHASE_NAMES[phase], total_clocks, verified_reads);
  print_observed();
  if (phase == STOPPED) {
    printf("Held in reset, data paths off. b + Enter arms the sequence.\n");
  } else if (phase == COMPLETE) {
    printf("PASS: three immediate and three final SRAM bus read-backs.\n"
           "RESET# LOW, clock stopped; test addresses retain their bytes.\n"
           "This is not full address-bus, CPU-register or timing proof.\n");
  } else if (phase == RESET_CLOCK) {
    printf("NEXT n: reset clock %u/6; RESET# stays LOW; data paths off.\n",
           reset_clocks + 1);
  } else if (phase == RELEASE_RESET) {
    printf("NEXT n: check idle controls, then release RESET# while CLK LOW;\n"
           "NO clock. Six reset clocks already completed.\n");
  } else if (phase == RESET_EXIT) {
    printf("NEXT n: reset-exit alignment clock %u/2; SRAM inhibited.\n",
           exit_clocks + 1);
  } else {
    const transfer_t *transfer = &TRANSFERS[transfer_index];
    printf("%s pair %u/3: [%04x]=%02x; injected PC=%04x\n"
           "TRANSFER: %s\nEXPECTED: %s\n",
           retention ? "FINAL RETENTION" : "WRITE / IMMEDIATE READ",
           test_index + 1, TEST_BYTES[test_index].address,
           TEST_BYTES[test_index].value, injected_pc,
           transfer->description, transfer->expected);
    if (injected())
      printf("Injected byte=%02x; fetch address=%04x, NOT necessarily HL.\n",
             transfer_byte(), injected_pc);
    switch (phase) {
    case SETUP:
      printf("NEXT n: NO clock; %s.\n",
             injected() ? "inhibit SRAM and isolate both data paths" :
             transfer->kind == CPU_WRITE ?
                 "inhibit SRAM output; configure U10 input to observe Z80 write" :
                 "isolate upward drive; configure U10 input; enable SRAM reads");
      break;
    case T1:
      printf("NEXT n: clock T1; target/fetch address and bus controls assert.\n");
      break;
    case DRIVE:
      printf("NEXT n: NO clock; check fetch controls/address, then inject %02x\n"
             "through U9. SRAM output MUST stay inhibited; U10 off.\n",
             transfer_byte());
      break;
    case T2:
      printf("NEXT n: clock T2; expected RD#=%u WR#=%u, address=%04x.\n",
             (unsigned)(transfer->kind == CPU_WRITE),
             (unsigned)(transfer->kind != CPU_WRITE),
             injected() ? injected_pc : TEST_BYTES[test_index].address);
      if (!injected())
        printf("Sample physical data bus via U10; expected=%02x.\n",
               transfer_byte());
      break;
    case T3:
      printf("HELD after T2: %s; expected bus=%02x.\n"
             "NEXT n: clock T3 to finish this transfer, then isolate data.\n",
             injected() ? "U9 supplies injected byte (command, not read-back)" :
             transfer->kind == CPU_WRITE ? "Z80 drives, SRAM write active" :
                                          "SRAM drives, Z80 read active",
             transfer_byte());
      printf("Probe SRAM A0-A15 for %04x and D0-D7 for %02x now;\n"
             "see h for physical pins. 1=HIGH, 0=LOW (logic expectations).\n",
             injected() ? injected_pc : TEST_BYTES[test_index].address,
             transfer_byte());
      print_bits("EXPECTED A15..A0 = ",
                 injected() ? injected_pc : TEST_BYTES[test_index].address, 16);
      print_bits("EXPECTED D7..D0 = ", transfer_byte(), 8);
      break;
    case T4:
      printf("NEXT n: opcode T4 refresh clock; U9/U10 off, SRAM inhibited.\n"
             "Address pins may show refresh address, NOT the test target.\n");
      break;
    default:
      break;
    }
    printf("RD#/WR# are active LOW. Full write/read address and SRAM pins\n"
           "must be checked externally; U7 sees only A0/A1/A2/A4.\n");
  }
  printf("n + Enter advances; r repeats; x stops immediately.\n");
}

static void begin_test(void) {
  safe_stop();
  reset_clocks = exit_clocks = total_clocks = 0;
  test_index = transfer_index = verified_reads = 0;
  injected_pc = 0;
  retention = false;
  phase = RESET_CLOCK;
  printf("\nARMED: existing SRAM will be overwritten ONLY at 0000/0001/0002.\n");
  print_status();
}

static void clock_once(void) {
  z80_clock_one_cycle(STEP_HALF_PERIOD_US);
  ++total_clocks;
}

static void finish_transfer(void) {
  if (injected())
    ++injected_pc;
  if (transfer_index == 7)
    printf("EXPECTED CPU A=00 after LD A,00; register not directly measured.\n");
  ++transfer_index;
  if (retention && transfer_index == 3)
    transfer_index = 6;
  if (transfer_index == TRANSFER_COUNT) {
    transfer_index = 0;
    ++test_index;
    if (test_index == TEST_COUNT) {
      test_index = 0;
      if (retention) {
        safe_stop();
        phase = COMPLETE;
        return;
      }
      retention = true;
      printf("\nAll writes done. FINAL RETENTION: reread every location;\n"
             "do NOT rewrite memory. This can expose aliases among these bytes.\n");
    }
  }
  phase = SETUP;
}

static void next_action(void) {
  if (phase == STOPPED || phase == COMPLETE) {
    printf("No advance: use b + Enter to begin/restart.\n");
    return;
  }
  switch (phase) {
  case RESET_CLOCK:
    clock_once();
    if (++reset_clocks == 6)
      phase = RELEASE_RESET;
    break;
  case RELEASE_RESET:
    if (!check_controls(true, true, false))
      return;
    gpio_put(PIN_RESET_N, 1);
    busy_wait_us_32(1);
    phase = RESET_EXIT;
    break;
  case RESET_EXIT:
    clock_once();
    if (++exit_clocks == 2)
      phase = SETUP;
    break;
  case SETUP:
    z80_data_bus_isolate();
    gpio_put(PIN_BOOT_READ_DISABLE, 1);
    if (!injected()) {
      z80_data_bus_prepare_input();
      if (TRANSFERS[transfer_index].kind == SRAM_READ)
        gpio_put(PIN_BOOT_READ_DISABLE, 0);
    }
    busy_wait_us_32(1);
    phase = T1;
    break;
  case T1:
    clock_once();
    phase = injected() ? DRIVE : T2;
    break;
  case DRIVE:
    if (!check_controls(false, true, true))
      return;
    z80_data_bus_drive(transfer_byte());
    phase = T2;
    break;
  case T2: {
    clock_once();
    bool writing = TRANSFERS[transfer_index].kind == CPU_WRITE;
    if (!check_controls(writing, !writing, true))
      return;
    if (!injected()) {
      uint8_t actual = z80_data_bus_sample();
      printf("%s BUS SAMPLE: address=%04x expected=%02x actual=%02x\n",
             writing ? "Z80 WRITE" : "SRAM READ",
             TEST_BYTES[test_index].address, transfer_byte(), actual);
      if (actual != transfer_byte()) {
        fail(writing ? "Z80 write data mismatch" : "SRAM read-back mismatch");
        return;
      }
      printf("MATCH: %s; %s\n",
             writing ? "CPU-driven write data" : "SRAM-driven read data",
             writing ? "write not complete until T3" :
                       "CPU read not complete until T3");
    }
    phase = T3;
    break;
  }
  case T3:
    clock_once();
    if (!check_controls(true, true, false))
      return;
    z80_data_bus_isolate();
    gpio_put(PIN_BOOT_READ_DISABLE, 1);
    if (TRANSFERS[transfer_index].kind == SRAM_READ)
      ++verified_reads;
    if (TRANSFERS[transfer_index].kind == OPCODE)
      phase = T4;
    else
      finish_transfer();
    break;
  case T4:
    clock_once();
    if (!check_controls(true, true, false))
      return;
    finish_transfer();
    break;
  default:
    fail("invalid step state");
    return;
  }
  print_status();
}

static void run_command(int command) {
  if (z80_full_memory_test_active()) {
    printf("BUSY: automatic full-memory test; x aborts between bytes.\n");
    return;
  }
  if (command == 'f') {
    safe_stop();
    total_clocks = verified_reads = 0;
    z80_full_memory_test_start();
  } else if (command == 'b')
    begin_test();
  else if (command == 'n')
    next_action();
  else if (command == 'r')
    print_status();
  else if (command == 'h') {
    print_help();
    print_status();
  } else
    printf("ERROR: unknown command; outputs unchanged. Use h + Enter.\n");
}

static void handle_character(int character) {
  if (character == 'x') {
    safe_stop();
    pending_command = 0;
    invalid_line = true;
    if (z80_full_memory_test_active())
      z80_full_memory_test_abort("operator");
    else
      printf("\nABORTED: RESET# LOW, CLK LOW, inhibit HIGH, data paths off.\n");
  } else if (character == '\r' || character == '\n') {
    if (invalid_line)
      printf("Line discarded. Use one command letter then Enter.\n");
    else if (pending_command)
      run_command(pending_command);
    pending_command = 0;
    invalid_line = false;
  } else if (pending_command || invalid_line) {
    invalid_line = true;
  } else {
    pending_command = character;
  }
}

int main(void) {
  z80_safe_startup();
  safe_stop();
  stdio_init_all();
  while (true) {
    while (!stdio_usb_connected())
      tight_loop_contents();
    pending_command = 0;
    invalid_line = false;
    printf("\nZ80 SRAM diagnostic: RESET# LOW; all ICs required.\n"
           "Console connected. Waiting for a command; no test has started.\n"
           "f + Enter = automatic full 64 KiB low/high-byte test; x = abort\n"
           "b + Enter = three-byte single-step test; h + Enter = full help\n"
           "Destructive test. External 5 V before USB; no USB-only power.\n");
    while (stdio_usb_connected()) {
      int character = getchar_timeout_us(0);
      if (character >= 0)
        handle_character(character);
      if (z80_full_memory_test_active()) {
        if (!stdio_usb_connected())
          break;
        z80_full_memory_test_tick();
      }
      tight_loop_contents();
    }
    safe_stop();
    z80_full_memory_test_abort("console disconnected");
  }
}
