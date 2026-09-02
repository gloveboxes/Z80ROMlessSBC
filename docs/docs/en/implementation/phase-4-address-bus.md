# 8.5 Phase 4 - Direct Address Bus and Reset Isolation

**Prerequisite:** The [Phase 3 pass gate](phase-3-address-generator.md#pass-gate) must
pass.

**Install:** No additional IC. Keep Z80 and SRAM removed.

## Wiring - address-trunk verification

Add no signal wiring in this phase. Continuity-check the Phase 3 A0-A15
trunk from each MCP23S17 pin to both empty destination sockets, verify no
neighboring address lines are shorted, and then exercise the existing wiring
electrically as described in the test plan.

**Firmware feature:** Address helpers must assert ADDR_ENABLE LOW,
release MCP reset, wait, preload OLAT, and only then set IODIR outputs.
On every exit they must assert reset again.

**Implementation:** [Phase 4 application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage04_address_bus/main.c),
using the shared [bus module](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/bus.c).

## Contention-Safe Bus Isolation (Phases 4-6)

**Maintained source:** [bus.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/bus.h)
and [bus.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/bus.c).

ADDR_ENABLE and DATA_ENABLE must both be LOW for isolation. DATA_ENABLE
must remain LOW before DATA_DIR changes.

```c
static void isolate_buses(void) {
  gpio_put(PIN_ADDR_ENABLE, 0);
  gpio_put(PIN_DATA_ENABLE, 0);
}
```

## Data and Address Bus GPIO Helpers (Phases 4-6)

**Maintained source:** [bus.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/bus.h)
and [bus.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/bus.c).

`PIN_DATA_0`-`PIN_DATA_7` (GP10-GP17) connect to both fixed-direction
data transceivers. `PIN_DATA_DIR` and `PIN_DATA_ENABLE` feed the GAL,
which enables exactly one selected path. Address bytes reach the shared bus through the
MCP23S17's own output latches, already exercised by the
[Phase 3 register test](phase-3-address-generator.md#mcp23s17-register-and-port-test-phases-3-4)
and again in this phase. Call these helpers only while RESET# is asserted or
`request_cpu_bus()` has returned true, and keep SRAM CE# HIGH while
changing address or data direction.

```c
static const uint DATA_PINS[8] = {
  PIN_DATA_0, PIN_DATA_1, PIN_DATA_2, PIN_DATA_3,
  PIN_DATA_4, PIN_DATA_5, PIN_DATA_6, PIN_DATA_7
};

static void data_bus_drive(uint8_t value) {
  gpio_put(PIN_DATA_ENABLE, 0);
  for (size_t i = 0; i < 8; ++i) {
    gpio_put(DATA_PINS[i], (value >> i) & 1);  // Preload before enabling output.
    gpio_set_dir(DATA_PINS[i], GPIO_OUT);
  }
  gpio_put(PIN_DATA_DIR, 1); // Select AHCT Pico-to-bus path.
  busy_wait_us_32(1);
  gpio_put(PIN_DATA_ENABLE, 1);
}

static void data_bus_prepare_input(void) {
  gpio_put(PIN_DATA_ENABLE, 0);
  for (size_t i = 0; i < 8; ++i)
    gpio_set_dir(DATA_PINS[i], GPIO_IN);
  gpio_put(PIN_DATA_DIR, 0); // Select LVC bus-to-Pico path.
  busy_wait_us_32(1);
  gpio_put(PIN_DATA_ENABLE, 1);
}

static uint8_t data_bus_sample(void) {
  uint8_t value = 0;
  for (size_t i = 0; i < 8; ++i)
    value |= gpio_get(DATA_PINS[i]) << i;
  return value;
}

static void address_bus_drive(uint16_t address) {
  gpio_put(PIN_ADDR_ENABLE, 0);
  busy_wait_us_32(1);            // MCP RESET# low-pulse minimum.
  gpio_put(PIN_ADDR_ENABLE, 1); // Release MCP reset; ports default to inputs.
  busy_wait_us_32(1);
  mcp_write(OLATA, (uint8_t)address);
  mcp_write(OLATB, (uint8_t)(address >> 8));
  mcp_write(IODIRA, 0x00);
  mcp_write(IODIRB, 0x00);
}
```

**Test plan:**

1. With ADDR_ENABLE LOW, verify RESET# LOW and all 16 MCP pins
  high-impedance; each bus line must sit HIGH through its 10 kOhm pull-up.
2. Release reset, select MCP output direction, and test 0x55, 0xAA, walking-one, and
  walking-zero patterns on every shared address line.
3. Assert reset and use a 1 kOhm test pull-down to prove each bus line
  moves independently while MCP is isolated.
4. Release reset with IODIR inputs. Drive each bus line through 1 kOhm and
  verify the MCP reads it without driving back.
5. Test 0x0000, 0xFFFF, 0x5555, 0xAAAA, and a walking one across A0-A15,
  then the interleaved walking zero with command `a`. Run command `e` for
  1,000 release/configure/reset cycles while checking current. Scope A0,
  A7, A8, and A15 at their
  SRAM socket contacts during 0x0000, 0xFFFF, 0x5555, and 0xAAAA;
  require valid levels without double-clocking, sustained mid-rail
  plateaus, or ringing that crosses the MCP/SRAM input thresholds.

## Pass gate

Every address bit passes in both directions and MCP reset
reliably returns every port to high-impedance input mode.
