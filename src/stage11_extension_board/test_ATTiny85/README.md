# Test the ATtiny85 sound engine

[Stage 11 overview and builds](../README.md) |
[Flashing board guide](../flash_ATTiny85/README.md)

This standalone board uses a **Pimoroni Pico Plus 2 W**, socketed ATtiny85,
passive piezo, bypass capacitor and I2C/RESET pull-ups. **No ISP wires.**
The [Pico test](main.c) sends sound commands and reads status; it cannot
program the ATtiny85. Neither board is the Z80 supervisor.

## Wiring and power

Power from this Pico's USB only. Use **3.3 V**, never USB VBUS or 5 V,
for the target and pull-ups. Connect all grounds together on this board.
Fit 100 nF between ATtiny85 pins 8 and 4, close to the socket.
ATtiny85 DIP-8 is viewed from above, notch/dot at the top.

| ATtiny85 pin | Signal | Connection |
| --- | --- | --- |
| 1 | PB5 / RESET | 10 kohm to 3.3 V; no Pico RESET wire or large capacitor |
| 2 | PB3 | Unconnected input |
| 3 | PB4 | Unconnected input |
| 4 | GND | Ground rail; Pico physical pin 18 |
| 5 | PB0 / SDA | Pico GP20, physical pin 26; 4.7 kohm to 3.3 V |
| 6 | PB1 / OC1A | 220-330 ohm to passive piezo red (+); black (-) to GND |
| 7 | PB2 / SCL | Pico GP21, physical pin 27; 4.7 kohm to 3.3 V |
| 8 | VCC | Pico 3V3(OUT), physical pin 36 |

These are direct 3.3 V connections. On the Pico, physical pins 1-20 run
down the left header with USB at the top; physical pin numbers are not GPIO
numbers. Leave 3V3_EN (37), VSYS (39) and VBUS (40) unconnected. GP16-GP19
are unused. Use the GP20/GP21 header, not Qw/ST.
See the [Pimoroni pinout](https://cdn.shopify.com/s/files/1/0174/1800/files/ppico_plus_2_w_pinout_diagram.pdf?v=1727346378).

Keep the flashing board off and separate. Do not join the boards' supply
rails. Power off both boards before moving the chip or changing wiring.
The piezo and pull-ups stay on this board.

### Passive piezo

The tested AB3342 is a 30 mm passive piezo, rated at 4 kHz, with 25 nF
capacitance (+/-30%), 1-30 Vp-p operating voltage, and 85 dB minimum at
10 cm with a 3 Vp-p sine at 4 kHz. The target's 0-3.3 V square wave is
within that voltage range. It is loudest near 4 kHz; low engine/heartbeat
notes may be quiet.

Keep the 220-330 ohm series resistor: it limits capacitive edge current
(about 15 mA initially at 220 ohm) and gives a nominal 5.5-8.3 us RC time.
Do not drive an 8 ohm speaker or magnetic buzzer directly. Such loads need
a rated driver/amplifier and appropriate inductive-load protection.
An active buzzer generates its own pitch and is unsuitable for these effects.

## Running the test

1. Build and program the ATtiny85 using the [flashing guide](../flash_ATTiny85/README.md).
   Wait for **SUCCESS: Flash verified.**
2. Disconnect USB and all other power from both boards. Move the ATtiny85
   into the testing socket, checking pin 1 and notch orientation.
3. Connect only the testing Pico by USB. Run **Build and run Pico - Stage 11
   ATtiny85 test** and confirm the isolated bench setup. The task selects
   board `pimoroni_pico_plus2_w_rp2350`, discovers the Pico, builds, flashes
   and opens its USB console. This authorizes interruption of that Pico,
   not the Z80 supervisor.
4. The repeating test starts at power-up without waiting for a console.
   Exit picocom with **Ctrl-A, then Ctrl-X**; closing the console does not
   stop playback or power off the board.

For blank firmware, hold Pimoroni **BOOT** while connecting USB, then run
the task. For manual BOOTSEL copying, use the [build instructions](../README.md#build)
and `build/stage11/pimoroni_pico_plus2_w_rp2350/test_ATTiny85/stage11_test_attiny85.uf2`.
USB CDC conventionally uses 115200 baud; USB ignores baud.

### Demonstration and feedback

Each cycle stops/clears playback, then exercises:

- Tone, sweep, siren, notes/rests, joined pitch ramps, accelerating/slowing
  pulses, vibrato, major/minor arpeggios and gated tones.
- A manually supplied eight-note do-re-mi major scale using the note-sequence primitive.
- Every flash preset, then pitch/tempo scaling.
- High-priority interruption, low-priority rejection and equal-priority replacement.
- Two FIFO entries, queue-full rejection, and three measured completions.
- Live pitch/speed changes on a running engine; a mismatched request-ID rejection.
- Queue cancellation, phase-preserving vibrato depth/period changes and stop.

Completion waits use **status reads and events**, not estimated effect
durations. Console output prints event type, request ID and event sequence.
I2C timeout/short transfer, protocol mismatch, unexpected result, event overflow
or bounded-wait expiry prints an error. The cycle retries from stop after
one second. Overflow recovery drains/acknowledges retained events before
reporting failure, allowing the next cycle to recover.

Status reports firmware state, not measured sound pressure or frequency.
Check the audible results separately. The explosion is tonal, not noise.

## Command protocol and timers

The canonical definitions are [protocol.h](../flash_ATTiny85/target/protocol.h).
Use **7-bit address 0x42, 100 kHz**, with a master that supports clock
stretching. Send one complete command per write, followed by STOP.
Read status directly: no register-pointer write is needed. A repeated START
also begins a new address phase. Stage 10 OLED/Z80 integration is separate
from this standalone test.

All multibyte fields are **unsigned big-endian**. Times are milliseconds
of effect time. IDs, priorities, flags, counts, percentages, kinds, pattern
and preset IDs are single bytes. Request IDs are 1-255; do not reuse an ID
while its sound is active or queued, or its terminal event is unacknowledged.

| Command | Packet |
| --- | --- |
| `10` PLAY | `10 id priority flags pitch_percent speed_percent effect...` |
| `11` STOP | `11 id` |
| `12` UPDATE | `12 id target_id mask pitch_percent speed_percent depth_Hz period` |
| `13` CLEAR_QUEUE | `13 id` |
| `14` ACK_EVENT | `14 event_sequence` |

Pitch/speed percentages range from **25 to 200**, with 100 normal.
Pitch scaling preserves rests and saturates nominal output to 1-65535 Hz.
Speed scales the effect timeline, including gaps and vibrato. It does not
alter pitch. Priority is 0-255, higher wins.

PLAY flag `01` explicitly enqueues when a sound is active; otherwise it starts
immediately. The FIFO holds **two complete sounds** and runs in insertion
order regardless of priority. Without enqueue, higher priority interrupts,
equal replaces, and lower rejects. Interrupted sounds are discarded.
Queued sounds remain pending after an interruption. STOP always cancels the
active sound and entire playback FIFO; CLEAR_QUEUE leaves the active sound
running. STOP does not clear unread events.

UPDATE mask bits are pitch `01`, speed `02`, modulation `04`. Only selected
fields are applied; unselected fields may be zero. `target_id` must match
the active sound. Pitch and speed preserve effect position; modulation is
valid only for vibrato and preserves fractional modulation-cycle phase.
Depth must keep both base sweep endpoints within 1-65535 Hz. A rejected
update changes nothing.

### Effect payloads

| Kind | Payload | Meaning |
| --- | --- | --- |
| `01` | `01 frequency duration` | Timed tone |
| `02` | `02 start_Hz end_Hz duration` | Linear sweep |
| `03` | `03 low_Hz high_Hz rise fall total` | Repeating linear siren |
| `04` | `04 count repeats (frequency duration)...` | 1-8 notes/rests |
| `05` | `05 count repeats start_Hz (target_Hz duration)...` | 1-7 joined linear sweeps |
| `06` | `06 count frequency on start_gap end_gap` | 1-64 pulses, changing gaps |
| `07` | `07 start_Hz end_Hz duration depth_Hz period` | Sweep plus triangular vibrato |
| `08` | `08 root_Hz step_time total pattern` | Four-note arpeggio: major `00`, minor `01` |
| `09` | `09 frequency on off total` | Repeating gated tone |
| `0A` | `0A preset_id` | Flash-resident effect |

Each finite effect finishes silent. All note/segment/leg/step/on/off durations
are nonzero; pulse gaps may be zero. Frequencies are nonzero except note
sequence frequency zero, which is a rest. Each time field is at most 65535.
Siren/gate/arpeggio total zero runs indefinitely; sequence/segment repeat
zero repeats indefinitely. Timed tone/sweep/vibrato duration zero is invalid.

Segments start at the preceding endpoint; repeat cycles restart at the
specified starting frequency. Use a matching final endpoint for a continuous
loop. Seven segments may total 458745 ms per cycle.
Pulse gaps interpolate linearly from first to last inter-pulse gap, rounding
the change toward the starting gap. One pulse has no gap; two use only
`start_gap`. There is no final trailing gap.

Vibrato period is one full cycle, minimum 2 ms. It starts at `-depth`, reaches
`+depth` at half-period and returns. Depth zero gives an unmodulated sweep.
Base endpoints plus/minus depth must stay in 1-65535 Hz.
Arpeggio root is 1-32767 Hz; intervals are approximate root, third, fifth,
octave. It is one rapidly changing voice, not simultaneous notes.
Gate on/off timing provides tremolo-like articulation, not a volume envelope.

Example: `10 01 05 00 64 64 0A 01` plays the coin preset as request 1,
priority 5, normal pitch/speed. It is an eight-byte game-side event.

### Flash presets

The 63 definitions live in [presets.h](../flash_ATTiny85/target/presets.h), in AVR
program memory, not EEPROM or SRAM.

| ID | Effect | ID | Effect |
| --- | --- | --- | --- |
| `01` | Coin | `18` | Invader footsteps |
| `02` | Laser | `19` | UFO flyby |
| `03` | Tonal explosion | `1A` | Accelerating countdown |
| `04` | Engine, indefinite | `1B` | Door unlock |
| `05` | Alarm | `1C` | Sonar ping |
| `06` | Heartbeat, indefinite | `1D` | Combo multiplier |
| `07` | Treasure arpeggio | `1E` | Player hit |
| `08` | Level complete | `1F` | Player death |
| `09` | Pellet munch | `20` | Checkpoint |
| `0A` | Ricochet | `21` | Item pickup |
| `0B` | Robot chatter | `22` | Menu move |
| `0C` | Jump | `23` | Menu confirm |
| `0D` | Teleport | `24` | Menu cancel |
| `0E` | Shield recharge | `25` | Pause |
| `0F` | Extra life | `26` | Resume |
| `10` | Victory fanfare | `27` | Low health warning, indefinite |
| `11` | Game over | `28` | Time running out, indefinite |
| `12` | Power pellet | `29` | Enemy spawn |
| `13` | Enemy capture | `2A` | Enemy destroyed |
| `14` | Paddle bounce | `2B` | Weapon charge |
| `15` | Wall bounce | `2C` | Boss appears |
| `16` | Brick shatter | `2D` | Falling whistle |
| `17` | Ball lost | | |
| `2E` | Shield hit | `37` | Door slam |
| `2F` | Power-up expires | `38` | Switch click |
| `30` | Empty weapon | `39` | Landing thud |
| `31` | Secret discovered | `3A` | Skid |
| `32` | Round start | `3B` | Warp failed |
| `33` | Bomb armed | `3C` | Magic cast |
| `34` | Reload | `3D` | Freeze |
| `35` | Springboard | `3E` | Bonus tally |
| `36` | Splash | `3F` | Objective failed |

Stop or interrupt indefinite presets when the gameplay condition ends.
Weapon charge is a finite 1.6-second rising warble; pause/resume are sound
cues, not playback-control commands. Pitch/speed scaling applies to every preset.
Bomb armed is a finite accelerating tick sequence, not a detonation scheduler.

### Status and events

A read returns a **19-byte coherent snapshot**, latched at read-address
acceptance. Short reads are allowed; bytes beyond the snapshot are `FF`.
Reads never consume events. End a read with master NACK and STOP.

| Offset | Field |
| --- | --- |
| 0 | Protocol version, `01` |
| 1 | Flags: active `01`, received command awaiting execution `02`, event overflow `04` |
| 2-5 | Active request ID, priority, effect kind, preset ID (`00` for raw effect) |
| 6 | Playback FIFO depth, 0-2 |
| 7-8 | Nominal current output Hz; zero during rests or silence |
| 9-10 | Last executed request ID and result |
| 11-12 | Oldest unread event sequence, zero if empty |
| 13-14 | Event request ID and type, zero if empty |
| 15-16 | Completed-effect counter |
| 17-18 | Rejected-request counter |

Results: none `00`, accepted/started `01`, queued `02`, priority rejection
`03`, queue full `04`, target mismatch `05`, updated `06`, stopped `07`,
queue cleared `08`, active-effect parameter rejection `09`.

Event types: started `01`, queued `02`, completed `03`, interrupted `04`,
rejected `05`, updated `06`, stopped `07`, queued sound cancelled `08`.
Started also reports that a queued sound has begun. Updates carry the active
sound ID; their execution result carries the UPDATE request ID.

Read and process the oldest event, then ACK its exact sequence with command
`14`. ACK retries with a stale sequence do nothing. ACK does not replace the
last request result. The four-entry event FIFO retains unread events and
sets overflow if another event cannot fit. Overflow clears when all retained
events have been acknowledged. Do not interpret an overflowed event stream
as complete: reconcile active/FIFO state and counters, or stop and restart.
Event sequences cycle through 1-65535; completion/rejection counters wrap at
65536. Completion counts only natural finishes, not interruption or stop.

A bus ACK means **received**, not played. Match status request ID/result
after the command-pending flag clears. A single foreground mailbox bounds
RAM use; another write while it is occupied is NACKed. Malformed commands,
unknown addresses/kinds, invalid parameters and overlength packets are
NACKed without executing. Incomplete packets discard at the next START.
After final acceptance, extra bytes are NACKed without undoing the command.
An ambiguous transport failure may still have accepted a command: inspect
status/events before resubmitting a gameplay event.

### Execution model

Timer1 generates PB1 in hardware without enabling complementary SDA/PB0
output. The 1 Hz case uses compare-toggle. Frequencies round to timer
divisors; nominal Hz in status is not a measured frequency.
Timer0 provides a 1 ms interrupt. Effect interpolation, scaled elapsed-time
tracking, priorities, playback and event bookkeeping run in the foreground.
There is no heap, busy-waiting sound loop, mixing or noise generator.
PB3/PB4 remain inputs.

USI handlers receive packets, send status and ACK/NACK, including master
ACK/NACK on reads. A stalled START is finished in the foreground rather
than spinning in an ISR. Status can lag by a foreground pass; reads remain
internally coherent. Queued sounds begin on the next service pass after
completion, not at sample-accurate boundaries. RC clock accuracy and bus
latency need bench qualification.
