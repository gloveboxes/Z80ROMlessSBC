#include "z80sbc/banked_pio.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "z80sbc/bus.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"
#include "z80sbc/ram_timer.h"
#include "z80sbc/supervisor.h"
#include "z80sbc/gpio_mux.h"
#include "banked_io.pio.h"

enum {
  IO_HOLD_IRQ = 1, CPU_HOLD_IRQ = 2,
  IO_SM = 0, /* PIO1 is exclusive, so its engine is always state machine 0 */
  BANK_PORT = 0x07, /* OUT is completed by PIO; IN reports the bank status */
  NO_PORT = 0xFF, /* never decoded: ports alias into Z80_PORT_ADDRESS_MASK */
  SNAPSHOT_PORT_SHIFT = PIN_PORT_A0 - PIN_DATA_0,
  SNAPSHOT_RD = 1u << (PIN_RD_N - PIN_DATA_0),
  SNAPSHOT_WR = 1u << (PIN_WR_N - PIN_DATA_0),
  /* IN reply: OUT pins GP8..GP17 = A16, IO_RELEASE (LOW), D0-D7; then the
   * pindirs used while driving (all ten outputs) and when idle (GP8/GP9). */
  REPLY_DATA_SHIFT = PIN_DATA_0 - PIN_MEMORY_BANK,
  REPLY_DRIVE_DIRS = 0x3FFu << 10,
  REPLY_IDLE_DIRS = 0x003u << 20,
  CONTROL_MASK = (1u << PIN_DATA_UP_OE_N) | (1u << PIN_DATA_DOWN_OE_N) |
                 (1u << PIN_MEMORY_BANK) | (1u << PIN_IO_RELEASE),
  IO_DATA_MASK = 0xFFu << PIN_DATA_0,
};
_Static_assert(PIN_PORT_A0 - PIN_DATA_0 == 8, "OUT snapshot layout changed");
_Static_assert(PIN_DATA_DOWN_OE_N == PIN_DATA_UP_OE_N + 1 &&
                   PIN_IO_RELEASE == PIN_MEMORY_BANK + 1 &&
                   PIN_DATA_0 == PIN_MEMORY_BANK + 2,
               "PIO1 SET/OUT pin windows changed");

/* PIO0 runs the Z80 clock beside the status OLED's I2C; PIO1 runs the whole I/O
 * engine and holds the clock through PIO0 IRQ 1 with IRQ PREV. The CYW43 SPI
 * driver claims PIO2. Core 0 owns the clock SM and all of PIO1. */
static PIO const clock_engine = pio0;
static PIO const io_engine = pio1;
static int clock_sm = -1;
static uint clock_offset, io_offset;
static volatile bool allocated, io_active, clock_running;
static uint64_t pending_since;
static uint32_t polled_progress;
/* Everything the PIO1 IRQ touches is one object, so the service path reaches
 * it from a single base address. */
static struct {
  uint8_t (*read)(uint8_t port, void *context);
  void (*write)(uint8_t port, uint8_t value, void *context);
  void *context;
  void (*fault)(uint32_t controls);
  bool (*read_gate)(void);
  /* The only per-cycle counters: the watchdog's progress and the latency
   * window's counts both derive from them. */
  volatile uint32_t reads, writes;
  uint32_t window_reads, window_writes, in_max_cycles, out_max_cycles;
  uint32_t deferred_cycles;
  uint64_t in_cycles, out_cycles;
  uint8_t gated_port, deferred_port, last_port, last_value;
  volatile bool deferred_read;
  bool last_read, faulted, cycle_counter;
} engine = {.gated_port = NO_PORT};

void z80_banked_pio_set_read_gate(uint8_t port, bool (*ready)(void)) {
  engine.read_gate = ready;
  engine.gated_port = ready ? port : NO_PORT;
}

void __not_in_flash_func(z80_banked_pio_clock_stop)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!allocated || !clock_running) {
    restore_interrupts(interrupts);
    return;
  }
  clock_engine->irq_force = 1u << CPU_HOLD_IRQ;
  pio_sm_set_enabled(clock_engine, (uint)clock_sm, false);
  clock_running = false;
  /* Do not force a falling edge: z80_clock_prepare_stepping extends a frozen
   * HIGH by 1 us before handing CLK to SIO. */
  restore_interrupts(interrupts);
}

void __not_in_flash_func(z80_banked_pio_clock_resume)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!allocated) {
    restore_interrupts(interrupts);
    return;
  }
  clock_engine->irq_force = 1u << CPU_HOLD_IRQ;
  pio_sm_set_enabled(clock_engine, (uint)clock_sm, false);
  pio_sm_restart(clock_engine, (uint)clock_sm);
  /* SET is permanently mapped to CLK on this SM. Avoid the SDK's general
   * pin-mask helper, which saves and rewrites the pin configuration. */
  pio_sm_exec(clock_engine, (uint)clock_sm, pio_encode_set(pio_pins, 0));
  pio_sm_exec(clock_engine, (uint)clock_sm,
              pio_encode_jmp(clock_offset + banked_clock_offset_clock_end));
  /* SIO and PIO now both hold CLK LOW. The first HIGH is a complete pulse. */
  z80_gpio_mux_select(PIN_CLK, GPIO_FUNC_PIO0);
  pio_sm_set_enabled(clock_engine, (uint)clock_sm, true);
  pio_interrupt_clear(clock_engine, CPU_HOLD_IRQ);
  clock_running = true;
  restore_interrupts(interrupts);
}

bool z80_banked_pio_clock_start(uint32_t hz) {
  if (!allocated || hz != 20000000u || clock_get_hz(clk_sys) != 120000000u)
    return false;
  z80_clock_prepare_stepping();
  busy_wait_us_32(1);
  z80_banked_pio_clock_resume();
  return true;
}

/* Hand the I/O pins back to SIO: buffers isolated, WAIT armed, D0-D7 inputs,
 * A16 unchanged. Disable outputs before the data pins change owner. */
static void sio_controls(unsigned bank) {
  gpio_put(PIN_DATA_UP_OE_N, 1);
  gpio_put(PIN_DATA_DOWN_OE_N, 1);
  gpio_put(PIN_IO_RELEASE, 0);
  gpio_put(PIN_MEMORY_BANK, bank);
  gpio_set_dir_masked(IO_DATA_MASK, 0);
  gpio_set_function(PIN_DATA_UP_OE_N, GPIO_FUNC_SIO);
  gpio_set_function(PIN_DATA_DOWN_OE_N, GPIO_FUNC_SIO);
  gpio_set_function(PIN_IO_RELEASE, GPIO_FUNC_SIO);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin)
    gpio_set_function(pin, GPIO_FUNC_SIO);
  gpio_set_function(PIN_MEMORY_BANK, GPIO_FUNC_SIO);
}

static void __not_in_flash_func(select_bank)(unsigned bank) {
  /* Only reset and the loader call this hook; every OUT 07h is PIO-only. */
  uint32_t interrupts = save_and_disable_interrupts();
  pio_sm_set_enabled(io_engine, IO_SM, false);
  gpio_put(PIN_MEMORY_BANK, bank);
  pio_sm_set_pins_with_mask(io_engine, IO_SM,
                            (1u << PIN_DATA_UP_OE_N) | (1u << PIN_DATA_DOWN_OE_N) |
                            (bank << PIN_MEMORY_BANK),
                            CONTROL_MASK);
  pio_interrupt_clear(clock_engine, IO_HOLD_IRQ);
  pio_sm_restart(io_engine, IO_SM);
  pio_sm_exec(io_engine, IO_SM,
              pio_encode_jmp(io_offset + banked_io_offset_io_start));
  pio_sm_set_enabled(io_engine, IO_SM, true);
  restore_interrupts(interrupts);
}

/* Apply PIO1's snapshots in bus order: OUTs PIO already completed, then at
 * most one IN that is waiting, with WAIT held, for its reply. Every path that
 * must observe all prior OUTs (an IN, bus grant, disable, stop) runs this.
 * Core 0 only, never reentered: IRQ context or interrupts masked. The reply
 * is queued before any bookkeeping, which the Z80 does not wait for. */
static void __not_in_flash_func(service_fifo)(bool reply) {
  for (;;) {
    uint32_t started = z80_cycle_count();
    uint8_t port = engine.deferred_port;
    if (!engine.deferred_read) {
      if (pio_sm_is_rx_fifo_empty(io_engine, IO_SM))
        return;
      uint32_t snapshot = pio_sm_get(io_engine, IO_SM);
      port = z80_port_decode(snapshot >> SNAPSHOT_PORT_SHIFT);
      bool read = !(snapshot & SNAPSHOT_RD);
      if (read == !(snapshot & SNAPSHOT_WR)) {
        /* The handler disables the engine, which must not service again. */
        engine.faulted = true;
        engine.fault(snapshot << PIN_DATA_0);
      }
      if (!read) {
        engine.write(port, (uint8_t)snapshot, engine.context);
        uint32_t cycles = z80_cycle_count() - started;
        engine.last_port = port;
        engine.last_value = (uint8_t)snapshot;
        engine.last_read = false;
        ++engine.writes;
        engine.out_cycles += cycles;
        if (cycles > engine.out_max_cycles)
          engine.out_max_cycles = cycles;
        continue;
      }
    }
    /* PIO is stalled on this IN, so nothing can follow it in the FIFO. */
    if (!reply)
      return;
    if (port == engine.gated_port && !engine.read_gate()) {
      if (!engine.deferred_read) {
        engine.deferred_port = port;
        engine.deferred_cycles = z80_cycle_count() - started;
        engine.deferred_read = true;
      }
      return;
    }
    uint8_t value = port == BANK_PORT ? (uint8_t)z80_memory_bank_status()
                                      : engine.read(port, engine.context);
    pio_sm_put(io_engine, IO_SM,
               (uint32_t)value << REPLY_DATA_SHIFT | gpio_get(PIN_MEMORY_BANK) |
                   REPLY_DRIVE_DIRS | REPLY_IDLE_DIRS);
    uint32_t cycles = z80_cycle_count() - started;
    if (engine.deferred_read) {
      cycles += engine.deferred_cycles;
      engine.deferred_read = false;
    }
    engine.last_port = port;
    engine.last_value = value;
    engine.last_read = true;
    ++engine.reads;
    engine.in_cycles += cycles;
    if (cycles > engine.in_max_cycles)
      engine.in_max_cycles = cycles;
  }
}

void __not_in_flash_func(z80_banked_pio_drain)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (io_active)
    service_fifo(true);
  restore_interrupts(interrupts);
}

static void __not_in_flash_func(io_irq_handler)(void) {
  if (io_active)
    service_fifo(true);
}

static void stop_io(void) {
  pio_set_irq0_source_enabled(
      io_engine, pio_get_rx_fifo_not_empty_interrupt_source(IO_SM), false);
  pio_sm_set_enabled(io_engine, IO_SM, false);
  /* Apply completed OUTs. An IN still waiting is not answered: the engine is
   * stopped, so it could never complete, and its read may have side effects. */
  if (io_active && !engine.faulted)
    service_fifo(false);
}

void __not_in_flash_func(z80_banked_pio_disable)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!io_active) {
    restore_interrupts(interrupts);
    return;
  }
  bool resume = clock_running;
  z80_clock_prepare_stepping();
  stop_io();
  sio_controls(gpio_get(PIN_MEMORY_BANK));
  z80_memory_bank_set_driver(NULL);
  pio_interrupt_clear(clock_engine, IO_HOLD_IRQ);
  io_active = false;
  engine.deferred_read = false;
  pending_since = 0;
  if (resume)
    z80_banked_pio_clock_resume();
  restore_interrupts(interrupts);
}

static bool block_free(PIO pio) {
  for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
    if (pio_sm_is_claimed(pio, sm))
      return false;
  return true;
}

static bool allocate_engine(void) {
  if (allocated)
    return true;
  /* The clock owns PIO0 IRQ flags 1..2 (the status OLED's I2C uses none) and
   * the engine fills PIO1's instruction store, so PIO1 is not shared. */
  if (clock_get_hz(clk_sys) != 120000000u || !block_free(io_engine) ||
      !pio_can_add_program(clock_engine, &banked_clock_program) ||
      !pio_can_add_program(io_engine, &banked_io_program) ||
      irq_get_exclusive_handler(PIO1_IRQ_0) != NULL ||
      irq_has_shared_handler(PIO1_IRQ_0))
    return false;
  clock_sm = pio_claim_unused_sm(clock_engine, false);
  if (clock_sm < 0)
    return false;
  pio_sm_claim(io_engine, IO_SM);
  z80_clock_prepare_stepping();
  engine.cycle_counter = z80_cycle_counter_enable();
  clock_offset = pio_add_program(clock_engine, &banked_clock_program);
  io_offset = pio_add_program(io_engine, &banked_io_program);
  pio_sm_config clock_config =
      banked_clock_program_get_default_config(clock_offset);
  sm_config_set_set_pins(&clock_config, PIN_CLK, 1);
  sm_config_set_clkdiv(&clock_config, 1.0f);
  pio_sm_init(clock_engine, (uint)clock_sm,
              clock_offset + banked_clock_offset_clock_end, &clock_config);
  pio_sm_set_consecutive_pindirs(clock_engine, (uint)clock_sm, PIN_CLK, 1, true);
  clock_engine->irq_force = 1u << CPU_HOLD_IRQ;
  irq_set_exclusive_handler(PIO1_IRQ_0, io_irq_handler);
  irq_set_enabled(PIO1_IRQ_0, true);
  allocated = true;
  return true;
}

bool z80_banked_pio_rearm(uint8_t (*read_handler)(uint8_t port, void *context),
                          void (*write_handler)(uint8_t port, uint8_t value,
                                                void *context),
                          void *context,
                          void (*fault_handler)(uint32_t controls)) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!allocate_engine()) {
    restore_interrupts(interrupts);
    return false;
  }
  /* Without a verified bank 1, GP8 stays on SIO at its current level, so a
   * PIO-completed OUT 07h cannot select an unloaded bank. */
  bool banked = z80_memory_bank_status() != 0;
  bool resume = clock_running;
  z80_clock_prepare_stepping();
  stop_io();
  unsigned bank = gpio_get(PIN_MEMORY_BANK);
  sio_controls(bank);
  busy_wait_us_32(1);
  pio_sm_config config = banked_io_program_get_default_config(io_offset);
  sm_config_set_in_pins(&config, PIN_DATA_0);
  sm_config_set_out_pins(&config, PIN_MEMORY_BANK, 10);
  sm_config_set_set_pins(&config, PIN_DATA_UP_OE_N, 2);
  sm_config_set_sideset_pins(&config, PIN_IO_RELEASE);
  sm_config_set_jmp_pin(&config, PIN_RD_N);
  sm_config_set_out_shift(&config, true, false, 32);
  sm_config_set_in_shift(&config, true, true, 32);
  pio_sm_init(io_engine, IO_SM,
              io_offset + banked_io_offset_io_start, &config);
  pio_sm_set_pins_with_mask(io_engine, IO_SM,
                            (1u << PIN_DATA_UP_OE_N) | (1u << PIN_DATA_DOWN_OE_N) |
                            (bank << PIN_MEMORY_BANK),
                            CONTROL_MASK);
  pio_sm_set_pindirs_with_mask(io_engine, IO_SM, CONTROL_MASK,
                               CONTROL_MASK | IO_DATA_MASK);
  pio_gpio_init(io_engine, PIN_DATA_UP_OE_N);
  pio_gpio_init(io_engine, PIN_DATA_DOWN_OE_N);
  pio_gpio_init(io_engine, PIN_IO_RELEASE);
  for (uint pin = PIN_DATA_0; pin <= PIN_DATA_7; ++pin)
    pio_gpio_init(io_engine, pin);
  if (banked)
    pio_gpio_init(io_engine, PIN_MEMORY_BANK);
  pio_interrupt_clear(clock_engine, IO_HOLD_IRQ);
  engine.read = read_handler;
  engine.write = write_handler;
  engine.context = context;
  engine.fault = fault_handler;
  engine.deferred_read = false;
  pending_since = 0;
  polled_progress = engine.reads + engine.writes;
  io_active = true;
  z80_memory_bank_set_driver(banked ? select_bank : NULL);
  pio_set_irq0_source_enabled(
      io_engine, pio_get_rx_fifo_not_empty_interrupt_source(IO_SM), true);
  pio_sm_set_enabled(io_engine, IO_SM, true);
  if (resume)
    z80_banked_pio_clock_resume();
  restore_interrupts(interrupts);
  return true;
}

/* Fail-closed watchdog for a stuck cycle. Progress is any serviced snapshot;
 * an idle sample (WAIT armed, clock not held) also counts. */
bool __not_in_flash_func(z80_banked_pio_poll)(void) {
  if (!io_active)
    return true;
  if (engine.deferred_read) {
    uint32_t interrupts = save_and_disable_interrupts();
    service_fifo(true);
    restore_interrupts(interrupts);
  }
  uint32_t seen = engine.reads + engine.writes;
  if (seen != polled_progress ||
      (!gpio_get(PIN_IO_RELEASE) &&
       !pio_interrupt_get(clock_engine, IO_HOLD_IRQ))) {
    polled_progress = seen;
    pending_since = 0;
    return true;
  }
  uint64_t now = z80_time_us_64();
  if (!pending_since)
    pending_since = now + 1;
  return now < pending_since || now - pending_since < 500000u;
}

z80_banked_pio_status_t z80_banked_pio_status(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  z80_banked_pio_status_t result = {
    .reads = engine.reads, .writes = engine.writes, .pins = gpio_get_all(),
    .port = engine.last_port, .value = engine.last_value,
    .active = io_active, .running = clock_running, .read = engine.last_read,
  };
  if (allocated) {
    result.clock_pc = pio_sm_get_pc(clock_engine, (uint)clock_sm) - clock_offset;
    result.io_pc = pio_sm_get_pc(io_engine, IO_SM) - io_offset;
    for (unsigned irq = IO_HOLD_IRQ; irq <= CPU_HOLD_IRQ; ++irq)
      result.irqs |= (unsigned)pio_interrupt_get(clock_engine, irq) << irq;
  }
  restore_interrupts(interrupts);
  return result;
}

z80_banked_pio_latency_t z80_banked_pio_take_latency(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  z80_banked_pio_latency_t result = {
    .available = engine.cycle_counter,
    .in_count = engine.reads - engine.window_reads,
    .in_max_cycles = engine.in_max_cycles,
    .out_count = engine.writes - engine.window_writes,
    .out_max_cycles = engine.out_max_cycles,
    .in_cycles = engine.in_cycles, .out_cycles = engine.out_cycles,
  };
  engine.window_reads = engine.reads;
  engine.window_writes = engine.writes;
  engine.in_max_cycles = engine.out_max_cycles = 0;
  engine.in_cycles = engine.out_cycles = 0;
  restore_interrupts(interrupts);
  return result;
}
