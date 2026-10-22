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
- Slow clocks return only after a fixed 1 us LOW settling interval. Hold a frozen PWM HIGH for at least 1 us before selecting preloaded LOW SIO; do not repeat this handover guard on already-SIO steps. Wait at least 1 us after isolation before driving Pico data pins or resuming trapped execution.
- Release RESET while CLK is stopped LOW, wait at least 1 us, then start PWM. Integer/even PWM selection must never exceed the requested frequency. Configure clk_sys to 144 MHz at SDK startup (1440 MHz PLL_SYS VCO, reference divider 1, post-dividers 5 and 2); an 8 MHz request produces nominally exact 8 MHz. USB retains its independent 48 MHz PLL.
- Decode only A0/A1/A2/A4 (mask0x17): console00/01 and disk10-14 remain distinct; A3/A5-A7 and high byte alias.
- Stage 8 diagnostics use masked result port 06. The replacement Stage 10 compiles the Stage 9 automatic-loader application: its read-only disk diagnostic uses data port 06 and the shared boot-report result port. Generated test programs and expected alias replies must match the trap.
- Loader resets six clocks, releases RESET, supplies two exit clocks, then injects. Re-reset to PC zero after verification before SRAM execution. Host cycle models do not prove physical reset-exit timing.
- Address and data buses are short shared trunks with taps, not stars or implied series paths.
- Z80 BUSACK# floats address, data, MREQ#, IORQ#, RD#, and WR#; fitted 5 V pull-ups define monitored controls during the grant.

## Firmware Scope

Pico source remains breadboard-first. The replacement Stage 10 inherits the
tested Stage 9 automatic loader's 120 MHz supervisor and nominal 20 MHz Z80
settings; this does not qualify combined network operation. The existing
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

- Qualify at 1 MHz first, then increase requests from 2-6 MHz in 500 kHz steps. Actual rates differ: requests of 6 and 6.5 MHz both produce 6 MHz; requests of 7, 7.5 and 8 MHz produce 6.545455, 7.2 and 8 MHz. Qualify distinct actual rates, not duplicate requests; actual rates above 6 MHz are experimental even if measured clean.
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
- Core0 owns GPIO, clock, injection and trapping; core1 owns USB, networking and runtime flash writes. Stages 9/10 launch core1 before journal recovery; both cores participate in SDK flash lockout. Never enable `PICO_FLASH_ASSUME_CORE1_SAFE` with the USB core running. Flash offsets, journal and geometry are unchanged.
- Stage 10 uses `pico_cyw43_arch_lwip_poll` on core1, not core0 network timers or a background architecture. USB provisioning passes requests/results through queues; core1 alone accesses ENV. A browser owns CP/M exclusively while connected, with USB fallback on disconnect. `FS_SERVER` is stored only; remote-FS I/O is deferred.
- Runtime writes keep trapping armed until BUSACK# LOW and re-arm it before BUSACK# HIGH. Non-`PICO_OK` flash-safe results fail closed through watchdog reboot.
- Wi-Fi polling is bounded and nonblocking. Core 1 services disk writes before network work on every loop.
- Phase 10 weather uses asynchronous verified HTTPS on core1, with an explicit
  8 KiB main-RAM core1 stack. Do not enlarge the default scratch-RAM stack
  beyond its region or move TLS, ENV access, or weather parsing into the Z80 trap.

## Core and Module Responsibilities

These are the current ownership contracts, not an invitation to move work
between cores. A change of owner or network architecture requires an explicit
design decision and validation of every affected caller.

| Owner | Responsibilities | Boundary |
| --- | --- | --- |
| Core 0 | Z80 bus GPIO, PWM/stepped clocks, assisted SRAM loading, trap/IRQ completion, queued Z80 supervisor requests, flash-quiescence participation | No USB endpoint calls; no Wi-Fi, sockets, TLS/JSON, or runtime flash programming in the trap |
| Core 1 | USB initialization, IRQ/background tasks, terminal and supervisor framing, CYW43/lwIP, HTTP/WebSocket, asynchronous services, ENV, runtime disk/service writes | No direct takeover of Z80 bus pins; request Z80 actions and quiescence through queues |
| Shared console router | Bounded RX/TX storage and exclusive USB/browser console ownership | Transport-neutral API; no Wi-Fi lifecycle or socket ownership |
| Service orchestration | Service ordering, connectivity-generation propagation, and bounded scheduling | Disk first, then network progress; do not merge independent service state machines |

Follow maintained sources rather than creating parallel transport abstractions:

- `src/stage10_websocket_terminal/network_runtime.cpp` owns connectivity,
  retry/SNTP state and address/generation publication. It must not depend on
  terminal session state or consume console bytes.
- `src/stage10_websocket_terminal/web_terminal.cpp` owns HTTP/WebSocket,
  protocol sequencing, heartbeat, consumption ACKs and session teardown.
  It consumes published network state, not Wi-Fi configuration internals.
- `src/stage10_websocket_terminal/network_services.c` orchestrates independent
  services. Weather/TLS must not become terminal or trap dependencies.
- `src/common/terminal_queue.c` owns console rings and atomic ownership handover.
  USB ingestion and supervisor framing run on core 1 in the shared Stage 9
  application; only queued Z80 control requests execute on core 0.
  Never mix ordinary USB keystrokes into the browser-owned CP/M stream, leak stale
  queued bytes across handover, or replay unconfirmed input after reconnect.
- `src/stage09_flash_storage/usb_console.c` registers bounded diagnostic stdout,
  initializes USB and the SDK default alarm pool on core 1, and drains logs
  there. The target skips runtime default-alarm initialization and disables the
  SDK stdout mutex; no other core may call USB endpoints. The four-entry
  command queue and 4 KiB log ring report overflow explicitly. Core 1 starts
  before loading but runtime storage/service/network workers remain gated
  until verified CP/M startup. USB is serviced while loading or paused/stopped.
- These multicore USB contracts apply to Stages 9/10; earlier bring-up targets
  retain their single-core diagnostic USB behavior.
- Keep Z80 port/trap handlers as bounded adapters to buffers and worker requests.
  Do not perform slow service transactions synchronously in those handlers.

### Shared State and Callback Lifetime

- Use the existing SDK critical sections, queues or appropriately ordered atomics.
  `volatile` is not a cross-core publication mechanism, and disabling interrupts
  on one core does not exclude the other.
- Keep shared-lock operations short and bounded; avoid nested queue locks and
  per-byte lock churn. Do not hold console/storage locks across network calls,
  flash operations, blocking USB output or waits for another core. Define lock
  ordering before adding any second lock.
- Keep trap/IRQ handling allocation-free and deterministic. Defer diagnostics,
  formatting and retries to normal service context; do not remove the required
  busy-wait settling guards as a latency optimisation.
- All CYW43/lwIP operations must follow the selected SDK architecture's context,
  affinity and locking requirements. In this project they run on core 1 with
  `pico_cyw43_arch_lwip_poll`; do not add a second poll owner or move them into
  core-0 IRQs. CYW43-managed resources must also use the driver-safe context.
- Raw lwIP callbacks must not block or destroy an object still being parsed or
  dispatched. Defer abort/removal to a safe boundary, detach callbacks before
  releasing resources, and follow lwIP's `ERR_ABRT` contract when aborting from
  a raw callback. An error callback may run after the PCB is already freed.
- Invalidate stale DNS, reconnect, timer and browser-consumption callbacks with
  existing generation/session tokens. Teardown must reclaim connection slots
  once, without allowing an old callback to reset a new session.

## Performance and Pico SDK Practices

- Prefer bounded work and explicit ownership over speculative concurrency.
  Use nonblocking state machines, work budgets and wrap-safe monotonic deadlines;
  do not busy-spin for network, queue space or another core. Hardware timing
  guards are deliberate exceptions, not general-purpose retry loops.
- Bound message sizes, queue bytes/counts, fragmentation, pending control replies,
  dispatch and retries. Specify behavior at each bound: retry/backpressure,
  explicit disconnect/error, or a documented discard policy for noncritical data.
  CP/M input/output must not be silently evicted or reported successful on loss.
- Keep each frame's TCP enqueue atomic. With `TCP_WRITE_FLAG_COPY`, temporary
  frame buffers can be released after acceptance; zero-copy requires a separately
  proven ACK/abort lifetime. TCP acceptance, output and browser consumption are
  different states. Retry `tcp_output` without re-enqueueing accepted frames;
  application consumption retransmits remain governed by protocol ACKs.
- Batch terminal output within the bounded window and flush once per burst.
  Preserve immediate interactive/control sends and configured TCP_NODELAY.
  A successful flush does not prove the browser rendered or consumed anything.
- Avoid repeated pbuf-chain traversal, header decoding, payload copies and
  per-frame heap/list allocations. Prefer span processing, moved buffers and
  fixed rings where they preserve masking, fragmentation, control interleaving,
  ordering and the text callback's NUL-termination contract.
- Budget total memory, not just payload bytes: include partial frames, vector
  capacity, queue/container overhead, lwIP pools, TLS peaks and both core stacks.
  Inspect linker/size output and stack placement when changing memory use.
  Do not enlarge scratch-RAM stacks or network pools to hide a leak/stall.
- Use SDK GPIO mask/snapshot operations only when they preserve pin ownership,
  preload-before-output, OE isolation and timing. Consider PIO/DMA or clock
  changes only after measuring a real bottleneck and designing ownership,
  interrupt, buffer-lifetime and error handling; do not introduce them by default.
- Preserve Pico 2 W/RP2350 board, clock and USB assumptions in CMake and actual
  SDK builds. Use supported flash-safe/multicore APIs; never bypass lockout or
  assume arbitrary interrupt code remains executable during an XIP flash write.
  RAM placement alone is insufficient without checking callees and data access.
- Rate-limit diagnostics and keep formatting/blocking logging out of hot paths
  and shared locks. Observability must not create the stall being measured.
- Fast disk reads and cached writes are not evidence that commits interfere
  with networking, or that they cannot. Measure flash commits and network-poll
  gaps separately before changing storage scheduling, journaling or geometry.

### Maintained WebSocket Fork

The submodule uses `https://github.com/gloveboxes/pico-ws-server.git`;
`origin` is the maintained fork and `upstream` is the original cadouthat
repository. Implement transport fixes in the fork, not generated source patches.
Preserve dirty submodule changes, test the actual sources, then commit/publish
the fork before advancing the parent's gitlink. Verify the published SHA and
submodule URL; a parent commit cannot capture uncommitted nested source changes.
