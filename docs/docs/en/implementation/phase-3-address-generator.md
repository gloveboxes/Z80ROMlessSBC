# 8.4 Phase 3 - MCP23S17 SPI Address Generator

**Prerequisite:** The [Phase 2 pass gate](phase-2-buffer-clock.md#pass-gate) must pass.

**Install:** The 3.3 V-powered SN74LVC244AN first, then Q1 and the
MCP23S17-E/SP. The already-tested AHCT244 supplies all three SPI inputs.
Keep Z80 and SRAM removed; their empty sockets expose the pulled-up
shared address bus for probing.

**Electrical hold point:** Fit level translation on all SPI inputs. The
MCP23S17 datasheet specifies $V_{IH} \ge 0.8V_{DD}$ for CS#, SCK, and
SI, which is 4.0 V with a 5 V supply; a Pico 3.3 V HIGH is therefore
not compliant. Translate Pico CS#, SCK, and MOSI through SN74AHCT244
channels 3-5 in the [output-buffer map](../hardware/output-buffer.md).
Buffer MCP SO/MISO down through SN74LVC244AN channel 2A1/2Y1 in the
[input-buffer map](../hardware/bus-isolation.md#53-sn74lvc244an-5-v-to-33-v-input-buffer);
do not connect it directly to GP20. Tie the MCP23S17 A0/A1/A2 hardware
address pins to GND as shown in the
[address-interface map](../hardware/address-interface.md). Confirm
this wiring against the schematic before proceeding.

## Wiring - SPI and MCP23S17 reset

Install and continuity-check these connections before inserting the MCP23S17.
The diagrams are included from the authoritative hardware reference so the
phase instructions and consolidated pin maps cannot drift.

{%
  include-markdown "../hardware/address-interface.md"
  start='<template id="phase-3-spi-reset-wiring">'
  end="phase-3-spi-reset-wiring-end</template>"
%}

Install the two pulled-up address trunks from the MCP23S17 to the empty Z80
and SRAM sockets. Repeated A0-A15 labels in the chip-pair views are taps on
the same physical nets, not separate or serial paths.

{%
  include-markdown "../hardware/address-interface.md"
  start='<template id="phase-3-address-trunk-wiring">'
  end="phase-3-address-trunk-wiring-end</template>"
%}

Install and continuity-check the complete 5 V-to-3.3 V input-buffer wiring
before inserting the SN74LVC244AN.

{%
  include-markdown "../hardware/bus-isolation.md"
  start='<template id="phase-3-input-buffer-wiring">'
  end="phase-3-input-buffer-wiring-end</template>"
%}

**Firmware feature:** Add byte-level SPI register read/write, a
write-then-read register test, and 16-bit walking-one/walking-zero tests
that can configure both MCP ports as either inputs or outputs.

**Implementation:** [Phase 3 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage03_mcp23s17/main.c),
using the shared [MCP23S17 driver](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/mcp23s17.c).

## MCP23S17 Register and Port Test (Phases 3-4)

**Maintained source:** [mcp23s17.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/mcp23s17.h)
and [mcp23s17.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/mcp23s17.c).

The SPI translator sits between these Pico pins and the 5 V MCP23S17;
MISO/SO returns through the SN74LVC244AN. The register test proves
communication before either address transceiver is enabled.

```c
#include "hardware/spi.h"

enum { MCP_WRITE = 0x40, MCP_READ = 0x41 };
enum { IODIRA = 0x00, IODIRB = 0x01, GPIOA = 0x12, GPIOB = 0x13,
     OLATA = 0x14, OLATB = 0x15 };

static void mcp_spi_init(void) {
  output_with_initial_level(PIN_SPI_CS_N, 1);
  spi_init(spi0, 4000000);
  gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);
}

static void mcp_write(uint8_t reg, uint8_t value) {
  uint8_t frame[] = { MCP_WRITE, reg, value };
  gpio_put(PIN_SPI_CS_N, 0);
  spi_write_blocking(spi0, frame, sizeof frame);
  gpio_put(PIN_SPI_CS_N, 1);
}

static uint8_t mcp_read(uint8_t reg) {
  uint8_t tx[] = { MCP_READ, reg, 0x00 };
  uint8_t rx[sizeof tx];
  gpio_put(PIN_SPI_CS_N, 0);
  spi_write_read_blocking(spi0, tx, rx, sizeof tx);
  gpio_put(PIN_SPI_CS_N, 1);
  return rx[2];
}

static bool mcp_register_test(void) {
  const uint8_t patterns[] = { 0x55, 0xAA };
  gpio_put(PIN_ADDR_ENABLE, 0); // Reset and isolate the MCP23S17 outputs.
  busy_wait_us_32(1);
  gpio_put(PIN_ADDR_ENABLE, 1);
  busy_wait_us_32(1);

  bool passed = true;
  for (size_t i = 0; i < sizeof patterns; ++i) {
    mcp_write(IODIRA, patterns[i]);
    mcp_write(IODIRB, (uint8_t)~patterns[i]);
    if (mcp_read(IODIRA) != patterns[i] ||
      mcp_read(IODIRB) != (uint8_t)~patterns[i]) {
      passed = false;
      break;
    }
    mcp_write(OLATA, patterns[i]);
    mcp_write(OLATB, (uint8_t)~patterns[i]);
    if (mcp_read(OLATA) != patterns[i] ||
      mcp_read(OLATB) != (uint8_t)~patterns[i]) {
      passed = false;
      break;
    }
  }

  mcp_write(IODIRA, 0xFF);
  mcp_write(IODIRB, 0xFF);
  bool restored = mcp_read(IODIRA) == 0xFF && mcp_read(IODIRB) == 0xFF;
  gpio_put(PIN_ADDR_ENABLE, 0);
  return passed && restored;
}
```

**Test plan:**

1. Before fitting the MCP, pull each used LVC244 input LOW through
  1 kOhm, then release it HIGH through its fitted 10 kOhm pull-up.
  Verify the matching Pico input reads LOW and HIGH at 3.3 V levels
  and unused channels do not change.
2. With GP9 LOW, verify Q1 holds MCP RESET# LOW and all A0-A15 nodes
  read HIGH through the two SIP networks. Drive GP9 HIGH and require a
  clean 5 V RESET# release.
3. With compliant translation fitted, write and read back 0x55 and 0xAA
  in IODIRA, IODIRB, OLATA, and OLATB with command `m`.
4. Configure outputs and probe walking-one and walking-zero patterns at
  the empty Z80 and SRAM sockets with command `o`.
5. Configure inputs, apply 0 V or 5 V through 10 kOhm to each pin, and
  use command `i` to verify only the corresponding GPIO register bit changes.
6. Run command `e` for 10,000 alternating register writes and reads with
  zero errors.

## Pass gate

Compliant SPI levels, error-free register access, and
correct operation of all 16 port bits in both directions.
