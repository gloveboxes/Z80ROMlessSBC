# Related projects

Two related projects provide a C development toolchain and a collection of
CP/M applications and disk images for this computer. Together, they offer a
path from C source on a modern development machine to applications running
under CP/M on this project's real Z80.

<div class="related-projects" aria-label="Explore related projects">
  <a class="related-project" href="https://davidly.github.io/dcc/">
    <span class="related-project__badge related-project__badge--dcc" aria-hidden="true">C</span>
    <span class="related-project__kind">Compiler &amp; toolchain</span>
    <strong class="related-project__name">DCC C Compiler</strong>
    <span class="related-project__description">Build C applications on your computer. Run them on a Z80 under CP/M.</span>
    <span class="related-project__link">Explore DCC <span aria-hidden="true">↗</span></span>
  </a>
  <a class="related-project" href="https://gloveboxes.github.io/Altair-8800-AI-Edition/">
    <span class="related-project__badge related-project__badge--altair" aria-hidden="true">8800</span>
    <span class="related-project__kind">Applications &amp; disk images</span>
    <strong class="related-project__name">Altair 8800 AI Edition</strong>
    <span class="related-project__description">Explore CP/M applications and build disk images for this Z80 computer.</span>
    <span class="related-project__link">Explore Altair <span aria-hidden="true">↗</span></span>
  </a>
</div>

The disks supplied with this project already include a default set of
applications. The system boots **`64K CP/M 2.2`**, a
custom version of the original Burcon CP/M 2.2 and BIOS, modified and optimised
for the Z80 and this board. You can use these supplied disks when you reach
the CP/M phases; building or importing additional applications is optional.

This is the application-development goal for the later stages of the project.
Start with the [staged hardware bring-up](implementation/index.md), completing
the documented electrical checks and tests in Phases 0–8 before proceeding
to CP/M disk deployment. Those phases establish correct power, clock, bus,
SRAM, and I/O operation on your board.

[Phase 9](implementation/phase-9-flash-storage.md) introduces flash-backed
disks, Z80 CP/M booting, and applications through the USB terminal.
[Phase 10](implementation/phase-10-websocket.md) adds the WebSocket terminal.
The deployment example below describes the completed Phase 10 system; follow
each phase's own firmware and validation instructions as you reach it.

## DCC C Compiler

The [DCC C Compiler](https://davidly.github.io/dcc/) builds Z80 CP/M `.COM`
applications on Windows, macOS, and Linux. It supports C89 with selected
C99/C11 features, making it possible to write C applications with modern
development tools for a CP/M target. Its documentation includes a
[C conformance guide](https://davidly.github.io/dcc/01-c-conformance/) describing
supported features and target exceptions.

DCC translates C source into M80 assembly, then uses M80 and L80 to assemble
and link the executable. Its `dccmake` command coordinates the build, and
its runtime provides console and file I/O and other C library facilities
suited to CP/M. See this project's [CP/M and DCC compatibility overview](cpm-dcc/index.md)
for the runtime environment provided by the Z80 ROMless SBC.

## Altair 8800 AI Edition

[Altair 8800 AI Edition](https://gloveboxes.github.io/Altair-8800-AI-Edition/)
includes a collection of CP/M/Z80 applications in its
[`Apps` folder](https://github.com/gloveboxes/Altair-8800-AI-Edition/tree/main/Apps).
The collection includes C applications built with DCC and tooling to package
applications into 512 KiB CP/M disk images for the Z80 ROMless SBC.

Once the earlier bring-up tests pass and you reach Phases 9–10, use the
following workflow to bring applications across from the Altair project.

### In the ESP32 Altair 8800 AI Edition project

1. Run **Run interactive CP/M emulator** in that project to edit its
   writable `disks/512kb/` base images. These are also the build-all input/output
   disks. File edits persist directly; finish the application and stop with
   Ctrl+C before running a host build or copy.
2. Stop the SBC emulator and save any wanted session changes too. In the
   Altair project's `Apps` directory, choose:

   ```sh
   # Build/install applications, then copy A-D.
   pwsh ./build-all.ps1 -c /path/to/Z80ROMlessSBC/src/disks/native

   # Copy current CP/M disk contents without rebuilding applications.
   pwsh ./build-all.ps1 -CloneOnly -c /path/to/Z80ROMlessSBC/src/disks/native
   ```

   Build-and-copy replaces built application files but preserves unrelated
   files. Copy-only keeps current file contents without rebuilding.
   The existing destination directory is overwritten, including D's database.
   Equivalent ESP32 tasks are **Altair: Build apps and copy disks to Z80 SBC**
   and **Altair: Copy current disks to Z80 SBC**; they prompt for that directory.

Copying is one-way. Changes in SBC session disks do not automatically update
the ESP32 base disks. ESP32 F5 debugging uses private snapshots; use its normal
start task for persistent CP/M edits. See the
[ESP32 editing and copy guide](https://gloveboxes.github.io/Altair-8800-AI-Edition/en/90-appendices/10-build-and-install-cpm-apps/#edit-base-disks-and-copy-to-the-z80-sbc).

### In the Z80ROMlessSBC project

These steps target the completed Phase 10 hardware and firmware setup. When
first bringing up Phase 9, use its
[USB CP/M and flash storage instructions](implementation/phase-9-flash-storage.md)
and complete its tests before moving on to Phase 10.

1. Open the **Z80ROMlessSBC project folder in VS Code** and run
   **Run interactive CP/M emulator** to use the copied disks. The launcher
   validates them and patches A with this checkout's CP/M, BDOS, BIOS and AUTORUN.
   B-D are copied unchanged. No import step is needed.
2. For hardware deployment, use **Build and flash Pico - Phase 10 firmware** from the same checkout
   before flashing the matching CP/M disks and boot package. Firmware-only
   deployment preserves storage.
3. Choose **Terminal → Run Task** and one of the disk deployment tasks below.
   All tasks use `src/disks/native/`, the same source as the emulator. The
   all-disks task validates A-D and rebuilds A plus the matching boot package.
   Follow the
   [deployment requirements](system/firmware-build.md#deployment-task-requirements).
4. With both the disks and Pico firmware deployed, boot CP/M on the real Z80
   and access the installed applications on the
   configured drives **A:** through **D:**.

The Pico serves the flash-backed disks while the Z80 executes CP/M and its
applications. See
the [disk provisioning details](system/firmware-build.md#72-flash-provisioning)
for how this works. The firmware uses native 524,288-byte images.

### VS Code disk deployment tasks

The tasks in [`.vscode/tasks.json`](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/.vscode/tasks.json)
use `src/disks/native/` for all-disk and individual-drive deployment:

| Task | Disks replaced | Source image |
| --- | --- | --- |
| **Flash Pico - All disks (A-D)** | A: through D: | A-D from `src/disks/native/`; A's system tracks rebuilt |
| **Flash Pico - Drive A** | A: | `drive_a_cpm63k.img` (system tracks rebuilt) |
| **Flash Pico - Drive B** | B: | `drive_b_bdsc.img` |
| **Flash Pico - Drive C** | C: | `drive_c_escape.img` |
| **Flash Pico - Drive D** | D: | `drive_d_blank.img` |

With both emulators stopped, copy updated images into `src/disks/native/`
using the filenames above. The A: tasks
regenerate the board-native boot disk; the B:–D: tasks flash the selected
prepared images with unchanged B-D contents. Every disk task validates source
and prepared filesystems before deployment. The tasks replace selected disks
without backups and preserve firmware and ENV settings. **All disks (A-D)** also
validates and installs the newly built boot package with Drive A. Individual
drive tasks preserve the installed boot package; Drive A checks compatibility.
Follow the task's confirmation prompt before deployment.

Drive **A:** also contains the boot system and needs the board-native BIOS
and matching boot package. Drives **B:** through **D:** can hold application
disks copied from the ESP32 project. See
[Phase 9: USB CP/M and flash storage](implementation/phase-9-flash-storage.md)
for the storage design and the
[disk media README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md)
for validation commands and the shared source-image mapping.
