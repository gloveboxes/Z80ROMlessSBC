# Z80 ROMless SBC - WIP Engineering and Build Specification

**Repository:** [github.com/gloveboxes/Z80ROMlessSBC](https://github.com/gloveboxes/Z80ROMlessSBC)

## Overview

This is a theoretical Z80 single-board computer design that has not yet been
built or validated in hardware. Treat it as an engineering proposal and
bring-up plan, not a proven reference design. Its electrical assumptions,
timing margins, and firmware interactions require bench validation through
the staged [implementation plan](implementation/index.md).

## Reading path for a first hardware build

Start with the [inventory](hardware/inventory.md) and the
[breadboard and meter basics](hardware/construction.md#before-you-wire), then
follow the [implementation plan](implementation/index.md) in order. It tells
you what to install, wire, and test at each step. The hardware chapters are
pin-level references; you do not need to understand every firmware excerpt
before beginning the power checks.

Use the [glossary](reference/glossary.md) for unfamiliar terms. Expect to
use a multimeter from Phase 0 and a scope for startup/clock checks; the logic
analyzer is also required for the later bus and speed qualification captures.
This is a measured three-BB830 breadboard build, not a
wire-everything-and-power-on kit. PCB migration is deferred, and the historical
PCB documentation is hidden from this site. The existing PCB and fabrication
files describe the previous circuit and must not be used for this build.

## How the computer works

The design pairs a Z80 without a ROM chip with a Raspberry Pi Pico 2 W. The
Pico supplies the clock and reset, then feeds instructions to the Z80 with
SRAM reads inhibited. The Z80 writes and verifies its own RAM; it is the only
address/write master. The lower 64 KiB of a 128 KiB SRAM provides memory.
AHCT/LVC buffers separate 3.3 V and 5 V domains, and one HCT32 implements
fixed read-inhibit, WAIT, and data-output gates. No PLD programmer is needed.

The Pico also provides virtual peripherals. During a Z80 input/output request,
hardware pauses the processor while the Pico identifies the requested port and
exchanges one byte. Z80 `IN` and `OUT` instructions feed terminal queues; a
WebSocket VT100 terminal runs on the Pico's other core so network traffic does
not affect Z80 timing.

Z80 boot software and CP/M disks occupy reserved regions of the Pico's onboard
flash rather than removable media. The Pico reads the flash directly and uses
Z80-assisted loading to populate and verify SRAM, then resets to PC zero for
normal execution. Once running, virtual I/O disk ports access the
[onboard flash partition](system/operation.md#63-onboard-flash-cpm-disk-storage).

### How can the Z80 boot without ROM?

**The Z80 does not know whether its instructions come from ROM, RAM, or the
Pico. It reads instruction bytes from its data pins and executes them.**
During loading, the Pico temporarily supplies those bytes instead of SRAM.
Those instructions make the Z80 fill its own RAM.

1. **The Pico starts first.** It boots its firmware from onboard flash, holds
   the Z80 in reset, and initially keeps the Z80 clock stopped. SRAM contains
   unknown data at power-on. The Pico recovers the disk journal and validates
   the CP/M boot package before starting the loader.
2. **The Pico supplies the first instructions.** It clocks the Z80 through
   reset, releases reset, and supplies slow, controlled clock pulses. The Z80
   begins fetching at address `0000`, but SRAM's outputs are disabled.
   Instead, the Pico supplies instruction bytes through the AHCT245 data
   buffer at the appropriate read cycles. The Z80 still drives the address
   bus; the Pico supplies each byte according to the loading sequence.

    **How slow are these pulses?** They are individual software-controlled
    pulses, not a continuous fixed-frequency clock. Each loader cycle has
    these explicit waits:

    | Part of cycle | Programmed wait |
    | --- | ---: |
    | CLK LOW before rising | 1 us |
    | CLK HIGH | 1 us |
    | CLK LOW settling after falling | 1 us |
    | **Total** | **3 us per cycle** |

    Here, `us` means microseconds. Back-to-back pulses therefore have a
    wait-only rate of approximately **333 kHz**; software overhead makes the
    actual rate lower. Between consecutive pulses, the trailing LOW settling
    and the next leading LOW wait add to at least approximately 2 us LOW.
    Data preparation and other work extend that LOW interval further, so the
    loading clock is irregular rather than a steady 333 kHz waveform.
    The loader's reset and reset-exit clocks use the same slow stepping.
    These are firmware timing settings, not measured waveforms. They are
    separate from the **1 MHz normal startup clock** and the nominal **8 MHz**
    setting available for later qualification.

3. **The Z80 writes its own RAM.** For example, to store byte `A5` at address
   `1234`, the Pico supplies `LD HL,1234h` followed by `LD (HL),0A5h`. These
   instructions are encoded as the bytes `21 34 12 36 A5`. When the Z80
   executes the write, the Pico stops driving data. The Z80 puts `1234` on
   the address bus, puts `A5` on the data bus, and generates the SRAM write
   signals. Repeating this process loads the boot image.
4. **The Pico checks the result.** It injects instructions such as
   `LD HL,1234h` and `LD A,(HL)`. For the actual RAM read, the Pico disables
   its output driver and temporarily enables SRAM's outputs. It observes the
   returned byte through the LVC245 and compares it with the expected value.
   A failed verification leaves the Z80 held in reset instead of running CP/M.
5. **Reset again, then execute from RAM.** After successful verification,
   the Pico resets the Z80 to return its program counter to `0000`, isolates
   both Pico data paths, and allows normal SRAM reads. It releases reset with
   the clock stopped LOW, waits for reset setup, then starts the normal clock.
   This time SRAM supplies the instructions, and the loaded code starts CP/M.

**The HCT32 contains no boot program and needs no programming.** One of its
OR gates controls SRAM's active-LOW output enable:

```text
SRAM_OE# = buffered RD# OR buffered BOOT_READ_DISABLE
```

When `BOOT_READ_DISABLE` is HIGH, SRAM's data outputs stay disabled, leaving
the Pico free to supply injected instruction bytes. When it is LOW, SRAM
can respond to normal reads. This inhibits **SRAM output, not SRAM writes**:
the Z80 can still write RAM while receiving its instructions from the Pico.
Firmware keeps the Pico and SRAM from driving the data bus simultaneously.

In short: **the Pico feeds the Z80 instructions to write and verify the boot
image in RAM, then resets it so it starts again, this time executing from RAM.**
The Z80 is the only memory-address and write master throughout.

Normal execution starts at **1 MHz**. A nominal **8 MHz** setting is available
for later qualification; loading uses slow stepped clocks, not that run rate.
This is the intended sequence, not a claim of measured hardware operation.
See the [detailed loader protocol](hardware/address-interface.md#loader-sequence),
[Phase 6 tests](implementation/phase-6-sram.md), and
[frequency qualification](implementation/frequency-qualification.md).

### How long does loading take?

Allow **roughly 10 seconds or somewhat longer to load and verify the complete
64 KiB boot image** with the current firmware. This is a calculation-based
estimate, not a measured startup time.

| Operation | Explicit waits per byte | For 65,536 bytes |
| --- | ---: | ---: |
| Write the image into SRAM | 75 us | 4.92 seconds |
| Read back and verify every byte | 65 us | 4.26 seconds |
| **Total loading and verification** | **140 us** | **9.18 seconds** |

Writing each byte takes 20 stepped clocks; verification takes another 17.
At 3 us of programmed waits per clock, clock waits alone total approximately
7.27 seconds. The additional SRAM-inhibit, data-preparation, isolation, and
readback waits bring the total explicit waits to approximately 9.18 seconds.
GPIO operations, function execution, and interrupts add time beyond that.

Initial flash validation, any journal recovery, CP/M startup, and Wi-Fi
connection are separate from this load-and-verify estimate. **Only the boot
image is loaded into SRAM:** the four CP/M disks remain in Pico flash and are
accessed as needed, not copied during boot.

### How does the Z80 write the boot image into SRAM?

**The Pico gives the Z80 a "write this byte to this address" instruction for
every byte of the boot image.** The Z80 executes those instructions using its
normal memory-write hardware. There are two different things to distinguish:

- **Loader instructions:** bytes supplied by the Pico directly to the Z80
  through the AHCT245.
- **Boot-image bytes:** the contents those instructions tell the Z80 to store
  in SRAM.

For example, suppose the first boot-image byte is `C3`. To store it at address
`0000`, the Pico supplies this illustrative instruction sequence:

```asm
LD HL,0000h
LD (HL),0C3h
```

| Step | Who drives the data bus? | What happens |
| --- | --- | --- |
| 1 | Pico | Supplies `21 00 00`, the bytes for `LD HL,0000h`. The Z80 sets its HL register to destination address `0000`. |
| 2 | Pico | Supplies `36 C3`, the bytes for `LD (HL),0C3h`. The Z80 takes `C3` as the value to store. |
| 3 | Neither Pico nor SRAM | The Pico disables its data driver before the write. SRAM outputs remain disabled. |
| 4 | Z80 | Places address `0000` on A0-A15 and byte `C3` on D0-D7. |
| 5 | Z80 | Asserts MREQ# and WR#. Through the AHCT244, these make SRAM CE# and WE# LOW. |
| 6 | Z80 | Completes the write cycle. With the required timing met, SRAM now holds `C3` at address `0000`. |

**The byte `C3` is data in this operation, not an instruction being executed.**
It follows opcode `36`, which tells the Z80 to treat the next byte as the value
to write at the address in HL.

For the next boot-image byte, the Pico supplies `LD HL,0001h` followed by
`LD (HL),value`, using that next byte as `value`. It repeats for addresses
`0002`, `0003`, and so on through `FFFF` for the complete 64 KiB image.

The injected loader instructions **are not automatically stored in SRAM**.
Reading an instruction from the data bus does not write memory; the explicit
`LD (HL),value` operation performs the write.

Only SRAM's **output enable, OE#**, is inhibited during instruction injection.
Its **write enable, WE#**, still works:

- During injected instruction reads: **Pico to Z80**.
- During memory writes: **Z80 to SRAM**, with the Pico data driver disabled.

After loading and verification, the Pico resets the Z80 to address `0000` and
allows normal SRAM reads. **The stored boot-image bytes now become the
instructions the Z80 executes.** The
[maintained loader](implementation/phase-6-sram.md#maintained-source) implements
this sequence; the example above explains the byte roles rather than replacing
that source.

### How are the Pico and Z80 synchronized during loading?

**The Pico controls the Z80 clock, so it controls when the Z80 can advance
to the point where it reads a byte.** It does not have to race a freely
running Z80 during loading.

Think of it as: **advance the Z80, pause, prepare the next byte, then advance
the Z80 to read it.**

For each injected instruction or immediate operand byte, the current loader
uses this sequence:

| Step | Pico action | Purpose |
| --- | --- | --- |
| 1 | Generate one complete clock cycle, then leave CLK LOW | Advance into the expected Z80 read cycle |
| 2 | Keep SRAM outputs disabled | Prevent SRAM from answering the read |
| 3 | Disable both data translators, allow isolation to settle, and prepare the byte on the Pico data pins | Keep intermediate GPIO changes off the Z80 bus |
| 4 | Allow data to settle, then request the upward AHCT245 path | Present the byte when Z80 RD# is LOW |
| 5 | Generate two more clock cycles while holding the byte | Advance through the read and its completion |
| 6 | After the final falling-edge settling interval, disable the Pico data path | Release the bus before subsequent operations |
| 7 | For an opcode, generate one additional cycle | Complete the four-T-state opcode-fetch cycle, including its refresh portion |

An immediate operand read uses three clock cycles; an opcode fetch uses four.
The specified CMOS Z80 is **fully static**, meaning its clock can be paused
without losing its execution state. While CLK is held LOW, it cannot advance
to a later sampling edge. The Pico prepares the byte before generating that
edge; a software delay between steps extends the pause instead of letting the
Z80 run ahead.

The HCT32 adds read-strobe gating to the upward data path:

```text
AHCT245_OE# = Z80_RD# OR PICO_DATA_UP_OE#
```

Both inputs must be LOW to enable the translator: the Z80 must be reading,
and the Pico must have requested upward drive. This gate does not select the
byte or synchronize the instruction sequence by itself, and it is not an
all-state interlock against another bus driver.

**The loader counts clocks rather than polling RD# for each byte.** It does
not use a memory-read interrupt or a request/acknowledge protocol. It relies
on the expected reset-exit phase, the known instructions being injected, and
their clock counts. If that phase or a cycle count is wrong, it can supply the
wrong byte at the wrong time. Slow clocks provide settling time, but do not
prove the sequence correct: real captures of CLK, M1#, RD#, data, and the
enables must confirm it.

The [maintained loader source](implementation/phase-6-sram.md#maintained-source)
and [Phase 6 qualification procedure](implementation/phase-6-sram.md#firmware-and-loader-qualification)
show the implementation and required checks. Normal-run I/O uses a different
[hardware-WAIT-assisted trap protocol](system/operation.md#61-hardware-wait-assisted-clock-stop-trap-protocol).

### CP/M Boot and Disk Flow

The complete software and storage path is:

1. The image build combines CP/M's command processor (CCP), core operating
   system (BDOS), and board input/output layer (BIOS) in `z80boot.img`. It adds
   an integrity header and checksums to create `z80boot.pkg`.
2. The build also writes the CCP, BDOS, and BIOS to Drive A's reserved boot
   area and emits Drives A-D as separate 320 KiB CP/M disk images.
3. It combines the final software for the Pico 2 W, `z80boot.pkg`, and all four
   disks in `z80romless-flash.bin`, the complete 4 MiB image used for initial
   flash setup. The separate files allow later updates without replacing the
   rest of flash.
4. On cold boot, the Pico completes any interrupted disk write, validates
   `z80boot.pkg`, and uses the injected instructions described above to make
   the Z80 write and read back its 64 KiB payload in SRAM. After verification,
   the Pico resets the Z80 again and starts execution from SRAM. The BIOS
   installs CP/M's restart and system-call entry points, then displays the
   `A>` prompt.
5. Drives A-D are persistent read/write disks. During operation, the BIOS
   converts disk requests into 128-byte transfers that the Pico reads from or
   writes to flash. The Pico caches writes, commits them after 250 ms of
   inactivity or when CP/M requests an immediate save, and uses a recovery log
   to protect each flash update. A warm boot does not use `z80boot.pkg`; the
   BIOS reloads the CCP and BDOS from Drive A before returning to the prompt.

See the [CP/M boot-image README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md),
[disk-media README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md),
[flash-storage design](system/operation.md#63-onboard-flash-cpm-disk-storage),
and [CP/M appendix](cpm-dcc/index.md) for construction details, addresses,
protocols, and validation evidence.

The build plan proves each subsystem before relying on it in the next phase.
It progresses from power and bus isolation through injected loading and Z80 execution,
virtual peripherals, flash storage, the browser terminal, and measurement of
the maximum reliable clock speed.

## Project Documentation

The repository includes these focused implementation guides:

- [Stage 0: Power and passive wiring](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage00_power/README.md) -
   power, continuity, resistance, rail-voltage, and diode-OR checks.
- [Native CP/M boot image](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md) -
   memory layout, Z80 optimization, image construction, BIOS behavior, and host
   tests.
- [DCC debug I/O adapter](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/dcc_io_adapter/README.md) -
   Altair-compatible I/O drivers, native disks, interrupts, configuration, and
   ANSI terminal input.
- [CP/M disk media](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md) -
   source formats, conversion, native geometry, generated images, and flash
   provisioning.
