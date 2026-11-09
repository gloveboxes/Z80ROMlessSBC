# Firmware Flashing and USB Supervisor Tools

Run commands from the repository root.

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
