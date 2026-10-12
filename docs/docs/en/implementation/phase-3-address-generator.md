# Phase 3 - Control and Port Monitor

<div data-checklist="phase-3" data-checklist-label="Phase 3" markdown="1">

Tick setup items when complete and tests only after recording the measured
result. Progress is saved in this browser; ticks are not automatic verification.

<button type="button" class="md-button" data-checklist-clear hidden>Clear checklist</button>
<p data-checklist-status role="status" aria-live="polite"></p>

<input type="checkbox" data-checklist-id="prerequisite" aria-label="Phase 3 prerequisite passed"> **Prerequisite:** [Phase 2](phase-2-buffer-clock.md#pass-gate).
**Install:** U7 SN74LVC244AN only; Z80/SRAM remain absent.
The historical source-directory name is retained, but no address expander is fitted.

## Wiring - Control and Port Monitor

**U7 is the SN74LVC244AN eight-channel input buffer, powered from +3.3 V.**

Disconnect external power and USB; keep U7 out of its socket until wiring
checks pass. CPU and SRAM remain absent. Tick each channel after checking
both wires at the actual socket contacts and Pico pins, and isolation from
adjacent pins. Pico pin numbers below are physical pin positions, not GPIO
numbers. See the [monitor reference](../hardware/address-interface.md#port-address-monitor).

| Done | Signal | Z80 socket → U7 LVC244 input | U7 LVC244 output → Pico pin |
| --- | --- | --- | --- |
| <input type="checkbox" data-checklist-id="monitor-busack" aria-label="BUSACK monitor channel wired and checked"> | BUSACK# | Z80 pin 23 → U7 pin 2 | U7 pin 18 → Pico pin 1 (GP0) |
| <input type="checkbox" data-checklist-id="monitor-iorq" aria-label="IORQ monitor channel wired and checked"> | IORQ# | Z80 pin 20 → U7 pin 4 | U7 pin 16 → Pico pin 2 (GP1) |
| <input type="checkbox" data-checklist-id="monitor-rd" aria-label="RD monitor channel wired and checked"> | RD# | Z80 pin 21 → U7 pin 6 | U7 pin 14 → Pico pin 32 (GP27) |
| <input type="checkbox" data-checklist-id="monitor-wr" aria-label="WR monitor channel wired and checked"> | WR# | Z80 pin 22 → U7 pin 8 | U7 pin 12 → Pico pin 34 (GP28) |
| <input type="checkbox" data-checklist-id="monitor-a0" aria-label="A0 monitor channel wired and checked"> | A0 | Z80 pin 30 → U7 pin 11 | U7 pin 9 → Pico pin 24 (GP18) |
| <input type="checkbox" data-checklist-id="monitor-a1" aria-label="A1 monitor channel wired and checked"> | A1 | Z80 pin 31 → U7 pin 13 | U7 pin 7 → Pico pin 25 (GP19) |
| <input type="checkbox" data-checklist-id="monitor-a2" aria-label="A2 monitor channel wired and checked"> | A2 | Z80 pin 32 → U7 pin 15 | U7 pin 5 → Pico pin 26 (GP20) |
| <input type="checkbox" data-checklist-id="monitor-a4" aria-label="A4 monitor channel wired and checked"> | A4 | Z80 pin 34 → U7 pin 17 | U7 pin 3 → Pico pin 27 (GP21) |

| Done | Check |
| --- | --- |
| <input type="checkbox" data-checklist-id="monitor-fixed-pins" aria-label="Existing U7 ground supply and bypass connections verified"> | **Verify existing U7 connections:** pins 1/10/19 directly to common GND, pin 20 to Pico-derived **+3.3 V, not +5 V**, and C5, 100 nF, nearby between +3.3 V and GND |
| <input type="checkbox" data-checklist-id="monitor-install" aria-label="U7 orientation and wiring checked before installation"> | **Install U7:** with external power and USB disconnected, confirm continuity, no adjacent-pin shorts and correct pin-1 orientation before insertion |

Never connect the 5 V Z80 signals directly to the Pico; route them through U7.

### Why A4 is monitored but A3 is not

The Pico decodes I/O ports using `address & 0x17`. The `&` operator is
**bitwise AND**: a result bit is 1 only when both corresponding bits are 1.
The hexadecimal mask `0x17` is binary `0001 0111`:

```text
Address bit:  A7 A6 A5 A4 A3 A2 A1 A0
Mask 0x17:     0  0  0  1  0  1  1  1
```

This keeps **A4, A2, A1 and A0**, matching the four address connections to U7.
A3, A5-A7 and the high address byte are deliberately ignored for I/O decoding.

For example, `0x08` differs from `0x00` only in A3. Masking removes that bit:

```text
Address 0x08:  0000 1000
Mask    0x17:  0001 0111
               ---------
AND result:    0000 0000 = 0x00
```

Thus `0x08` **aliases** `0x00`: the Pico treats both as the same port.
Similarly, `0x09 & 0x17` gives `0x01`.

**A4 represents 16 (`0x10`)**, so keeping it distinguishes console ports
`0x00/01` from disk ports `0x10-14`:

| Port | Binary address | After AND with `0001 0111` | Decoded port |
| --- | --- | --- | --- |
| Console `0x00` | `0000 0000` | `0000 0000` | `0x00` |
| Console `0x01` | `0000 0001` | `0000 0001` | `0x01` |
| Disk `0x10` | `0001 0000` | `0001 0000` | `0x10` |
| Disk `0x11` | `0001 0001` | `0001 0001` | `0x11` |
| Disk `0x12` | `0001 0010` | `0001 0010` | `0x12` |
| Disk `0x13` | `0001 0011` | `0001 0011` | `0x13` |
| Disk `0x14` | `0001 0100` | `0001 0100` | `0x14` |

If only A0-A2 were monitored, the mask would be `0x07` and disk port `0x10`
would become indistinguishable from console port `0x00`:

```text
Disk address:  0001 0000 = 0x10
Mask:          0000 0111 = 0x07
               ---------
AND result:    0000 0000 = 0x00
```

This omission applies only to **I/O monitoring**, not memory addressing.
A3 still connects **Z80 pin 33 to SRAM pin 9** in
[Phase 4](phase-4-address-bus.md#wiring-address-trunk); do not connect it to U7.

The four monitored bits provide 16 distinct I/O ports. See
[I/O ports and services](../system/io-ports.md) for fixed terminal/disk
assignments and the proposed shared service allocation.

## Firmware and Tests

<input type="checkbox" data-checklist-id="stage-firmware" aria-label="Stage 3 firmware built and loaded"> Build `z80_stage03_control_inputs`; UF2:
`build/src/stage03_mcp23s17/z80_stage03_control_inputs.uf2`.
[Maintained application](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage03_mcp23s17/main.c).

<input type="checkbox" data-checklist-id="idle-inputs" aria-label="Monitor idle controls and port measured"> `s` samples BUSACK/IORQ/RD/WR and `port`. With CPU absent and pulls fitted,
controls are HIGH and port is `17`.

<input type="checkbox" data-checklist-id="input-low-tests" aria-label="Every monitor input LOW test passed"> Pull each input LOW through a temporary
1 kOhm resistor; confirm only the matching GPIO/decoded bit changes and U7
outputs remain in the 3.3 V domain. Never short output pins to a rail.

<input type="checkbox" data-checklist-id="wait-release" aria-label="Independent WAIT assertion and release measured"> Pull IORQ LOW and verify WAIT LOW. Type `w` to raise IO_RELEASE: WAIT must
rise without either data OE becoming active.

<input type="checkbox" data-checklist-id="wait-rearm-fixture" aria-label="WAIT rearmed and temporary monitor pulls removed"> Type `x` to rearm WAIT, then
remove the temporary pull. This tests WAIT independently of data direction.

## Pass Gate

- <input type="checkbox" data-checklist-id="gate-routes" aria-label="Phase 3 all eight input routes passed"> All eight input routes correct.
- <input type="checkbox" data-checklist-id="gate-wait" aria-label="Phase 3 WAIT release with disabled data paths passed"> WAIT release works with data paths disabled.
- <input type="checkbox" data-checklist-id="gate-decode" aria-label="Phase 3 masked address decode passed"> Decoded address bits match `address & 0x17`, not a full 8-bit decode.

</div>

## Maintained Source

The complete application below is included from the repository at documentation
build time, not copied into this page.

??? example "Stage 3 application - src/stage03_mcp23s17/main.c"

    ```c
    {% include "../../../../src/stage03_mcp23s17/main.c" %}
    ```
