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
4. Both fade up and down together autonomously, with one command per fade.

An arcade showcase follows: **coin pickup, jump, ricochet, teleport,
shield recharge, extra life, victory fanfare and game over**. Each effect
is one autonomous sequence, sweep or synchronized cue, with a 200 ms pause after
the sound completes. Jump flashes blue; shield recharge fades blue up.
Next is a 2.25-second **do, re, mi, fa, sol, la, ti, do**
major scale (C6-C7), holding the final note twice as long.
Original maze-chase effects follow: **pellet munching, power pellet and
enemy capture**. Brick-breaker effects follow: **paddle bounce,
wall bounce, brick shatter and ball lost**, with a fading red flash on loss.
The final set covers **accelerating invader footsteps, UFO flyby, robot
chatter, engine acceleration and warble, accelerating bomb countdown,
explosion/player death, door unlocking, sonar ping, low-health heartbeat,
combo multiplier, treasure discovery and level complete**. Engine and door
effects each use two stages. The explosion is a jagged tonal descent into
a low buzz, not noise; the heartbeat may be quiet on the passive piezo.
The combo demonstrates a rising hit chain rather than reacting to game input.
Six primitive demonstrations finish the showcase: **smooth segmented engine,
repeating segmented ricochet, twelve accelerating countdown pulses, eight
slowing radar pulses, rising UFO with vibrato and descending engine with
vibrato**.
These are genre-inspired effects, not reproductions of game recordings.
The console names each effect. Program the ATtiny85 with the compiled target
firmware before running the testing Pico; the showcase exercises all target
commands.

The effects demonstration then runs:

1. Descending laser and ascending power-up pitch sweeps.
2. A finite siren with alternating red/blue LEDs; both stop automatically.
3. A jingle with a silent rest, repeated twice, while both LEDs breathe.
4. Three red blinks while blue independently fades to off.
5. One synchronized cue: rising pitch, red fade-up and blue fade-down.
6. Indefinite siren/breathing, then sound-only and red-only cancellation.
7. Replacement of blue breathing with indefinite blinking, then fixed brightness.
8. Indefinite jingle replaced by a timed tone, followed by explicit tone stop.
9. Indefinite alternating LEDs, LED-only stop, then siren/blinking and all-stop.

The game-side equivalent only sends these effect commands; it does not
stream brightness or frequency updates. Console messages identify each
observation. The cycle repeats after the all-stop pause.

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
sound-and-light effects. If PB4 flickers during its **fully on** check,
this is not an expected PWM effect: brightness 255 is continuously on.
With power disconnected, check the pin-3 jumper, socket/breadboard contact,
resistor connections and ground return. A fault that appears only while
tones play needs a separate timer/output or supply-noise investigation;
the visual result alone does not identify its cause.

## Command protocol and timers

7-bit I2C address **0x42**, 100 kHz, write-only, clock stretching supported.
Send **one complete command per write**, followed by STOP; no register
pointer or read-back protocol is implemented.

All named frequency, time and repeat fields below are **unsigned big-endian
16-bit** values. Times are milliseconds; brightness, mask, idle and count
are single bytes. Brightness ranges from 0 (off) to 255 (fully on).
LED masks are red `01`, blue `02`, or both `03`. Stop masks also accept
sound `04`; OR channels together, or use `07` for all.

| Command | Bytes, in order | Meaning |
| --- | --- | --- |
| `01` | `01 frequency duration` | Timed tone; either field zero stops sound |
| `02` | `02 brightness` | Fixed red brightness |
| `03` | `03 brightness` | Fixed blue brightness |
| `04` | `04 start_Hz end_Hz duration` | Linear pitch sweep, then silence |
| `05` | `05 low_Hz high_Hz rise fall total` | Repeating linear siren; `total=0` runs indefinitely |
| `06` | `06 count repeats (frequency duration)...` | 1-8 notes/rests; repeats counts complete sequences, `0` runs indefinitely |
| `07` | `07 mask start end duration` | Linear LED fade; holds end brightness |
| `08` | `08 mask brightness on off repeats` | LED blink; repeats counts complete on/off cycles, `0` runs indefinitely |
| `09` | `09 mask minimum maximum rise fall total idle` | Linear breathing; `total=0` runs indefinitely; finishes at idle |
| `0A` | `0A brightness red_time blue_time repeats` | Alternating LEDs; repeats counts complete red/blue cycles, `0` runs indefinitely |
| `0B` | `0B mask` | Cancel selected channels: silence/off |
| `0C` | `0C start_Hz end_Hz duration red_start red_end blue_start blue_end` | Start a pitch sweep and two LED fades on the same timeline |
| `0D` | `0D count repeats start_Hz (target_Hz duration)...` | 1-7 joined linear sweeps; repeats counts complete envelopes, `0` runs indefinitely |
| `0E` | `0E count frequency on start_gap end_gap` | 1-64 pulses with linearly changing silent gaps; finishes after the final beep |
| `0F` | `0F start_Hz end_Hz duration depth_Hz period` | Linear sweep plus triangular vibrato; finishes silent |

For example `01 03 E8 05 DC` plays 1000 Hz for 1500 ms.
For example `04 0F A0 03 E8 00 B4` sweeps 4000 to 1000 Hz over 180 ms.
Each time field supports up to 65535 ms. Siren/breathing total duration is
independent of cycle length and may end partway through a cycle.
Sequence frequency zero means a silent rest; note durations must be nonzero.
Sweep/cue/siren frequencies and all cycle-leg/fade durations must be nonzero.
Breathing minimum must not exceed maximum. Masks must select valid channels.
Invalid parameters are NACKed without replacing an active effect.

Segment targets, starting frequency and durations must be nonzero.
Each segment begins at the preceding target; repetitions restart at
`start_Hz`, so choose a matching final target for a continuous loop.
The seven durations can total up to 458745 ms per envelope.

Pulse frequency and on-time must be nonzero. Gaps may be zero: with three
or more pulses they interpolate from the first to the last inter-pulse
gap, rounding the change toward the starting gap. One pulse has no gap;
two pulses use only `start_gap`. There is no trailing gap.

Vibrato period is a full cycle in milliseconds (minimum 2); depth is the
maximum offset in Hz, not volume. Its triangle starts at `-depth`, reaches
`+depth` after half a period, and returns to `-depth`. The sweep duration
must be nonzero; both endpoints minus/plus depth must stay within
1-65535 Hz. Depth zero gives an unmodulated sweep. All three primitives
replace only the sound channel and can run alongside LED effects.

One sound and two independent LED effects run concurrently. A command
replaces only its selected channel(s); fixed brightness cancels that LED's
effect, and a timed tone replaces the sound effect. Blink/alternate finish
off; breathing uses its specified idle brightness. A cue finishes silent
and holds both LED endpoints. Stop cancels immediately when executed;
there is no priority system, command scripting or volume envelope.
Repeating effects continue until their finite limit, replacement, stop or
power-off; I2C disconnection does not cancel them.

The target uses avr-libc directly, not Arduino or an `analogWrite` dependency.
Timer1 produces the PB1 waveform in hardware, using non-inverted PWM
without enabling its complementary PB0 output (SDA must remain free).
The special 1 Hz case uses hardware compare-toggle because Timer1 PWM
cannot reach 1 Hz at 8 MHz. Frequencies are rounded to timer divisors.

Timer0 supplies 32 us ticks, a millisecond timebase and independent 8-bit
software PWM on PB3/PB4 (nominally 122 Hz). Integer interpolation, repeat
tracking and Timer1 frequency changes run in the foreground, not the PWM
ISR. Frequency updates retain the running timer when possible instead of
restarting every millisecond. PB3/PB4 cannot provide
independently timed hardware brightness PWM while Timer1 owns the tone.
Timer/RC accuracy and I2C interrupt latency affect the physical waveform;
builds and host tests do not establish measured timing accuracy.

USI START/overflow handlers receive all bytes and perform ACK/NACK, with a
three-command queue for foreground execution, with packets up to 36 bytes.
Incomplete packets are discarded at the next START. Unknown commands,
invalid parameters, read addresses and a full queue are NACKed.
Once the expected final byte is accepted, the command is
queued; extra bytes are NACKed and do not roll it back. A stalled I2C
master does not busy-wait in an ISR, so PWM and foreground effects continue.
Effect transitions occur on foreground service passes using elapsed
milliseconds; they are not sample-accurate audio. There are no blocking
effect delays or heap allocations.
