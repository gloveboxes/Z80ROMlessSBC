#include "z80sbc/banked_pio.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "z80sbc/clock.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"
#include "z80sbc/gpio_mux.h"
#include "banked_io.pio.h"

enum { SLOW_IRQ = 0, IO_HOLD_IRQ = 1, CPU_HOLD_IRQ = 2 };
static PIO const engine = pio1;
static int clock_sm = -1, io_sm = -1;
static uint program_offset;
static volatile bool allocated, io_active, in_slow, clock_running;
static uint64_t pending_since;
static void (*service_slow)(void);
static volatile z80_banked_pio_status_t diagnostics;

bool __not_in_flash_func(z80_banked_pio_active)(void) {
  return io_active;
}

bool __not_in_flash_func(z80_banked_pio_clock_owned)(void) {
  return allocated;
}

void __not_in_flash_func(z80_banked_pio_clock_stop)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!allocated || !clock_running) {
    restore_interrupts(interrupts);
    return;
  }
  engine->irq_force = 1u << CPU_HOLD_IRQ;
  pio_sm_set_enabled(engine, (uint)clock_sm, false);
  clock_running = false;
  /* The caller retains the normal 1 us HIGH guard before switching to SIO.
   * Do not force a falling edge here: PWM and PIO have the same stop contract. */
  restore_interrupts(interrupts);
}

void __not_in_flash_func(z80_banked_pio_clock_resume)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!allocated) {
    restore_interrupts(interrupts);
    return;
  }
  engine->irq_force = 1u << CPU_HOLD_IRQ;
  pio_sm_set_enabled(engine, (uint)clock_sm, false);
  pio_sm_restart(engine, (uint)clock_sm);
  /* SET is permanently mapped to CLK on this SM. Avoid the SDK's general
   * pin-mask helper, which saves and rewrites the pin configuration. */
  pio_sm_exec(engine, (uint)clock_sm, pio_encode_set(pio_pins, 0));
  pio_sm_exec(engine, (uint)clock_sm,
              pio_encode_jmp(program_offset + banked_io_offset_clock_end));
  /* SIO and PIO now both hold CLK LOW. The first HIGH is a complete pulse. */
  z80_gpio_mux_select(PIN_CLK, GPIO_FUNC_PIO1);
  pio_sm_set_enabled(engine, (uint)clock_sm, true);
  pio_interrupt_clear(engine, CPU_HOLD_IRQ);
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

static void __not_in_flash_func(sio_controls)(void) {
  gpio_put(PIN_DATA_DOWN_OE_N, 1);
  gpio_put(PIN_IO_RELEASE, 0);
  z80_gpio_mux_select(PIN_DATA_DOWN_OE_N, GPIO_FUNC_SIO);
  z80_gpio_mux_select(PIN_IO_RELEASE, GPIO_FUNC_SIO);
}

static void __not_in_flash_func(select_bank)(unsigned bank) {
  /* Only reset/loader or the stopped slow-service path calls this hook.
   * Normal bank writes never enter ARM code. Preserve an active slow request. */
  uint32_t interrupts = save_and_disable_interrupts();
  pio_sm_set_enabled(engine, (uint)io_sm, false);
  gpio_put(PIN_MEMORY_BANK, bank);
  pio_sm_set_pins_with_mask(engine, (uint)io_sm,
                            (1u << PIN_DATA_DOWN_OE_N) | (bank << PIN_MEMORY_BANK),
                            (1u << PIN_DATA_DOWN_OE_N) | (1u << PIN_MEMORY_BANK) |
                            (1u << PIN_IO_RELEASE));
  pio_interrupt_clear(engine, IO_HOLD_IRQ);
  if (!in_slow) {
    pio_sm_restart(engine, (uint)io_sm);
    pio_sm_exec(engine, (uint)io_sm,
                pio_encode_jmp(program_offset + banked_io_offset_io_start));
    pio_sm_set_enabled(engine, (uint)io_sm, true);
  }
  restore_interrupts(interrupts);
}

static void __not_in_flash_func(slow_irq_handler)(void) {
  if (!io_active || !pio_interrupt_get(engine, SLOW_IRQ))
    return;
  if (!gpio_get(PIN_BUSACK_N)) {
    z80_banked_pio_disable();
    return;
  }
  ++diagnostics.requests;
  pio_sm_set_enabled(engine, (uint)io_sm, false);
  z80_clock_stop();
  busy_wait_us_32(1);
  /* IRQ WAIT stalls the I/O SM with GP7 HIGH and GP9 LOW. Preloaded SIO
   * levels match before mux handoff; the CPU still holds the hardware WAIT.
   * Retain this hardware-qualified handoff. Keeping the pins on PIO needs
   * separate bus-settling validation: the host model missed a boot failure. */
  sio_controls();
  in_slow = true;
  service_slow();
}

void __not_in_flash_func(z80_banked_pio_slow_complete)(void) {
  if (!io_active || !in_slow)
    return;
  uint32_t interrupts = save_and_disable_interrupts();
  /* Parked throughout SIO service. Discard the IRQ WAIT continuation rather
   * than racing its completion against the clock/GPIO handoff. Rearm from a
   * known PC before a subsequent Z80 cycle can assert IORQ again. */
  pio_sm_set_enabled(engine, (uint)io_sm, false);
  /* SET=GP7 and optional side-set=GP9: isolate RX and rearm WAIT in one
   * instruction, preserving GP8/A16 and the SM's fixed pin configuration. */
  pio_sm_exec(engine, (uint)io_sm,
              pio_encode_set(pio_pins, 1) | pio_encode_sideset_opt(1, 0));
  pio_sm_restart(engine, (uint)io_sm);
  pio_sm_exec(engine, (uint)io_sm,
              pio_encode_jmp(program_offset + banked_io_offset_io_start));
  pio_interrupt_clear(engine, SLOW_IRQ);
  gpio_set_dir_masked(0xFFu << PIN_DATA_0, 0);
  z80_gpio_mux_select(PIN_DATA_DOWN_OE_N, GPIO_FUNC_PIO1);
  z80_gpio_mux_select(PIN_IO_RELEASE, GPIO_FUNC_PIO1);
  in_slow = false;
  pio_sm_set_enabled(engine, (uint)io_sm, true);
  restore_interrupts(interrupts);
}

void __not_in_flash_func(z80_banked_pio_disable)(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!io_active) {
    restore_interrupts(interrupts);
    return;
  }
  bool resume = clock_running;
  z80_clock_prepare_stepping();
  pio_set_irq0_source_enabled(engine, pis_interrupt0, false);
  pio_sm_set_enabled(engine, (uint)io_sm, false);
  unsigned bank = gpio_get(PIN_MEMORY_BANK);
  sio_controls();
  gpio_put(PIN_MEMORY_BANK, bank);
  gpio_set_function(PIN_MEMORY_BANK, GPIO_FUNC_SIO);
  z80_memory_bank_set_driver(NULL);
  pio_interrupt_clear(engine, SLOW_IRQ);
  pio_interrupt_clear(engine, IO_HOLD_IRQ);
  io_active = in_slow = false;
  pending_since = 0;
  if (resume)
    z80_banked_pio_clock_resume();
  restore_interrupts(interrupts);
}

static bool allocate_engine(void) {
  if (allocated)
    return true;
  if (clock_get_hz(clk_sys) != 120000000u ||
      !pio_can_add_program(engine, &banked_io_program) ||
      irq_get_exclusive_handler(PIO1_IRQ_0) != NULL ||
      irq_has_shared_handler(PIO1_IRQ_0))
    return false;
  /* The program consumes almost the whole instruction store and owns flags
   * 0..2. Do not attach it to a PIO block already in use by another client. */
  for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
    if (pio_sm_is_claimed(engine, sm))
      return false;
  clock_sm = pio_claim_unused_sm(engine, false);
  if (clock_sm < 0)
    return false;
  io_sm = pio_claim_unused_sm(engine, false);
  if (io_sm < 0) {
    pio_sm_unclaim(engine, (uint)clock_sm);
    clock_sm = -1;
    return false;
  }
  z80_clock_prepare_stepping();
  program_offset = pio_add_program(engine, &banked_io_program);
  pio_sm_config clock_config = banked_io_program_get_default_config(program_offset);
  sm_config_set_sideset(&clock_config, 0, false, false);
  sm_config_set_wrap(&clock_config, program_offset + banked_io_offset_clock_start,
                     program_offset + banked_io_offset_clock_end);
  sm_config_set_set_pins(&clock_config, PIN_CLK, 1);
  sm_config_set_clkdiv(&clock_config, 1.0f);
  pio_sm_init(engine, (uint)clock_sm,
               program_offset + banked_io_offset_clock_end, &clock_config);
  pio_sm_set_consecutive_pindirs(engine, (uint)clock_sm, PIN_CLK, 1, true);
  engine->irq_force = 1u << CPU_HOLD_IRQ;
  irq_set_exclusive_handler(PIO1_IRQ_0, slow_irq_handler);
  irq_set_enabled(PIO1_IRQ_0, true);
  allocated = true;
  return true;
}

bool z80_banked_pio_rearm(void (*slow_handler)(void)) {
  uint32_t interrupts = save_and_disable_interrupts();
  if (!z80_memory_bank_status() || !allocate_engine()) {
    restore_interrupts(interrupts);
    return false;
  }
  bool resume = clock_running;
  z80_clock_prepare_stepping();
  pio_set_irq0_source_enabled(engine, pis_interrupt0, false);
  pio_sm_set_enabled(engine, (uint)io_sm, false);
  unsigned bank = gpio_get(PIN_MEMORY_BANK);
  gpio_put(PIN_DATA_UP_OE_N, 1);
  sio_controls();
  busy_wait_us_32(1);
  gpio_set_dir_masked(0xFFu << PIN_DATA_0, 0);
  pio_sm_config config = banked_io_program_get_default_config(program_offset);
  sm_config_set_in_pins(&config, PIN_DATA_0);
  sm_config_set_out_pins(&config, PIN_MEMORY_BANK, 1);
  sm_config_set_set_pins(&config, PIN_DATA_DOWN_OE_N, 1);
  sm_config_set_sideset_pins(&config, PIN_IO_RELEASE);
  sm_config_set_jmp_pin(&config, PIN_BUSACK_N);
  sm_config_set_out_shift(&config, true, false, 32);
  pio_sm_init(engine, (uint)io_sm,
               program_offset + banked_io_offset_io_start, &config);
  pio_sm_set_pins_with_mask(engine, (uint)io_sm,
                            (1u << PIN_DATA_DOWN_OE_N) | (bank << PIN_MEMORY_BANK),
                            (1u << PIN_DATA_DOWN_OE_N) | (1u << PIN_MEMORY_BANK) |
                            (1u << PIN_IO_RELEASE));
  pio_sm_set_consecutive_pindirs(engine, (uint)io_sm, PIN_DATA_DOWN_OE_N, 3, true);
  for (uint pin = PIN_DATA_DOWN_OE_N; pin <= PIN_IO_RELEASE; ++pin)
    pio_gpio_init(engine, pin);
  pio_sm_clear_fifos(engine, (uint)io_sm);
  pio_interrupt_clear(engine, SLOW_IRQ);
  pio_interrupt_clear(engine, IO_HOLD_IRQ);
  service_slow = slow_handler;
  pending_since = 0;
  in_slow = false;
  io_active = true;
  z80_memory_bank_set_driver(select_bank);
  pio_set_irq0_source_enabled(engine, pis_interrupt0, true);
  pio_sm_set_enabled(engine, (uint)io_sm, true);
  if (resume)
    z80_banked_pio_clock_resume();
  restore_interrupts(interrupts);
  return true;
}

bool __not_in_flash_func(z80_banked_pio_poll)(void) {
  if (!io_active)
    return true;
  bool progress = false;
  while (!pio_sm_is_rx_fifo_empty(engine, (uint)io_sm)) {
    (void)pio_sm_get(engine, (uint)io_sm);
    progress = true;
  }
  if (progress || (!gpio_get(PIN_IO_RELEASE) &&
                   !pio_interrupt_get(engine, IO_HOLD_IRQ))) {
    pending_since = 0;
    return true;
  }
  uint64_t now = time_us_64();
  if (!pending_since)
    pending_since = now + 1;
  return now < pending_since || now - pending_since < 500000u;
}

void __not_in_flash_func(z80_banked_pio_note_io)(uint8_t port, bool read, uint8_t value) {
  diagnostics.port = port;
  diagnostics.read = read;
  diagnostics.value = value;
  if (read) ++diagnostics.reads;
  else ++diagnostics.writes;
}

z80_banked_pio_status_t z80_banked_pio_status(void) {
  uint32_t interrupts = save_and_disable_interrupts();
  z80_banked_pio_status_t result = diagnostics;
  result.active = io_active;
  result.slow = in_slow;
  result.running = clock_running;
  result.pins = gpio_get_all();
  if (allocated) {
    result.clock_pc = pio_sm_get_pc(engine, (uint)clock_sm);
    result.io_pc = pio_sm_get_pc(engine, (uint)io_sm);
    for (unsigned irq = 0; irq < 3; ++irq)
      result.irqs |= (unsigned)pio_interrupt_get(engine, irq) << irq;
  }
  restore_interrupts(interrupts);
  return result;
}
