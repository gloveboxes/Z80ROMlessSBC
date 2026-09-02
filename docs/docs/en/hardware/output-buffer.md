# 4. Output Buffer Mapping: SN74AHCT244N (DIP-20)

The 5 V-powered SN74AHCT244 provides all eight required high-level
outputs. Its TTL-compatible inputs accept both Pico 3.3 V signals and
the ATF22V10's guaranteed 2.4 V HIGH. At the light CMOS loads used here,
its 5 V outputs satisfy the Z80 clock's strict $V_{IHC}$ threshold, the
MCP23S17's $0.8V_{DD}$ SPI threshold, and the SRAM's CMOS control-input
threshold. Its current TI datasheet specifies a worst-case 9.5 ns
A-to-Y delay at 5 V with a 50 pF load over -40°C to 85°C. Combined
with the ATF22V10C-15's 15 ns combinational delay and the SRAM's 55 ns
chip-enable access, these datasheet maxima give a conservative 79.5 ns
component-delay sum for the control-to-data path. This is not complete
timing closure: breadboard interconnect and Z80 setup allowance are
additional. This is why 1-6 MHz is measured rather than assumed, and
why this design is not a
20 MHz system despite using a 20 MHz-rated CPU.

| Channel | AHCT244 input | AHCT244 output | Function |
|----:|----|----|----|
| 1A1 | Pin 2 from Pico GP2 | 1Y1 pin 18 to Z80 CLK pin 6 | Master clock |
| 1A2 | Pin 4 from Pico GP4 | 1Y2 pin 16 to Z80 BUSREQ# pin 25 | DMA request |
| 1A3 | Pin 6 from Pico GP21 | 1Y3 pin 14 to MCP23S17 CS# pin 11 | SPI chip select |
| 1A4 | Pin 8 from Pico GP18 | 1Y4 pin 12 to MCP23S17 SCK pin 12 | SPI clock |
| 2A1 | Pin 11 from Pico GP19 | 2Y1 pin 9 to MCP23S17 SI pin 13 | SPI MOSI |
| 2A2 | Pin 13 from ATF22V10 pin 14 | 2Y2 pin 7 to SRAM WE# pin 29 | Arbitrated write enable |
| 2A3 | Pin 15 from ATF22V10 pin 15 | 2Y3 pin 5 to SRAM OE# pin 24 | Arbitrated output enable |
| 2A4 | Pin 17 from ATF22V10 pin 16 | 2Y4 pin 3 to SRAM CE# pin 22 | Arbitrated chip enable |

Tie both active-low output enables, OE1# pin 1 and OE2# pin 19, to GND.
Connect VCC pin 20 to regulated +5 V and GND pin 10 to common ground.
The buffer is permanently enabled; the Pico-side defaults and GAL
equations therefore establish safe inactive outputs before firmware
enables its GPIO drivers.

Pico GP3 RESET# bypasses the AHCT244 and connects directly to Z80 pin 26
and ATF22V10 pin 1. Its 3.3 V HIGH exceeds the Z84C00's 2.2 V and the
ATF22V10's 2.0 V input-HIGH minima. Keep its 10 kOhm pull-down to GND
and never fit a 5 V pull-up on this node.
