# Architecture Invariants

## Current Hardware

The current assisted-loader design has eight active through-hole packages:

- Raspberry Pi Pico 2 W / RP2350 supervisor
- Z84C0020PEC Z80
- AS6C1008-55PCN SRAM, lower 64 KiB selected with A16 LOW
- SN74HCT32N fixed quad OR logic (not HC32)
- SN74AHCT244N 5 V output buffer
- SN74AHCT245N fixed Pico-to-5 V data path
- SN74LVC245AN fixed 5 V-to-Pico data path
- SN74LVC244AN eight-signal input buffer

The address expander and programmed PLD are retired. No external programmer
is required. The older LVC8T245 carrier, SD, PSRAM, three AHCT125 packages,
and HCT157/HCT08 arbitration are not current. The PCB remains the previous
circuit and must not be manufactured or used with this firmware.

## Bench Instruments

- The RIGOL DHO814 validates analogue voltage, edge quality, ringing,
  overshoot, pulse width, and close timing. Bond its chassis to protective
  earth before connecting any input/output lead.
- The DreamSourceLab DSLogic Plus validates digital state and ordering in the
  Group A-D capture sets defined in
  `docs/docs/en/hardware/logic-analyzer.md`.
- DSLogic Plus uses 16-channel, 100 MHz Buffer Mode for qualification. Its
  16-channel Stream Mode limit is 20 MHz and is only suitable for long-duration
  low-rate searches.
- DSLogic grounds are connected to the USB host ground. Connect them only to
  common circuit GND. CK and TI are limited to 0-3.3 V and are not used for
  standard project captures.
- Neither instrument replaces the other: digital thresholds do not prove
  analogue margins, and four scope channels do not prove whole-bus ordering.

## Voltage and Power

- All 5 V logic shares one regulated 5 V rail. Every installed 5 V device must be powered whenever that rail is energized.
- External 5 V reaches Pico `VSYS` only through a 1N5819, anode to external 5 V and banded cathode to `VSYS`. Never tie external 5 V to `VBUS`.
- On the populated board, apply external 5 V before USB and disconnect USB before removing external 5 V. AHCT245 data ports lack power-off isolation; USB-only operation/programming and arbitrary loss of external power with USB attached are unsupported.
- Pico 3.3 V powers LVC244/LVC245 and 3.3 V pull resistors. Pico is the only 3.3 V source.
- Pico GP0-GP25 are FT pads only while IOVDD is present. GP26-GP29 are not FT. Buffer all incoming 5 V signals.
- LVC244 mappings are BUSACK# GP0, IORQ# GP1, RD# GP27, WR# GP28, A0 GP18, A1 GP19, A2 GP20, A4 GP21. Both OEs are LOW.
- RP2350 GP23/24/25/29 are reserved by the Pico 2 W wireless interface; do not repurpose them.
- Pull-ups protect absent socketed drivers, not installed unpowered ICs.
- R17-R20 are 10k pulls HIGH on GP4/5/6/7; R21-R23 are 10k pulls LOW on GP2/3/9. No 5 V pull-up on direct RESET#. No WAIT pull is fitted.

## Assisted Loading and Buses

- Z80 alone drives A0-A15 and writes SRAM, including during loading. Pico injects LD HL,nn / LD (HL),n / LD A,(HL) under slow clocks; it never masters memory buses.
- AHCT244 translates GP2 CLK (2->18), GP4 BUSREQ (4->16), GP5 inhibit (6->14), MREQ (8->12), WR (11->9), RD (13->7). Unused inputs15/17 GND; outputs5/3 NC. Both OEs LOW. Keep the buffer beside CPU.
- HCT32 pins1/2->3 compute buffered RD OR buffered inhibit to SRAM OE; pins4/5->6 compute raw IORQ OR GP9 to WAIT; pins9/10->8 compute raw RD OR GP6 to U9 OE. Inputs12/13 GND, output11 NC.
- GP6 up-OE request and GP7 down-OE default HIGH. Both paths must be disabled before preload/direction changes. U9 DIR +5V, U10 DIR GND. RD gating is not an all-state hardware mutual exclusion guarantee.
- GP5 boot inhibit defaults HIGH; GP9 IO_RELEASE defaults LOW. Trap stops PWM, configures one data path, raises GP9, completes I/O with single clocks, isolates both paths, lowers GP9, then resumes PWM. Polling after free-running resume is unsafe.
- GPIO output latches are preloaded before output enable; GP10-GP17 begin as inputs. Explicitly select SIO and clear pad isolation. Resume reconnects PWM mux and resets its counter.
- Decode only A0/A1/A2/A4 (mask0x17): console00/01 and disk10-14 remain distinct; A3/A5-A7 and high byte alias.
- Loader resets six clocks, releases RESET, supplies two exit clocks, then injects. Re-reset to PC zero after verification before SRAM execution. Host cycle models do not prove physical reset-exit timing.
- Address and data buses are short shared trunks with taps, not stars or implied series paths.
- Z80 BUSACK# floats address, data, MREQ#, IORQ#, RD#, and WR#; fitted 5 V pull-ups define monitored controls during the grant.

## Firmware Scope

Pico source remains breadboard-first; Stage 10 starts at 1 MHz. The existing
PCB is not electrically equivalent and migration is deferred. Do not modify
PCB/project/routing/fabrication files or claim its compatibility. Preserve
specified populations: CPU/SRAM absent through Phase 5, both fitted in Phase 6.
The Phase 4 active address diagnostic is deferred until Phase 6.

## Physical Placement

The Phase 0-10 implementation plan is for the three-BB830 breadboard
prototype. PCB documentation is historical and explicitly marked deferred.
The current breadboard uses four bussed 8x10k networks and seven discrete
10k startup pulls (39 branches), seven bypass capacitors, three22uF and one100uF.

With BB830 row 1 at top and row 63 at bottom:

- Memory: HCT32 rows5-11 notchup, SRAM18-33 notchup.
- Core: supply clearance 1-3, AHCT244 8-17, Z80 19-38.
- Peripheral: Pico 1-20, LVC244 22-31, AHCT245 33-42, LVC245 44-53.

Narrow DIPs cross E/F. Z80 and SRAM require real 0.6-inch sockets. Verify
construction pin-1 corners. AHCT244/AHCT245/LVC245 notchdown; others notchup.

## Timing

- Qualify at 1 MHz first, then 2-6 MHz in 500 kHz steps. Treat 6.5-8 MHz as experimental even if measured clean.
- CPU 20 MHz grade is not a system rating. 55 ns SRAM plus AHCT244/HCT32 propagation and CPU setup/breadboard margins require measurements.
- I/O uses HCT32 hardware WAIT and stepped completion. WAIT, injected data, reset exit and resumed PWM remain unqualified without bench evidence.
- Use the 16-channel DSLogic Plus capture groups in
  `docs/docs/en/hardware/logic-analyzer.md` for digital buses and controls.
  Use 100 MHz Buffer Mode for qualification; 16-channel Stream Mode is only
  20 MHz. Use the four-channel DHO814 for analogue levels, edge quality, and
  correlated timing groups.

## Flash, CP/M, and Multicore Ownership

- Pico 2 W onboard flash is 4 MiB. Firmware must end below offset `0x290000`.
- Layout: journal `0x290000`/64 KiB, boot `0x2A0000`/128 KiB, drives at `0x2C0000`, `0x310000`, `0x360000`, `0x3B0000`, each 320 KiB.
- Never pass the `0x290000` linker limit as a compiler definition for physical `PICO_FLASH_SIZE_BYTES`; the C macro must remain 4 MiB.
- CP/M geometry is exactly 327,680 bytes = 2,560 x 128-byte records, 80 tracks x 32 records. It is not IBM 3740 geometry.
- Cold boot recovers journals, validates CRCs, injects instructions to load/verify SRAM, resets again and starts only on success. BUSACK is used for flash quiescence, never cold loading.
- Core0 owns GPIO, clock, injection and trapping; core1 owns networking/runtime flash writes. Flash offsets, journal, geometry and multicore lockout contracts are unchanged.
- Runtime writes keep trapping armed until BUSACK# LOW and re-arm it before BUSACK# HIGH. Non-`PICO_OK` flash-safe results fail closed through watchdog reboot.
- Wi-Fi polling is bounded and nonblocking. Core 1 services disk writes before network work on every loop.
