# 8.7 Phase 6 - AS6C1008 SRAM and DMA Path

**Prerequisite:** The [Phase 5 pass gate](phase-5-data-bus.md#pass-gate) must pass.

**Install:** The AS6C1008-55PCN only after the programmed ATF22V10,
AHCT244 channels 2A2-2A4, and final SRAM-side pull-ups have passed the
[Phase 2 gate](phase-2-buffer-clock.md#pass-gate).
Keep the Z80 removed.

**What you are proving:** the Pico can independently write and read every
location and bit in SRAM. The CPU is still absent, so a failure is confined
to power, the tested bus/control paths, or SRAM itself. The Stage 6 commands
are `p` (address-derived pattern), `c` (its complement), and `m` (March test).
These are destructive RAM tests: they overwrite the contents being tested.

A **March test** walks through memory in both address directions, reading
and writing prescribed values to expose stuck bits and interactions between
locations. Passing one write/read at address zero is not enough.

When a test fails, record the address, expected byte, and actual byte before
rerunning. Their XOR identifies differing data bits; repeated failures at
addresses separated by a power of two suggest an address-line problem, not
necessarily a bad SRAM chip. Recheck those paths with power off. SRAM loses
its contents when unpowered, so rerun the writes after each power cycle.

## Wiring - SRAM socket

Before inserting the SRAM, install and continuity-check its address and data
trunk taps, buffered control inputs, fixed A16/CE2 connections, supply pins,
and local decoupling capacitor. The Z80 remains absent; its pins in these
pairwise views are contacts on the already tested shared trunks.

{%
  include-markdown "../hardware/pin-mapping.md"
  start='<template id="phase-6-sram-wiring">'
  end="phase-6-sram-wiring-end</template>"
%}

**Electrical hold point:** During DMA the GAL must select only Pico
CE#/OE#/WE#; during execution it must select only Z80 MREQ#/RD#/WR#.
CE# must not be tied LOW, and no Pico output is joined directly to a
Z80 output. With the Z80 removed, BUSACK# floats HIGH via its pull-up;
hold RESET# LOW so the programmed equations select the Pico side
regardless. Do not install SRAM until all three GAL outputs and all
three corresponding AHCT244 outputs have passed static truth-table,
continuity, and voltage-level tests.

**Firmware feature:** Add single-byte DMA read/write primitives, a
walking address/data test, a two-pass full-memory pattern test, and a
March C- or equivalent RAM test. Every failure must report its address,
expected byte, and actual byte over USB serial.

**Implementation:** [Phase 6 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/main.c),
using the shared [SRAM DMA module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/sram.c).

## SRAM DMA Access and Pattern Test (Phase 6)

**Maintained source:** [sram.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/sram.h)
and [sram.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/sram.c).

The [address and data helper functions](phase-4-address-bus.md#data-and-address-bus-gpio-helpers-phases-4-6)
represent the already tested MCP23S17 and GPIO bus operations. The
control-source selector
must grant exclusive SRAM control to the Pico before this code runs.

```c
static void dma_write_byte(uint16_t address, uint8_t value) {
  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_SRAM_OE_N, 1);
  gpio_put(PIN_SRAM_WE_N, 1);
  address_bus_drive(address);       // MCP directly drives A0-A15.
  data_bus_drive(value);            // Pico -> AHCT245 -> D0-D7.
  gpio_put(PIN_SRAM_CE_N, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_SRAM_WE_N, 0);
  busy_wait_us_32(1);
  gpio_put(PIN_SRAM_WE_N, 1);
  gpio_put(PIN_SRAM_CE_N, 1);
}

static uint8_t dma_read_byte(uint16_t address) {
  uint8_t value;
  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_SRAM_WE_N, 1);
  address_bus_drive(address);
  data_bus_prepare_input();         // Disable, reverse, then enable.
  gpio_put(PIN_SRAM_CE_N, 0);
  gpio_put(PIN_SRAM_OE_N, 0);
  busy_wait_us_32(1);
  value = data_bus_sample();
  gpio_put(PIN_SRAM_OE_N, 1);
  gpio_put(PIN_SRAM_CE_N, 1);
  gpio_put(PIN_DATA_ENABLE, 0);
  return value;
}

static bool sram_pattern_test(bool complement) {
  for (uint32_t address = 0; address < 65536; ++address) {
    uint8_t expected = (uint8_t)address ^ (uint8_t)(address >> 8);
    dma_write_byte((uint16_t)address,
             complement ? (uint8_t)~expected : expected);
  }
  for (uint32_t address = 0; address < 65536; ++address) {
    uint8_t expected = (uint8_t)address ^ (uint8_t)(address >> 8);
    if (complement)
      expected = (uint8_t)~expected;
    uint8_t actual = dma_read_byte((uint16_t)address);
    if (actual != expected) {
      printf("FAIL %04lx expected=%02x actual=%02x\n",
           (unsigned long)address, expected, actual);
      isolate_buses();
      return false;
    }
  }
  isolate_buses();
  return true;
}
```

**Test plan:**

1. With transceivers disabled, require A16 LOW, CE2 HIGH, and CE#, OE#,
  and WE# HIGH directly at the SRAM.
2. **GAL -> AHCT244 -> SRAM end-to-end:** Keep the data transceivers
  disabled. With RESET# LOW, toggle one Pico CE#/OE#/WE# candidate at a
  time and probe the corresponding GAL pre-buffer pin, AHCT244 output,
  and final SRAM pin. Keep CE# HIGH while testing OE#/WE#; test CE# only
  with OE#/WE# HIGH. Then set RESET# and pulled-up BUSACK# HIGH and repeat
  by pulling MREQ#/RD#/WR# LOW individually through 1 kOhm. Require the
  selected path to reach below 0.3 V at the SRAM, return to at least
  4.4 V, preserve polarity, and leave the other two SRAM controls HIGH.
3. In DMA mode, write and read one byte. Require WE# HIGH before address
  or data changes, and disable the Pico data driver before asserting
  OE# for readback.
4. Write unique values at 0x0000 and each power-of-two address from
  0x0001 through 0x8000. Verify every value remains independent.
5. At several addresses test 0x00, 0xFF, 0x55, 0xAA, walking-one, and
  walking-zero data.
6. Fill all 65,536 bytes with the XOR of the address bytes, verify it,
  then repeat with the complement. Run the March test afterward.
7. Repeat after ten power cycles. These Pico-driven DMA tests do not prove
  CPU memory timing at 1 MHz; test that with the installed Z80 in Phase 7.

## Pass gate

Every end-to-end control path reaches the correct SRAM
pin with no adjacent-control activity, zero address/data/full-range
pattern or March-test errors, and no overlap between CPU and Pico SRAM
control sources.
