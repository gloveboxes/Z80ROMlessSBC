# Flash the ATtiny85

[Stage 11 overview and build setup](../README.md) |
[Testing board guide](../test_ATTiny85/README.md)

This board is **only an ISP programmer**. Fit a dedicated Pico, an
ATtiny85 DIP-8 socket (preferably ZIF), a RESET pull-up and a bypass
capacitor. **Do not fit LEDs, a piezo or I2C connections to this board.**
The testing hardware stays on the separate testing board.

The [Pico programmer](main.c) flashes the compiled
[ATtiny85 firmware](target/main.c) and verifies all 8192 flash bytes.
The default Pico board and VS Code task target Pico 2 W, isolated from
the Z80 board.

## Power and chip orientation

Power this board from its Pico USB connection only. Use **3.3 V**
from Pico 3V3(OUT), physical pin 36, for ATtiny85 VCC. Never connect
USB VBUS or 5 V to the target supply.

Fit a **100 nF ceramic capacitor** between ATtiny85 pins 8 and 4,
close to the socket. Connect Pico **physical pin 18 (GND)** to the
board's ground rail and ATtiny85 pin 4. Physical pins 3, 8 and 13 are
alternative Pico grounds on the same pin 1-20 side.

ATtiny85 DIP-8, viewed from above with notch/dot at the top:

| ATtiny85 pin | Signal | Flashing-board connection |
| --- | --- | --- |
| 1 | PB5 / RESET | Pico GP17 and 10 kohm pull-up |
| 2 | PB3 | Leave unconnected |
| 3 | PB4 | Leave unconnected |
| 4 | GND | Ground rail |
| 5 | PB0 / MOSI | Pico GP19 |
| 6 | PB1 / MISO | Pico GP16 |
| 7 | PB2 / SCK | Pico GP18 |
| 8 | VCC | 3.3 V rail |

## Pico pins and ISP wiring

Connect each pair directly with a wire.

| Pico signal | Pico physical pin | ATtiny85 DIP pin |
| --- | --- | --- |
| GP16 / SPI0 RX (MISO) | 21 | 6 / PB1 |
| GP17 / RESET output | 22 | 1 / RESET |
| GP18 / SPI0 SCK | 24 | 7 / PB2 |
| GP19 / SPI0 TX (MOSI) | 25 | 5 / PB0 |
| GND | 18 | 4 / GND |
| 3V3(OUT) | 36 | 8 / VCC |

Viewed from above with USB at the top, physical pins 1-20 run down the
left-hand header. Physical pin 40 is at the top right; numbering decreases
down the right-hand header. **Physical pin 18 is GND, not GP18**; GP18
is physical pin 24.

Leave 3V3_EN (physical pin 37) unconnected; grounding it disables the
3.3 V regulator. VSYS (39) is unused and VBUS (40) is USB 5 V, not the
target's 3.3 V rail. GP20 and GP21 are unused on this board.

These header assignments apply to Pico, Pico W, Pico 2 and Pico 2 W.
See the [official Pico 2 W datasheet](https://pip.raspberrypi.com/documents/RP-008304-DS-1-pico-2-w-datasheet.pdf)
for the board pinout.

## Pull-up resistor

| Signal | ATtiny85 DIP pin | Pull-up resistor | Purpose |
| --- | --- | --- | --- |
| RESET | 1 / PB5 | 10 kohm to 3.3 V | Keeps RESET high when the programmer is not driving it low |

Connect the resistor to Pico 3V3(OUT), physical pin 36, not 5 V.
Do not fit a large capacitor on RESET. No SDA/SCL pull-ups are needed on
this programming-only board.

## Target configuration

Use an ATtiny85 configured for its internal 8 MHz RC oscillator.
Factory-default low fuse `62` is suitable: the firmware removes the
divide-by-eight clock prescaler at startup. `CKSEL=0010`, RESET and SPI
must be enabled; debugWIRE, CKOUT and the always-on watchdog fuse must be
disabled. CKOUT would take over the blue LED pin, and WDTON would force
periodic resets. The programmer checks the signature and these fuse
requirements **before erasing**; it never writes fuses. An external-clock
target without a clock, or one with RESET or SPI disabled, needs separate
recovery equipment.

## Flash procedure

1. **Power off both boards.** Insert the ATtiny85 into the flashing
   board's socket with the correct notch/pin-1 orientation. Leave the
   testing board off.
2. Connect only the **flashing Pico's** USB cable normally. Leave the
   testing board off and disconnect other Picos.
3. In VS Code, run **Tasks: Run Task** ->
   **Build and run Pico - Stage 11 ATtiny85 ISP**. Confirm the setup
   to allow the task to discover the flashing Pico automatically. There
   is no serial-number or SDK-location prompt.
   The task builds the target firmware and programmer, flashes the Pico,
   and opens its USB console. The safety confirmation authorizes
   interrupting its running firmware; SDK USB firmware is rebooted into
   BOOTSEL automatically.
4. Type uppercase **P**. This explicitly authorizes one erase/program
   cycle. The programmer holds the ATtiny85 in reset until you do so.
5. Wait for **SUCCESS: Flash verified.** If programming fails, fix the
   fault and reset the programmer before retrying; do not move an
   incompletely programmed target to the testing board.
6. Exit picocom with **Ctrl-A, then Ctrl-X**. Disconnect USB and any
   other power from **both boards**.
7. Remove the ATtiny85 and transfer it to the
   [testing board](../test_ATTiny85/README.md), checking notch/pin-1
   orientation again. Leave the flashing board's wiring intact.

If the Pico is blank or lacks the SDK USB reset interface, hold BOOTSEL
while connecting USB and rerun the task. An already-BOOTSEL device is
supported too.

For manual loading, use the
[shared build instructions](../README.md#build), copy
`build/stage11/pico2_w/flash_ATTiny85/stage11_flash_attiny85.uf2` onto the
flashing Pico's BOOTSEL mass-storage drive, and open its USB CDC serial
monitor. 115200 baud is conventional; USB ignores baud. Then follow
steps 4-7 above.

## What the programmer does

The program enters ISP at 10 kHz, checks the `AC 53 00 00` response
(third received byte must be `53`), checks signature `1E 93 0B` and
fuses, issues `AC 80 00 00`, and polls busy with a bounded timeout.
It programs the embedded image, verifies the entire flash (including
erased padding), makes MOSI/SCK/MISO inputs, and releases RESET high.

A failure is reported explicitly and RESET is also released. After an
erase/write failure the target firmware may be incomplete. The programmer
does not automatically retry an erase, and does not program again until
the Pico is reset and `P` is entered again.

**Chip erase destroys existing flash and clears lock bits. It also erases
EEPROM unless the EESAVE fuse is programmed. No backup is made.**
