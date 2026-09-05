# 0. Project Inventory

The quantities below build one complete three-breadboard prototype.

## Before ordering

- Match the **whole part number and package**, not just `244` or `245`.
  AHCT and LVC devices have different supply and input-voltage requirements;
  they are not interchangeable even when their pins look alike.
- Buy through-hole parts in the listed packages. Check the wide Z80/SRAM
  sockets separately from the narrow logic sockets, and use **bussed**,
  9-pin resistor networks, not isolated resistor arrays.
- The Pico needs soldered headers. Order a header-fitted Pico 2 W or arrange
  for the headers to be soldered and inspected before breadboarding; loose
  header pins pushed through unsoldered holes are not reliable connections.
- Arrange a compatible GAL programmer **and** a PLD compile/fit tool before
  Phase 2. A blank ATF22V10 is not ready to use, and USB programming the Pico
  cannot program it.
- Have the meter and current-limited supply ready for Phase 0, the scope for
  Phase 1 onward, and the logic analyzer for the specified bus captures.
  Budget bench access to these instruments as part of the build.

## 0.1 Semiconductors

| Required | Component | Package | Function |
|----:|----|----|----|
| 1 | Raspberry Pi Pico 2 W | Module with two 20-pin headers | Supervisor, clock, DMA, virtual I/O, and Wi-Fi terminal |
| 1 | Z84C0020PEC | 40-pin PDIP | CMOS Z80 CPU |
| 1 | AS6C1008-55PCN | 32-pin PDIP | SRAM; lower 64 KiB used |
| 1 | MCP23S17-E/SP | 28-pin SPDIP | SPI-to-16-bit address interface |
| 1 | ATF22V10B-15PC or ATF22V10C-15PU | 24-pin PDIP | SRAM arbitration, data interlock, MCP reset, and I/O WAIT# control |
| 1 | SN74AHCT244N | 20-pin PDIP | Eight-channel 5 V output buffer for clock, BUSREQ#, SPI, and SRAM controls |
| 1 | SN74AHCT245N | 20-pin PDIP | Fixed Pico-to-5 V data-bus path; A side faces Pico, B side faces bus |
| 1 | SN74LVC245AN | 20-pin PDIP | Fixed 5 V-to-Pico data-bus path with 5 V-tolerant inputs and `Ioff` protection |
| 1 | SN74LVC244AN | 20-pin PDIP | 5 V-to-3.3 V buffering for Z80 status/control and MCP SO |
| 1 | 2N3904 | TO-92 | Pulls MCP23S17 RESET# LOW whenever GAL address access is disabled |
| 1 | 1N5819 | Axial diode | Schottky power OR from external +5 V to Pico VSYS |

**Nine-package all-through-hole design:**

- **SRAM arbitration:** The ATF22V10 computes all three SRAM controls from
  RESET#, BUSACK#, the Pico DMA controls, and the Z80 MREQ#/RD#/WR# outputs.
  See [SRAM control-source arbitration](pin-mapping.md#12-sram-control-source-arbitration-atf22v10bc).

- **5 V output translation:** The ATF22V10 guarantees only a TTL-level 2.4 V
  HIGH, so its SRAM outputs pass through the SN74AHCT244N rather than driving
  the 5 V SRAM directly. The same buffer translates Pico CLK, BUSREQ#, and the
  three MCP23S17 SPI inputs to guaranteed 5 V levels.

- **Reset and clock:** Pico GP3 drives Z80 RESET# directly because the Z84C00
  specifies a 2.2 V HIGH threshold for non-clock inputs. The Z80 clock uses
  the AHCT244 path to satisfy its stricter threshold.

- **Bidirectional data bus:** Separate fixed-direction SN74AHCT245N and
  SN74LVC245AN devices are required because no single PDIP part provides a
  dual-supply, bidirectional, power-off-safe function. Two spare GAL inputs
  accept DIR and master enable; two spare outputs generate mutually exclusive
  active-low OEs.

- **MCP23S17 isolation:** A third spare GAL output and one 2N3904 hold the
  MCP23S17 in reset outside DMA or trapped I/O. Its reset-default input ports
  can therefore connect directly to pulled-up A0-A15 and safely share the
  address bus.

- **5 V input monitoring:** The SN74LVC244AN buffers RD#/WR# because Pico 2 W
  GP27 and GP28 are ADC-capable pads, not 5 V-tolerant FT pads. It also buffers
  BUSACK#, IORQ#, and MCP SO. Although GP0, GP1, and GP20 are FT pads, their
  5.5 V tolerance requires RP2350 IOVDD at 3.3 V. Using the spare channels
  preserves the safe sequencing and powered-off isolation described in the
  [SN74LVC244 input-buffer section](bus-isolation.md#53-sn74lvc244an-5-v-to-33-v-input-buffer).

- **I/O interlock:** GAL pin 13 accepts raw IORQ# and pin 20 drives WAIT#,
  providing deterministic I/O interlocking without another package.

Together, these choices provide deterministic isolation and safe power
sequencing with nine active packages.

**AHCT244 sourcing:** Specify the exact `SN74AHCT244N` PDIP-20 part. TI lists
the `N` package as active and in production. It is available from authorized
distributors and in breadboard-friendly DIP form on AliExpress (for example,
selectable-part listing `1005005865735693`). Do not substitute
`SN74AHC244N`: at 5 V its CMOS input threshold does not guarantee recognition
of Pico/GAL TTL HIGHs.

## 0.2 Sockets and Headers

| Quantity | Item | Used for | Width / form factor |
|----:|----|----|----|
| 1 | 40-pin DIP socket | Z84C0020PEC Z80 | **Wide: 0.6-inch (15.24 mm) row spacing** |
| 1 | 32-pin DIP socket | AS6C1008-55PCN SRAM | **Wide: 0.6-inch (15.24 mm) row spacing** |
| 1 | 28-pin DIP socket | MCP23S17-E/SP | Narrow: 0.3-inch (7.62 mm) row spacing |
| 1 | 24-pin DIP socket | ATF22V10B-15PC or ATF22V10C-15PU | Narrow: 0.3-inch (7.62 mm) row spacing |
| 4 | 20-pin DIP sockets | SN74AHCT244N, SN74AHCT245N, SN74LVC245AN, and SN74LVC244AN | Narrow: 0.3-inch (7.62 mm) row spacing |
| 2 | 20-pin 0.1-inch male headers | Pico 2 W, if headers are not fitted | Single-row header strips |
| 2 | 20-position 0.1-inch socket strips | Removable Pico 2 W mounting; do not substitute DIP IC sockets | Single-row socket strips |

## 0.3 Capacitors and Resistors

| Fitted | Item | Purpose |
|----:|----|----|
| 8 | 100 nF X7R ceramic capacitors, at least 10 V | One at every DIP logic supply pair |
| 3 | 22 uF capacitors, at least 10 V | One per breadboard |
| 1 | 100 uF electrolytic capacitor, at least 10 V | 5 V supply-entry bulk capacitance |
| 29 | 10 kOhm, 1/4 W resistors | Defined startup levels and temporary test pulls |
| 1 | 4.7 kOhm, 1/4 W resistor | GAL-to-2N3904 base current limiting |
| 1 | 47 kOhm, 1/4 W resistor | 2N3904 base-emitter pull-down |
| 3 | 8x10 kOhm bussed SIP resistor networks, 9-pin | Two A0-A15 pull-ups (common to +5 V) and one Pico D0-D7 pull-down (common to GND) |
| Reused for tests | 1 kOhm, 1/4 W resistors | First-drive current limiting and manual input tests |

## 0.4 Construction and Power

| Quantity | Item | Requirement |
|----:|----|----|
| 3 | 830-tie-point solderless breadboards (BusBoard/X-ON BB830, ABS body; 63 terminal rows plus two 100-point power-distribution strips per board) | Memory, CPU core, and peripheral zones |
| 1 | Regulated 5 V supply | At least 1 A; adjustable current limiting preferred |
| 1 | USB data cable | Pico programming and serial diagnostics |
| 1 set | 22 AWG solid-core hookup wire | Multiple colors for signal groups |
| 1 set | Short male-to-male jumpers | Inter-board and temporary connections |
| 1 set | Short ground and power-distribution links | Connects all three boards; includes +5 V, 3.3 V, and multiple GND links |
| 1 | In-line power switch or removable supply jumper | Emergency power removal; rated at least 1.5 A continuous |
| As required | Labels or wire markers | Bus bits, controls, and socket pin 1 |

## 0.5 Bring-Up Equipment

- Digital multimeter with resistance, continuity, and DC voltage modes.
- Oscilloscope: RIGOL DHO814, four analog channels, 100 MHz bandwidth,
  1.25 GSa/s maximum real-time sample rate, 25 Mpts maximum memory depth,
  and 12-bit vertical resolution. Use its four supplied 150 MHz passive
   probes and follow the [DHO814 capture plan](oscilloscope.md) for the
   analogue signal-quality and timing checks.
- Logic analyzer: DreamSourceLab DSLogic Plus, 16 digital channels, up to
   100 MHz with all 16 channels in Buffer Mode, adjustable threshold, and
   shielded fly wires. Follow the
   [DSLogic Plus capture plan](logic-analyzer.md) for exact settings, pins,
   triggers, and expected outcomes.
- Current-limited bench supply or an in-line 5 V current meter.
- Fine probe hooks or test clips suitable for DIP pins.
- A programmer explicitly supporting the selected ATF22V10B or
  ATF22V10C device and generic 22V10 JEDEC files.
- For the optional plug-in supply's Phase 0 load test: a suitable electronic
  load and a means of checking regulator temperature without touching it.

## 0.6 Optional Items

- LEDs with 1 kOhm series resistors for low-speed diagnostics only; do
  not permanently load high-speed buses with LEDs.
- Spare 100 nF capacitors, resistors, jumper wire, and an IC extractor.
