# Test the flashed ATtiny85

[Stage 11 overview and build setup](../README.md) |
[Flashing board guide](../flash_ATTiny85/README.md)

This board is **only for testing the flashed ATtiny85**. Fit a dedicated
**Pimoroni Pico Plus 2 W**, an ATtiny85 DIP-8 socket, I2C and RESET pull-ups, a bypass capacitor,
two LEDs and a passive piezo. **Do not connect ISP wires to this board.**
The flashing hardware stays on the separate flashing board.

The [Pico test program](main.c) sends I2C commands to the ATtiny85; it has
no ISP operations and cannot flash the target. The VS Code task selects
the SDK board **`pimoroni_pico_plus2_w_rp2350`**, isolated from the Z80 board.
Its 16 MB flash, PSRAM and wireless hardware do not change this test's
header wiring; the test does not use PSRAM or wireless.

## Power and chip orientation

Power this board from its Pico USB connection only. Use **3.3 V**
from Pico 3V3(OUT), physical pin 36, for ATtiny85 VCC and all pull-ups.
Never connect a 5 V target output or 5 V I2C pull-up directly to the Pico.

Fit a **100 nF ceramic capacitor** between ATtiny85 pins 8 and 4,
close to the socket. Use Pico **physical pin 18 (GND)** for the ground
rail, ATtiny85 pin 4, and LED/piezo ground returns. Physical pins 3, 8
and 13 are alternative Pico grounds on the same pin 1-20 side.

ATtiny85 DIP-8, viewed from above with notch/dot at the top:

| ATtiny85 pin | Signal | Testing-board connection |
| --- | --- | --- |
| 1 | PB5 / RESET | 10 kohm pull-up; no Pico RESET wire |
| 2 | PB3 | Red LED through 1 kohm resistor |
| 3 | PB4 | Blue LED through 1 kohm resistor |
| 4 | GND | Ground rail |
| 5 | PB0 / SDA | Pico GP20 and SDA pull-up |
| 6 | PB1 / OC1A | Passive piezo (red +) through 220-330 ohm resistor |
| 7 | PB2 / SCL | Pico GP21 and SCL pull-up |
| 8 | VCC | 3.3 V rail |

## Pico pins and I2C wiring

Connect each pair directly with a wire.

| Pico signal | Pico physical pin | ATtiny85 DIP pin |
| --- | --- | --- |
| GP20 / I2C0 SDA | 26 | 5 / PB0 |
| GP21 / I2C0 SCL | 27 | 7 / PB2 |
| GND | 18 | 4 / GND |
| 3V3(OUT) | 36 | 8 / VCC |

Viewed from above with USB at the top, physical pins 1-20 run down the
left-hand header. Physical pin 40 is at the top right; numbering decreases
down the right-hand header. Physical pin numbers are not GPIO numbers.

Leave 3V3_EN (physical pin 37) unconnected; grounding it disables the
3.3 V regulator. VSYS (39) is unused and VBUS (40) is USB 5 V, not the
target's 3.3 V rail. Pico GP16-GP19 are unused on this board.

The Pimoroni Pico Plus 2 W retains these standard Pico header assignments:
GP20 is physical pin 26, GP21 is pin 27, GND is pin 18 and 3V3(OUT) is pin 36.
See the [Pimoroni product page](https://shop.pimoroni.com/products/pimoroni-pico-plus-2-w)
and [official pinout](https://cdn.shopify.com/s/files/1/0174/1800/files/ppico_plus_2_w_pinout_diagram.pdf?v=1727346378).
Use the GP20/GP21 header pins, not the board's Qw/ST connector.

## Pull-up resistors

Connect one resistor from each signal below to **3.3 V**
(Pico 3V3(OUT), physical pin 36), not 5 V.

| Signal | ATtiny85 DIP pin | Pull-up resistor | Purpose |
| --- | --- | --- | --- |
| RESET | 1 / PB5 | 10 kohm to 3.3 V | Keeps the ATtiny85 running |
| SDA | 5 / PB0 | 4.7 kohm to 3.3 V | I2C data pull-up |
| SCL | 7 / PB2 | 4.7 kohm to 3.3 V | I2C clock pull-up |

Do not fit a large capacitor on RESET.

## LEDs and piezo

| ATtiny85 signal | Connection |
| --- | --- |
| Pin 2 / PB3 | 1 kohm resistor to red LED anode; LED cathode to GND |
| Pin 3 / PB4 | 1 kohm resistor to blue LED anode; LED cathode to GND |
| Pin 6 / PB1 | 220-330 ohm resistor to passive piezo red (+) lead; black (-) lead to GND |

### Recommended piezo: AB3342

The tested part is the AB3342 30 mm **passive** piezo transducer
(no internal oscillator). Datasheet values:

| Parameter | AB3342 |
| --- | --- |
| Rated frequency | 4000 Hz |
| Sound pressure | 85 dB minimum at 10 cm, 3 Vp-p sine at 4 kHz |
| Operating voltage | 1-30 Vp-p (rated 3 Vp-p) |
| Rated current | 1 mA maximum |
| Capacitance | 25 nF +/-30% at 120 Hz |
| Leads | Red +, black - |

The 0-3.3 V PB1 square wave is 3.3 Vp-p, which is within the operating range.
It is loudest near **4 kHz**. At 1 kHz and below, the same drive is much quieter.

The piezo is a capacitive load. The series resistor limits the
current spike on each edge: 3.3 V / 220 ohm is about 15 mA, within the
ATtiny85's 40 mA absolute maximum. With 25 nF, 220-330 ohm gives an
RC time constant of 5.5-8.3 us, well inside the 125 us half-period at 4 kHz.
A 1 kohm resistor (25 us) noticeably rounds each edge and reduces loudness.
Do not omit the resistor.

The LEDs, piezo and all pull-ups **remain wired on this board**.
Only the ATtiny85 moves between boards, with power off.

Do not drive an 8 ohm speaker or magnetic buzzer directly from PB1.
Such loads need a correctly rated transistor/amplifier and, for inductive
loads, appropriate flyback protection. Use a passive piezo for this test;
an active buzzer cannot reproduce arbitrary tone frequencies.

To check which you have, connect it directly across 3.3 V and GND. A
passive piezo only clicks; an active buzzer sounds its own fixed tone.
Driven from PB1, an active buzzer plays that fixed tone underneath every
commanded frequency, and the sweep cannot find a loud spot.

## Test procedure

1. Confirm that the [flashing board](../flash_ATTiny85/README.md)
   reported **SUCCESS: Flash verified.**
2. **Disconnect USB and any other power from both boards.** Remove
   the ATtiny85 from the flashing socket and insert it into the testing
   socket with the correct notch/pin-1 orientation.
3. Leave the flashing board off and disconnect other Picos. Connect the
   **testing Pico's USB-C** cable normally.
4. In VS Code, run **Tasks: Run Task** ->
   **Build and run Pico - Stage 11 ATtiny85 test**. Confirm the setup
   to allow the task to discover the testing Pico automatically. There
   is no serial-number or SDK-location prompt.
   Do not select the flashing Pico or Z80 supervisor.
5. The task builds and flashes the Pico I2C test and opens its USB
   console. The safety confirmation authorizes interrupting its running
   firmware; SDK USB firmware is rebooted into BOOTSEL automatically.
   The repeating LED/tone test starts automatically whenever the Pico
   powers up or resets; a USB console connection is not required.
6. Observe the sequence below. Exit picocom with **Ctrl-A, then Ctrl-X**;
   disconnect power before moving the ATtiny85 or changing any wiring.
   Closing the console does not power off the board or stop the test.

If the Pico is blank or lacks the SDK USB reset interface, hold the
Pimoroni **BOOT** button while connecting USB and rerun the task.
An already-BOOTSEL device is supported too.

For manual loading, use the
[shared build instructions](../README.md#build), copy
`build/stage11/pimoroni_pico_plus2_w_rp2350/test_ATTiny85/stage11_test_attiny85.uf2` onto the
testing Pico's BOOTSEL mass-storage drive, and open its USB CDC serial
monitor. 115200 baud is conventional; USB ignores baud.

## Expected test sequence

Each cycle starts with a **tone-off LED check**:

1. Red only, fully on for one second.
2. Blue only, fully on for one second.
3. Both fully on for one second; neither should flicker.
4. Both fade up and down together, with identical brightness commands.

The test then sweeps the piezo from **1000 to 5000 Hz in 250 Hz steps**
(400 ms tone, 600 ms per step, LEDs off). The console prints each
frequency as it plays; note the loudest one. Passive piezos are usually
loudest near their resonance; for the AB3342, this should be about 4000 Hz.

Next it plays 1 kHz for 1500 ms while cross-fading the LEDs
(red fades up as blue fades down), leaves the tone off, plays a short
440 Hz tone, and tests explicit stop of an 880 Hz tone. The sequence repeats.

I2C failures (including timeouts and short writes) print an error, abandon
the current cycle, and retry from the tone-off LED check after one second.
Retries continue while the target is unavailable, and the full sequence
resumes once commands succeed; no Pico reset is required. Disconnect power
before fixing wiring or moving the ATtiny85. The console is optional and
can be opened later to view subsequent status messages.

ACKs prove command delivery, **not** actual light/sound output: check the
LEDs, tone frequency and automatic stop on the bench.

### If one LED channel flickers

Keep the LED current-limiting resistors fitted. If the same LED and resistor
work on PB3 (DIP pin 2) but not PB4 (DIP pin 3), the symptom follows the
output path rather than the LED colour.

Use the console messages to distinguish the tone-off check from the
tone-playing cross-fade. If PB4 flickers during its **fully on** check,
this is not an expected PWM effect: brightness 255 is continuously on.
With power disconnected, check the pin-3 jumper, socket/breadboard contact,
resistor connections and ground return. A fault that appears only while
tones play needs a separate timer/output or supply-noise investigation;
the visual result alone does not identify its cause.

This diagnostic changes only the Pico test program. Rerun the test task;
there is no need to reflash or move the ATtiny85 just to run these checks.

## Command protocol and timers

7-bit I2C address **0x42**, 100 kHz, write-only, clock stretching supported.
Send **one complete command per write**, followed by STOP; no register
pointer or read-back protocol is implemented.

| Command | Bytes, in order | Meaning |
| --- | --- | --- |
| `01` | `01 freq_hi freq_lo ms_hi ms_lo` | Big-endian 16-bit Hz and duration in ms |
| `02` | `02 brightness` | Red brightness, 0 off through 255 fully on |
| `03` | `03 brightness` | Blue brightness, 0 off through 255 fully on |

For example `01 03 E8 05 DC` plays 1000 Hz for 1500 ms.
Frequency or duration zero stops the tone. A new tone replaces the current
one; the 16-bit duration supports up to 65535 ms.

The target uses avr-libc directly, not Arduino or an `analogWrite` dependency.
Timer1 produces the PB1 waveform in hardware, using non-inverted PWM
without enabling its complementary PB0 output (SDA must remain free).
The special 1 Hz case uses hardware compare-toggle because Timer1 PWM
cannot reach 1 Hz at 8 MHz. Frequencies are rounded to timer divisors.

Timer0 supplies 32 us ticks for duration expiry and independent 8-bit
software PWM on PB3/PB4 (nominally 122 Hz). PB3/PB4 cannot provide
independently timed hardware brightness PWM while Timer1 owns the tone.
Timer/RC accuracy and I2C interrupt latency affect the physical waveform;
builds and host tests do not establish measured timing accuracy.

USI START/overflow handlers receive all bytes and perform ACK/NACK, with a
three-command queue for foreground execution. Incomplete packets are
discarded at the next START. Unknown commands, read addresses and a full
queue are NACKed. Once the expected final byte is accepted, the command is
queued; extra bytes are NACKed and do not roll it back. A stalled I2C
master does not busy-wait in an ISR, so the duration timer continues.
There are no blocking tone delays.
