# D.9 Interactive End-to-End CP/M User Test

The repository includes an interactive launcher for manually exercising the
same optimized CP/M image, board BIOS, virtual-disk protocol, and native disk
images used by the automated DCC test.

Prerequisites are CMake, a C/C++ compiler, Python 3, and `z80asm`. In VS Code:

1. Open **Run and Debug**.
2. Select **Interactive CP/M emulator**.
3. Press **Start**.
4. Wait for the banner and `A>` prompt in the **Interactive CP/M emulator**
   terminal.
5. Type CP/M commands normally. Press `Ctrl+]` to detach and stop the session.

The pre-launch task builds the DCC-derived `z80-debug-host`, the complete
project-owned adapter in `src/cpm/dcc_io_adapter`, and the CP/M artifacts in
`build/cpm`. The **Interactive CP/M emulator** debug configuration always
passes that built adapter to `z80-debug-host`; no optional selection or external
Altair checkout is required. The launcher extracts the 64 KiB SRAM payload from
`z80boot.pkg`, starts it through the board BIOS, and attaches Drives A-D from
the generated 320 KiB images. A successful start displays:

```text
64K CP/M 2.2 - Burcon Z80 Edition

A>
```

The following manual sequence exercises progressively more of the deployed
software path:

```text
A>DIR
A>LS
A>C:
C>ATTNC11
C>A:PIP D:ATTNC11.COM=C:ATTNC11.COM
C>A:PIP D:ATTN.WTS=C:ATTN.WTS
C>D:
D>DIR
```

- `DIR` proves CCP command handling, BDOS directory access, BIOS record reads,
  the `0x10`-`0x14` disk-port protocol, and Drive A filesystem interpretation.
- `LS` loads and executes a transient program, exits through page zero, and
  exercises the BIOS warm-boot reload before returning to `A>`.
- `C:` and `ATTNC11` exercise drive selection and multi-extent program loading.
- The `PIP` commands exercise cross-drive reads, writes, directory updates, and
  persistence in the emulator's generated Drive D image.

The dynamic adapter preserves the SBC ports `0x10`-`0x15` and also mirrors all
active ports in the ESP32 Altair `port_drivers/io_ports.c` dispatcher:

| Ports | Host-emulated function |
|---|---|
| 24-31, 37-39, 41-44 | Time and timer requests |
| 24-30 | Time and timer input |
| 45, 48, 49, 70 | Utility strings and host information |
| 46-47 | OpenWeatherMap field request and status |
| 52 | DCC-host interrupt timer |
| 60-61 | Host file transfer |
| 71-72 | Text-file-backed environment variables |
| 120-124 | OpenAI-compatible chat request and response |
| 200 | Shared response-buffer input |

The host adapter intentionally does not add the `altair_local` Sense HAT
extensions because those ports are absent from the firmware source-of-truth
dispatcher. Weather and chat use libcurl when available and explicitly report
an unavailable/error state when network support or configuration is absent.
No API keys are stored in the repository.

Terminal input uses the same policy as `altair_local`: Up/Down/Right/Left map to
Ctrl-E/Ctrl-X/Ctrl-D/Ctrl-S, Insert/Delete map to Ctrl-O/Ctrl-G, Page Up/Page
Down map to Ctrl-R/Ctrl-V, and Backspace or Delete maps to Ctrl-H. A standalone
Escape is emitted after a 30 ms grace period, allowing ANSI cursor sequences to
be recognized without losing Escape as a CP/M command key.

This is a realistic end-to-end **software-path** test: it executes the optimized
CCP/BDOS and `z80_bios.asm` with the SRAM payload extracted from the packaged
boot artifact, while the BIOS accesses the same native disk-image format that
is provisioned into Pico flash. Disk writes are made to the generated files
under `build/cpm`; rerunning the preparation task regenerates those artifacts,
so copy out any test result that must be retained.

The launcher intentionally substitutes host files for the physical Pico flash
backend. It therefore does not validate Pico package/CRC handling, the physical
flash cache and journal implementation, power-loss recovery, the Pico-to-SRAM
DMA transfer, WAIT#/bus timing, voltage translation, or the assembled board.
Those remain [Phase 8](../implementation/phase-8-virtual-io.md),
[Phase 9](../implementation/phase-9-flash-storage.md), and
[Phase 10](../implementation/phase-10-websocket.md) hardware and firmware
qualification items.
