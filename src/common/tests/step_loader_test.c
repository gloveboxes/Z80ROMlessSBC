#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

void stdio_init_all(void);
bool stdio_usb_connected(void);
int getchar_timeout_us(uint32_t timeout_us);
static int console_printf(const char *format, ...);
#define printf console_printf
#define main step_loader_firmware_main
#include "../../stage06_sram_dma/step_loader.c"
#undef main
#undef printf

typedef enum { FETCH, LOW_BYTE, HIGH_BYTE, IMMEDIATE, WRITE, READ } machine_t;
typedef enum {
  NO_FAULT, READ_FAULT, WRITE_FAULT, ADDRESS_FAULT, RD_FAULT,
  BUSACK_FAULT, IORQ_FAULT, ADDRESS_ALIAS,
} fault_t;

static bool levels[32];
static bool outputs[32];
static bool preloaded[32];
static uint8_t memory[65536];
static machine_t machine;
static fault_t fault;
static unsigned t_state;
static unsigned model_reset_clocks;
static unsigned exit_tail;
static unsigned physical_clocks;
static unsigned writes;
static unsigned reads;
static unsigned clears;
static unsigned gpio_writes;
static uint16_t model_pc;
static uint16_t hl;
static uint16_t physical_address;
static uint8_t instruction;
static uint8_t low_byte;
static uint8_t immediate;
static uint8_t accumulator;
static bool rd_n;
static bool wr_n;
static uint64_t now_us;
static uint64_t isolated_at;
static uint64_t clock_high_at;
static uint64_t clock_low_at;
static char transcript[524288];
static size_t transcript_used;
static jmp_buf usb_disconnected;
static const char *usb_commands;
static bool usb_finished;
static bool automatic_clocks;
static bool idle_connected;
static unsigned idle_budget;

static int console_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  int length = vsnprintf(transcript + transcript_used,
                         sizeof(transcript) - transcript_used, format, args);
  va_end(args);
  assert(length >= 0 && (size_t)length < sizeof(transcript) - transcript_used);
  transcript_used += (size_t)length;
  return length;
}

static uint16_t memory_address(void) {
  return fault == ADDRESS_ALIAS ? hl & (uint16_t)~1u : hl;
}

static uint8_t bus_value(void) {
  if (!wr_n)
    return immediate ^ (fault == WRITE_FAULT ? 0x80 : 0);
  if (!rd_n && !levels[PIN_BOOT_READ_DISABLE])
    return memory[memory_address()] ^ (fault == READ_FAULT ? 0x80 : 0);
  assert(false);
  return 0;
}

static uint8_t injected_value(void) {
  assert(levels[PIN_BOOT_READ_DISABLE]);
  assert(!levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
  uint8_t value = 0;
  for (unsigned bit = 0; bit < 8; ++bit) {
    assert(outputs[PIN_DATA_0 + bit]);
    value |= (uint8_t)(levels[PIN_DATA_0 + bit] << bit);
  }
  return value;
}

/* Decode the injected instruction stream independently of the harness table. */
static void cpu_clock(void) {
  ++physical_clocks;
  if (!levels[PIN_RESET_N]) {
    ++model_reset_clocks;
    return;
  }
  if (exit_tail) {
    --exit_tail;
    return;
  }
  ++t_state;
  if (t_state == 1) {
    physical_address = machine == WRITE || machine == READ ? hl : model_pc;
    rd_n = machine == WRITE;
    wr_n = machine != WRITE;
  }
  if (t_state == 3) {
    if (machine == WRITE) {
      assert(levels[PIN_DATA_UP_OE_N] && !levels[PIN_DATA_DOWN_OE_N]);
      assert(levels[PIN_BOOT_READ_DISABLE]);
      assert(writes < 3);
      assert(hl == TEST_BYTES[writes].address);
      assert(immediate == TEST_BYTES[writes].value);
      memory[memory_address()] = immediate;
      ++writes;
    } else if (machine == READ) {
      assert(levels[PIN_DATA_UP_OE_N] && !levels[PIN_DATA_DOWN_OE_N]);
      assert(!levels[PIN_BOOT_READ_DISABLE]);
      assert(accumulator == 0 && clears == reads + 1);
      assert(hl == TEST_BYTES[reads % 3].address);
      accumulator = bus_value();
      ++reads;
    } else {
      uint8_t value = injected_value();
      ++model_pc;
      if (machine == FETCH)
        instruction = value;
      else if (machine == LOW_BYTE)
        low_byte = value;
      else if (machine == HIGH_BYTE)
        hl = (uint16_t)(low_byte | ((uint16_t)value << 8));
      else if (instruction == 0x36)
        immediate = value;
      else {
        assert(instruction == 0x3E && value == 0);
        accumulator = value;
        ++clears;
      }
    }
    rd_n = wr_n = true;
    if (machine != FETCH) {
      if (machine == LOW_BYTE)
        machine = HIGH_BYTE;
      else if (machine == IMMEDIATE && instruction == 0x36)
        machine = WRITE;
      else
        machine = FETCH;
      t_state = 0;
    }
  } else if (t_state == 4) {
    assert(machine == FETCH);
    if (instruction == 0x21)
      machine = LOW_BYTE;
    else if (instruction == 0x36 || instruction == 0x3E)
      machine = IMMEDIATE;
    else {
      assert(instruction == 0x7E);
      machine = READ;
    }
    t_state = 0;
  }
}

void gpio_init(uint pin) {
  preloaded[pin] = false;
  outputs[pin] = false;
}
void gpio_put(uint pin, bool value) {
  ++gpio_writes;
  if (pin == PIN_RESET_N) {
    if (value && !levels[pin]) {
      assert(model_reset_clocks >= 6);
      exit_tail = 2;
    } else if (!value) {
      model_pc = hl = 0;
      accumulator = 0;
      machine = FETCH;
      t_state = 0;
      rd_n = wr_n = true;
    }
  }
  if (pin == PIN_CLK && value != levels[pin]) {
    if (value) {
      assert(now_us - clock_low_at >=
             (automatic_clocks ? 1u : STEP_HALF_PERIOD_US));
      clock_high_at = now_us;
    } else {
      assert(now_us - clock_high_at >=
             (automatic_clocks ? 1u : STEP_HALF_PERIOD_US));
      clock_low_at = now_us;
      cpu_clock();
    }
  }
  if (pin == PIN_DATA_UP_OE_N && !value) {
    assert(levels[PIN_DATA_DOWN_OE_N] && levels[PIN_BOOT_READ_DISABLE]);
    assert(wr_n);
  }
  if (pin == PIN_DATA_DOWN_OE_N && !value) {
    assert(levels[PIN_DATA_UP_OE_N]);
    for (uint data = PIN_DATA_0; data <= PIN_DATA_7; ++data)
      assert(!outputs[data]);
  }
  if (pin == PIN_BOOT_READ_DISABLE && !value)
    assert(levels[PIN_DATA_UP_OE_N]);
  levels[pin] = value;
  preloaded[pin] = true;
  if ((pin == PIN_DATA_UP_OE_N || pin == PIN_DATA_DOWN_OE_N) &&
      value && levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N])
    isolated_at = now_us;
}
bool gpio_get(uint pin) {
  switch (pin) {
  case PIN_BUSACK_N: return fault != BUSACK_FAULT;
  case PIN_IORQ_N: return fault != IORQ_FAULT;
  case PIN_RD_N: return fault == RD_FAULT ? true : rd_n;
  case PIN_WR_N: return wr_n;
  case PIN_PORT_A0: return (physical_address & 1) != 0;
  case PIN_PORT_A1: return (physical_address & 2) != 0;
  case PIN_PORT_A2: return (physical_address & 4) != 0;
  case PIN_PORT_A4:
    return ((physical_address & 16) != 0) ^ (fault == ADDRESS_FAULT);
  default:
    if (pin >= PIN_DATA_0 && pin <= PIN_DATA_7 &&
        !levels[PIN_DATA_DOWN_OE_N]) {
      assert(!outputs[pin]);
      return (bus_value() & (1u << (pin - PIN_DATA_0))) != 0;
    }
    return levels[pin];
  }
}
void gpio_set_dir(uint pin, bool output) {
  assert(!output || preloaded[pin]);
  if (output && pin >= PIN_DATA_0 && pin <= PIN_DATA_7) {
    assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
    assert(now_us - isolated_at >= 1);
  }
  outputs[pin] = output;
}
void gpio_disable_pulls(uint pin) { (void)pin; }
void gpio_set_function(uint pin, uint function) {
  assert(pin == PIN_CLK && function == GPIO_FUNC_SIO);
}
uint gpio_get_function(uint pin) { assert(pin == PIN_CLK); return GPIO_FUNC_SIO; }
void busy_wait_us_32(uint32_t delay) { now_us += delay; }
void sleep_ms(uint32_t delay) { (void)delay; assert(false); }
void tight_loop_contents(void) {
  if (idle_budget && --idle_budget == 0)
    longjmp(usb_disconnected, 2);
}
absolute_time_t make_timeout_time_us(uint32_t delay) { return now_us + delay; }
bool time_reached(absolute_time_t deadline) { return now_us >= deadline; }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) {
  if (idle_connected)
    return true;
  if (!usb_commands)
    return false;
  if (usb_finished) {
    if (!levels[PIN_RESET_N] && !z80_full_memory_test_active())
      longjmp(usb_disconnected, 1);
    return false;
  }
  if (!*usb_commands) {
    usb_finished = true;
    return false;
  }
  return true;
}
int getchar_timeout_us(uint32_t timeout_us) {
  if (idle_connected)
    return -1;
  assert(timeout_us == 0 && usb_commands && *usb_commands);
  return *usb_commands++;
}
uint32_t clock_get_hz(uint clock) { assert(clock == clk_sys); return 144000000; }
uint pwm_gpio_to_slice_num(uint pin) { assert(pin == PIN_CLK); return 0; }
uint pwm_gpio_to_channel(uint pin) { assert(pin == PIN_CLK); return 0; }
void pwm_set_enabled(uint slice, bool enabled) { assert(slice == 0 && !enabled); }
void pwm_set_clkdiv_int_frac4(uint slice, uint8_t div, uint8_t frac) {
  (void)slice; (void)div; (void)frac; assert(false);
}
void pwm_set_wrap(uint slice, uint16_t wrap) {
  (void)slice; (void)wrap; assert(false);
}
void pwm_set_chan_level(uint slice, uint channel, uint16_t level) {
  (void)slice; (void)channel; (void)level; assert(false);
}
void pwm_set_counter(uint slice, uint16_t counter) {
  (void)slice; (void)counter; assert(false);
}

static void fixture(fault_t requested_fault) {
  memset(levels, 0, sizeof(levels));
  memset(outputs, 0, sizeof(outputs));
  memset(preloaded, 0, sizeof(preloaded));
  memset(memory, 0xA5, sizeof(memory));
  transcript_used = 0;
  transcript[0] = 0;
  model_reset_clocks = physical_clocks = writes = reads = clears = 0;
  gpio_writes = exit_tail = 0;
  now_us = isolated_at = clock_high_at = clock_low_at = 0;
  physical_address = 0;
  instruction = low_byte = immediate = 0;
  fault = requested_fault;
  pending_command = 0;
  invalid_line = false;
  usb_commands = NULL;
  usb_finished = false;
  automatic_clocks = false;
  idle_connected = false;
  idle_budget = 0;
  z80_safe_startup();
  begin_test();
}

static void send_line(const char *line) {
  while (*line)
    handle_character(*line++);
}

static void assert_stopped(void) {
  assert(!levels[PIN_CLK] && !levels[PIN_RESET_N]);
  assert(levels[PIN_BOOT_READ_DISABLE] && levels[PIN_BUSREQ_N]);
  assert(!levels[PIN_IO_RELEASE]);
  assert(levels[PIN_DATA_UP_OE_N] && levels[PIN_DATA_DOWN_OE_N]);
}

static unsigned run_sequence(void) {
  unsigned steps = 0;
  while (phase != STOPPED && phase != COMPLETE) {
    assert(++steps < 400);
    unsigned before = physical_clocks;
    send_line("n\r\n");
    assert(physical_clocks - before <= 1);
    assert(!levels[PIN_CLK]);
    unsigned puts_before = gpio_writes;
    if (steps % 17 == 0)
      send_line("r\nh\n?\n");
    assert(physical_clocks - before <= 1 && gpio_writes == puts_before);
  }
  return steps;
}

static void test_disconnect(unsigned advances) {
  char commands[1000] = "b\n";
  for (unsigned i = 0; i < advances; ++i)
    strcat(commands, "n\n");
  fixture(NO_FAULT);
  usb_commands = commands;
  if (setjmp(usb_disconnected) == 0)
    step_loader_firmware_main();
  assert_stopped();
  assert(phase == STOPPED);
}

int main(void) {
  for (unsigned connected = 0; connected < 2; ++connected) {
    fixture(NO_FAULT);
    transcript_used = 0;
    transcript[0] = 0;
    idle_connected = connected != 0;
    idle_budget = 100;
    if (setjmp(usb_disconnected) == 0)
      step_loader_firmware_main();
    assert(physical_clocks == 0 && writes == 0 && reads == 0);
    assert(!z80_full_memory_test_active() && phase == STOPPED);
    assert_stopped();
    if (connected)
      assert(strstr(transcript, "f + Enter"));
    else
      assert(transcript_used == 0);
  }
  fixture(NO_FAULT);
  handle_character('n');
  assert(physical_clocks == 0);
  send_line("\r\n");
  assert(physical_clocks == 1);
  send_line("nn\n\n");
  assert(physical_clocks == 1);
  fixture(NO_FAULT);
  unsigned steps = run_sequence();
  assert(phase == COMPLETE && !strstr(transcript, "FAIL"));
  assert(model_reset_clocks == 6 && physical_clocks == 182);
  assert(total_clocks == physical_clocks);
  assert(writes == 3 && reads == 6 && clears == 6 && verified_reads == 6);
  assert(injected_pc == 42);
  assert(memory[0] == 0 && memory[1] == 1 && memory[2] == 2);
  for (unsigned address = 0; address < sizeof(memory); ++address)
    if (address != 0 && address != 1 && address != 2)
      assert(memory[address] == 0xA5);
  assert(strstr(transcript, "Z80 WRITE BUS SAMPLE"));
  assert(strstr(transcript, "SRAM READ BUS SAMPLE"));
  assert(strstr(transcript, "expected=02 actual=02"));
  assert(strstr(transcript, "no separate physical data-bus clear"));
  assert(strstr(transcript, "FINAL RETENTION"));
  assert(strstr(transcript, "PASS: three immediate and three final"));
  assert_stopped();
  unsigned clocks = physical_clocks;
  send_line("n\n");
  assert(physical_clocks == clocks);

  for (fault_t injected_fault = READ_FAULT;
       injected_fault <= ADDRESS_ALIAS; ++injected_fault) {
    fixture(injected_fault);
    run_sequence();
    assert(phase == STOPPED && strstr(transcript, "FAIL:"));
    assert(!strstr(transcript, "PASS:"));
    assert_stopped();
    clocks = physical_clocks;
    send_line("n\n");
    assert(physical_clocks == clocks);
    if (injected_fault == ADDRESS_ALIAS)
      assert(writes == 3 && reads == 3 && verified_reads == 3);
    send_line("b\n");
    assert(phase == RESET_CLOCK && total_clocks == 0);
  }

  unsigned drive_stop = 0;
  unsigned read_stop = 0;
  for (unsigned stop = 0; stop <= steps; ++stop) {
    fixture(NO_FAULT);
    for (unsigned i = 0; i < stop; ++i)
      send_line("n\n");
    if (!drive_stop && !levels[PIN_DATA_UP_OE_N])
      drive_stop = stop;
    if (!read_stop && !levels[PIN_BOOT_READ_DISABLE])
      read_stop = stop;
    clocks = physical_clocks;
    handle_character('n');
    handle_character('x');
    assert(phase == STOPPED && physical_clocks == clocks);
    assert_stopped();
    send_line("n\nn\n");
    assert(phase == STOPPED && physical_clocks == clocks);
  }
  assert(drive_stop && read_stop);
  test_disconnect(drive_stop);
  test_disconnect(read_stop);
  fixture(NO_FAULT);
  automatic_clocks = true;
  send_line("f\n");
  assert(z80_full_memory_test_active() && phase == STOPPED);
  clocks = physical_clocks;
  send_line("n\nb\nf\n");
  assert(physical_clocks == clocks && z80_full_memory_test_active());
  handle_character('x');
  assert(!z80_full_memory_test_active());
  assert_stopped();
  send_line("\nb\n");
  assert(phase == RESET_CLOCK);

  fixture(NO_FAULT);
  automatic_clocks = true;
  usb_commands = "f\n";
  if (setjmp(usb_disconnected) == 0)
    step_loader_firmware_main();
  assert(!z80_full_memory_test_active());
  assert_stopped();
  puts("PASS: manual cycles, decoded Z80 writes/clears/reads, retention,"
       " ownership, faults, every-step abort, full-mode commands,"
       " serial startup gating and USB disconnect");
}
