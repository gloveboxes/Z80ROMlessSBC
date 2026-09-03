# 0. Project Inventory

The quantities below build one complete three-breadboard prototype.
Purchase quantities include a small allowance for breadboard spares.

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

> **Nine-package all-through-hole design:** The ATF22V10 computes all
> three SRAM controls from RESET#, BUSACK#, the Pico DMA
> controls, and the Z80 MREQ#/RD#/WR# outputs through the
> [SRAM control-source arbitration](pin-mapping.md#12-sram-control-source-arbitration-atf22v10bc).
> The
> ATF22V10 guarantees only a TTL-level 2.4 V HIGH, so its SRAM outputs
> pass through the SN74AHCT244N rather than driving the 5 V SRAM
> directly. The same AHCT244 translates Pico CLK, BUSREQ#, and the three
> MCP23S17 SPI inputs to guaranteed 5 V levels. Pico GP3 drives the
> Z80 RESET# input directly because the Z84C00 specifies a 2.2 V HIGH
> threshold for non-clock inputs; the Z80 clock uses the AHCT244 path to
> satisfy its stricter threshold. The data bus uses separate fixed-direction
> SN74AHCT245N and SN74LVC245AN devices because no single PDIP part
> provides a dual-supply, bidirectional, power-off-safe
> function. Two spare GAL inputs accept the existing DIR and
> master-enable controls, and two spare GAL outputs generate mutually
> exclusive active-low OEs. A third spare GAL output and one 2N3904
> hold the MCP23S17 in reset outside DMA/trap access, allowing its
> reset-default input ports to connect directly to pulled-up A0-A15 and
> safely share the address bus. The design uses 9 active packages with
> deterministic isolation and safe power sequencing. The
> SN74LVC244AN buffers RD#/WR# because Pico 2 W GP27 and GP28 are
> standard ADC-capable pads, not 5 V-tolerant FT pads. It also buffers
> BUSACK#, IORQ#, and MCP SO: although GP0, GP1, and GP20 are FT pads,
> their 5.5 V tolerance requires RP2350 IOVDD to be present at 3.3 V.
> Using the spare LVC244 channels preserves safe power sequencing and
> powered-off isolation described in the
> [SN74LVC244 input-buffer section](bus-isolation.md#53-sn74lvc244an-5-v-to-33-v-input-buffer).
>
> GAL pin 13 now accepts raw IORQ# and pin 20 drives WAIT#, adding the
> deterministic I/O interlock without another package.
>
> **AHCT244 sourcing:** Specify the exact `SN74AHCT244N` PDIP-20 part.
> TI lists this `N` package as active and in production. It is available
> from authorized distributors and in breadboard-friendly DIP form on
> AliExpress (for example, selectable-part listing `1005005865735693`).
> Do not substitute `SN74AHC244N`: at 5 V its CMOS input threshold does
> not guarantee recognition of Pico/GAL TTL HIGHs.

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

| Fitted | Purchase | Item | Purpose |
|----:|----:|----|----|
| 8 | 11 | 100 nF X7R ceramic capacitors, at least 10 V | One at every DIP logic supply pair |
| 3 | 5 | 22 uF capacitors, at least 10 V | One per breadboard |
| 1 | 2 | 100 uF electrolytic capacitor, at least 10 V | 5 V supply-entry bulk capacitance |
| 29 | 35 | 10 kOhm, 1/4 W resistors | Defined startup levels and temporary test pulls |
| 1 | 3 | 4.7 kOhm, 1/4 W resistor | GAL-to-2N3904 base current limiting |
| 1 | 3 | 47 kOhm, 1/4 W resistor | 2N3904 base-emitter pull-down |
| 3 | 4 | 8x10 kOhm bussed SIP resistor networks, 9-pin | Two A0-A15 pull-ups (common to +5 V) and one Pico D0-D7 pull-down (common to GND) |
| Reused for tests | 10 | 1 kOhm, 1/4 W resistors | First-drive current limiting and manual input tests |

Place each 100 nF capacitor directly across its IC supply pins with the
shortest practical leads. If scope captures show more than 250 mV rail
excursion at the farthest board, add 47-100 uF there and repeat the
capture; bulk capacitance does not replace local 100 nF capacitors. On
the 5 V side, fit 10 kOhm pull-ups to
BUSREQ#, BUSACK#, MREQ#, IORQ#, RD#, WR#, MCP SO, SRAM CE#, SRAM OE#,
SRAM WE#, WAIT#, INT#, and NMI#. RESET# is the direct GP3 node and must
not have a 5 V pull-up. Also pull SN74AHCT244 2A2, 2A3, and 2A4 up to 5 V
so its SRAM-control outputs remain inactive if the GAL is physically
absent. Fit 10 kOhm pull-ups on A0-A15 using two 9-pin bussed SIP
networks. On the Pico side, use 10 kOhm pull-ups to 3.3 V on GP4
(BUSREQ#), GP5 (SRAM CE#), GP21 (SPI CS#),
GP22 (SRAM WE#), and GP26 (SRAM OE#). Use 10 kOhm pull-downs to GND on
GP2 (CLK), GP3 (RESET#), GP6 (DATA_DIR), GP7 (DATA_ENABLE), GP9
(ADDR_ENABLE), and GP18/GP19 (SPI SCK/SI). Leave GP8 unconnected. Connect GP7 and GP6 directly
to ATF22V10 pins 9 and 11; its TTL-compatible inputs accept 3.3 V.
Fit the third 8x10 kOhm bussed SIP network from GP10-GP17 to GND so
the SN74AHCT245N A inputs stay defined while Pico GPIOs are inputs or
the Pico is absent. It loads each active HIGH by only 0.33 mA at 3.3 V.
Once the Pico 3.3 V rail is valid, these external resistors
establish safe levels before the firmware configures SIO. During a cold
power ramp the Pico-side pull-ups cannot hold active-low controls HIGH
while their 3.3 V rail is still at 0 V; RESET# remains asserted and SRAM
contents are therefore treated as indeterminate until the boot image is
loaded and verified. The AHCT244-input pull-ups cover an absent GAL, and
the final SRAM-node pull-ups cover an absent AHCT244. They do not provide
power-off isolation for an installed source IC. Tie every unused CMOS
input to a defined level.

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

Feed the breadboard's +5 V logic rail directly from the regulated
supply. Feed Pico VSYS from that rail only through the 1N5819: anode to
external +5 V, banded cathode to VSYS. Pico 2 W already has a Schottky
diode from USB VBUS to VSYS, so this second diode safely ORs USB and
external power without back-powering either source. Never link the
external +5 V rail directly to Pico VBUS or VSYS. Feed the
SN74LVC245AN and SN74LVC244AN from the Pico 3.3 V rail. Connect the
SN74AHCT245N and all other logic to the regulated 5 V
rail. No 5 V output connects directly to a Pico GPIO; both LVC devices
provide power-off isolation. The GAL inputs do not source 5 V into
GP6/GP7, and
the SN74LVC244AN's `Ioff` protection keeps the five
monitored inputs isolated while its 3.3 V supply is absent or ramping.
Tie the Pico's AGND pin (pin 33) to the common digital ground: this
design does not use the ADC function on GP26-GP29, so no separate
analogue ground plane is needed.

All installed 5 V logic devices must be powered from the same 5 V rail
whenever that rail is energized. In particular, do not apply 5 V with
an installed ATF22V10 or SN74AHCT244 unpowered: downstream pull-ups can
otherwise raise an output above its `VCC + 0.75 V` GAL limit or the
corresponding logic-family absolute maximum. The absent-device pull-up
behavior is valid only when that device is physically removed from its
socket. Verify 5 V continuity at every installed IC before power-up; a
missing VCC socket contact is a fault, not a supported operating mode.

The plug-in supply shown on the middle breadboard may provide the 5 V
entry point only after a load test proves 4.75 V to 5.25 V at the board
under at least 500 mA load without regulator overheating. Do not connect
that module's 3.3 V output to the Pico 3V3 pin or the system 3.3 V rail;
the Pico must remain the only 3.3 V source. Reserve terminal rows 1-3
under the module body even if its pins enter only the distribution
strip. If the actual module obscures more than three terminal rows,
move it off-board or to the Memory Board rather than compressing the
Core Board placement.

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

## 0.6 Optional Items

- LEDs with 1 kOhm series resistors for low-speed diagnostics only; do
  not permanently load high-speed buses with LEDs.
- Spare 100 nF capacitors, resistors, jumper wire, and an IC extractor.
