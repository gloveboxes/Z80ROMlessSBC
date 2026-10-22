---
name: z80-romless-sbc
description: 'Engineer, review, debug, build, test, flash, or document the Z80ROMlessSBC project. Use for Pico 2 W/RP2350 firmware, core 0/core 1 responsibilities, multicore synchronization, decoupling Wi-Fi/WebSocket/USB serial, terminal throughput and latency, bounded queues, lwIP backpressure, interrupt hot paths, Pico SDK best practices, UF2 flashing, Z80-assisted SRAM loading, HCT32 logic, voltage translation, breadboard placement, CP/M images, onboard-flash storage, staged bring-up, KiCad schematic checks, MkDocs maintenance, DSLogic Plus/DHO814 captures, timing qualification, or BOM and pin-map changes.'
argument-hint: 'Describe the hardware, firmware, documentation, or validation task'
user-invocable: true
---

# Z80 ROMless SBC Engineering

Use this workflow to preserve electrical safety, explicit core/module ownership,
bounded low-latency firmware, build reproducibility, and documentation consistency.

For disk-only deployment to the existing flash layout, invoke the project-local
`flash-cpm-disks` skill instead of the engineering workflow below. It contains
the self-contained conversion, no-backup deployment, journal, programming, and
verification procedure. Use this broader skill when changing firmware, BIOS,
geometry, or flash layout.

## Start With Current Sources

1. Treat `docs/docs/en/` as the engineering and bring-up specification;
   `README.md` is only the concise project entry point.
2. Treat maintained files under `src/` and `hardware/kicad/` as authoritative
   implementation sources. MkDocs C excerpts explain invariants but are not
   canonical code.
3. Load [architecture invariants](./references/architecture.md) before changing
   firmware, concurrency, transport boundaries, performance, pins, parts, buses,
   power, flash, or timing.
4. Load [validation workflows](./references/validation.md) before building, testing, regenerating artifacts, or declaring a change complete.
5. Load [documentation lessons](./references/documentation.md) before
   reorganizing MkDocs pages/navigation, editing diagrams, changing
   terminology, or moving documentation assets.
6. For PCB placement, routing, `pcbnew`, DSN/SES, trace-length, DRC, or
   fabrication work, also use the project-local `kicad-pcb` skill.

## Procedure

1. Identify the owning surface and, for firmware, the owning core, IRQ/callback
   context, shared state, and module boundary before proposing changes.
2. Read the nearest implementation and its corresponding page under
   `docs/docs/en/implementation/` before editing.
3. State one local hypothesis and the cheapest check that can falsify it. For
   performance changes, identify the metric, baseline, work/memory budget, and
   correctness invariants; do not assume disk, Wi-Fi, or GPIO is the bottleneck.
4. Make the smallest change that preserves the invariants in the references.
5. Run a focused validation immediately after the first substantive edit.
6. Expand validation according to blast radius:
   - Pin, logic, power, or package changes require schematic, BOM, placement, phase-plan, and documentation consistency checks.
   - Shared firmware changes require affected stage builds and later cumulative stages.
   - Transport or concurrency changes require actual implementation tests for
     backpressure, bounded work, handover, failure, and callback/resource lifetime.
   - Flash or CP/M changes require host image tests, exact geometry checks, and final artifact generation.
   - Documentation changes require a strict MkDocs build from `docs/docs`.
7. Report measured or tested behavior separately from theoretical limits. Never promote the 20 MHz CPU rating to a system claim.

## Firmware Design Gate

Apply these checks to every firmware change; the architecture reference defines
the detailed contracts.

- **Core 0:** Z80 bus GPIO, clock, assisted loading, trapping/stepped I/O,
  queued Z80 supervisor requests, and the core-0 side of flash quiescence.
- **Core 1:** USB initialization, IRQ/background work, console and supervisor
  framing, CYW43/lwIP polling, Wi-Fi lifecycle, HTTP/WebSocket sessions,
  asynchronous network services, ENV access, and runtime flash/storage work.
  Service pending disk work before network work; preserve regular network polls.
- **USB isolation (Stages 9/10):** initialize the default alarm pool on core 1,
  launch it before loading, and use real multicore lockout during recovery.
  Keep USB endpoint calls off core 0; use bounded command/diagnostic queues
  and atomic CPU-state/cancellation publication. Earlier single-core
  bring-up stages retain their diagnostic USB setup.
- **Separate responsibilities:** Wi-Fi publishes connectivity/address/generation,
  WebSocket owns sessions/protocol/backpressure, the console router owns
  transport-neutral queues and USB/browser ownership, and orchestration schedules
  services. Network modules must not consume USB input or manipulate Z80 bus pins.
- **Bound the hot path:** no network, TLS/JSON, heap allocation, blocking stdio,
  flash writes, or waiting on another core inside trap/IRQ handling. Keep hardware
  settling guards and flash-safe ownership handshakes intact.
- **Bound shared work:** short non-nested critical sections, explicit queue and
  dispatch limits, defined overflow/retry/timeout behavior, and callback-safe
  teardown. `volatile` or local IRQ masking alone does not synchronize two cores.
- **Preserve transport semantics:** distinguish rejected writes, TCP-accepted
  frames, pending flushes, and browser-consumed output. Batch output without
  delaying single interactive/control messages; never resend accepted frames
  merely because flushing failed.
- **Use Pico SDK contracts:** retain the selected poll architecture and CYW43
  affinity, supported synchronization/flash-safe APIs, wrap-safe deadlines, and
  verified stack/RAM/flash budgets. Do not substitute overclocking, larger pools,
  zero-copy buffers, or a new concurrency architecture for measured diagnosis.
- **Prove the result:** test real code at the changed boundary, build affected
  stages with the actual SDK, and use the validation reference's hardware metrics
  before claiming a throughput, latency, or timing improvement.

## Project-Specific Guardrails

- Do not revive superseded SD-card, PSRAM, LVC8T245-carrier, or 74HCT157/74HCT08 architectures without an explicit redesign request.
- Do not connect a 5 V output directly to a Pico GPIO.
- Do not bypass the AHCT244 on Z80-to-SRAM controls; Z80 TTL HIGH is not sufficient for SRAM CMOS inputs.
- Do not revive the retired address expander/PLD or add Pico memory bus mastering without an explicit redesign request.
- The existing PCB is the previous circuit. Keep PCB/project/routing/fabrication files unchanged until migration is separately authorized; the default KiCad build is schematic-only.
- Do not treat a physically installed but unpowered 5 V IC as equivalent to an absent socketed device.
- Bond the DHO814 chassis to protective earth before connecting any input or
   output lead; ordinary passive probes are not isolated.
- Treat DSLogic Plus grounds as USB-host referenced. Connect them only to
   common circuit GND, and never attach CK or TI to a 5 V signal.
- Use the documented DSLogic Group A-D captures. Qualification requires
   16-channel, 100 MHz Buffer Mode; 16-channel Stream Mode is limited to 20 MHz,
   and no 16-channel capture proves complete address, complete data, and all
   controls simultaneously.
- Do not trust package suffixes, TO-92 lead order, breadboard row geometry, or active-low polarity from memory. Verify the exact datasheet and current placement table.
- Do not use CMake Tools extension failures as evidence that the project cannot build; use the terminal workflow in the validation reference.

## Firmware Flashing and USB Supervisor Tools

Use the maintained Python tools in `scripts/`; do not write inline Node.js,
Python, or shell serial clients for each flash. Section 7 of the engineering
docs owns the complete setup and provisioning workflow.

```sh
python3 -m venv .venv-flash &&
. .venv-flash/bin/activate &&
python -m pip install -r scripts/requirements-flash.txt
```

For each invocation, activate and check dependencies in the same shell:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python scripts/pico_flash.py firmware \
  build/src/stage09_flash_storage/z80_stage09_cpm_loader.uf2 \
  --power-confirmed
```

- Create/install the environment only if absent or its dependencies are missing.
- Each tool call starts a fresh shell. Always activate `.venv-flash` and perform
  the import preflight in the same command as flashing or serial access, chained
  with `&&`. The printed interpreter must be inside this repository's
  `.venv-flash`; do not assume earlier activation persists or use another venv.
  If the check fails, stop before hardware access and diagnose the interpreter
  and dependencies. Do not run tests as an import-failure workaround.
- Flash only after the user authorizes deployment and saves CP/M work; never
  interrupt an active hardware test. External +5 V must remain applied.
- The flash command discovers a unique Pico, gracefully disconnects picocom
  using a verified PID and SIGTERM, then confirms a clean stop/flush by default.
  This project preference authorizes disconnecting picocom for an authorized
  flash, not arbitrary serial owners or unsaved application data loss.
- Never assume `/dev/cu.usbmodem1101`, any other port path, or a fixed board
  serial. With multiple Picos select `--serial DEVICE_SERIAL` from `ports`.
  Standalone supervisor discovery can select that serial even if its port changes.
- Unknown owners, stop timeouts, and storage errors abort without a forced reset.
  `--no-disconnect` disables automatic picocom disconnection. `--stopped` skips
  stop/flush only when the operator already confirmed it.
  Automatic stop/flush speaks the currently running Stage 9 protocol, regardless
  of which phase UF2 will be written. For other running phases, obtain a
  stage-appropriate safe stop first and use `--stopped`; do not probe unknown
  firmware with Stage 9 commands.
- For an already-BOOTSEL device, use `--bootsel --stopped --serial DEVICE_SERIAL`;
  the clean stop must have been confirmed before entering BOOTSEL.
- The tool rejects malformed UF2s and payloads outside the firmware region.
  It uses verified picotool loading, preserves the disks/boot/journal/ENV, and
  never reboots after a failed write or verification.
- Flash success means the firmware bytes were programmed and read-back verified.
  The flash command does not inspect USB reappearance, a CP/M prompt, banners or
  supervisor health after reboot. It supports non-CP/M phase firmware too.
  Do not add boot checks to routine flashing or pass the removed `--check-boot`.
  Use standalone supervisor boot/status checks only when separately requested.
- `pico_supervisor.py help`, `status`, and `stop` are standalone Stage 9
  commands. They refuse occupied ports rather than disconnecting terminals.
  Serial clients always close on completion or failure.
- For disk-only updates use `pico_flash.py disks --drive B=IMAGE --drive C=IMAGE
  --power-confirmed` and the `flash-cpm-disks` skill. The command disconnects
  picocom, stops/flushes, checks the journal, converts validated Altair B-D inputs
  if needed and verifies only selected slots. The standing project preference is
  **no backups** on this non-production board; do not ask about or create backups
  for routine replacements. Drive A requires matching board-native system records.
- After changing these tools, run the following; tests do not flash or stop
  real hardware:

```sh
. .venv-flash/bin/activate &&
python -c 'import sys, serial; print("Using", sys.executable)' &&
python -m unittest discover -s scripts -p 'test_pico*.py' -v
```
