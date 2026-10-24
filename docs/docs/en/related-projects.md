# Related projects

Two related projects provide a C development toolchain and a collection of
CP/M applications and disk images for this computer. Together, they offer a
path from C source on a modern development machine to applications running
under CP/M on this project's real Z80.

The disks supplied with this project already include a default set of
applications. The system boots **`64K CP/M 2.2 - Burcon Z80 Edition`**, a
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
applications into Altair 88-DCDD disk images. These images can also supply
applications for the Z80 ROMless SBC.

Once the earlier bring-up tests pass and you reach Phases 9–10, use the
following workflow to bring applications across from the Altair project.

### In the ESP32 Altair 8800 AI Edition project

1. Build the desired C application in the Altair project's `Apps` collection
   using its DCC build instructions, and create the corresponding 88-DCDD
   `.dsk` image.
2. Find the source disk images in the Altair project's
   `/esp32-altair-8800/disks` folder, then copy the desired `.dsk` image into
   `Z80ROMlessSBC/src/disks/source-altair/`.

### In the Z80ROMlessSBC project

These steps target the completed Phase 10 hardware and firmware setup. When
first bringing up Phase 9, use its
[USB CP/M and flash storage instructions](implementation/phase-9-flash-storage.md)
and complete its tests before moving on to Phase 10.

1. Open the **Z80ROMlessSBC project folder in VS Code**. Choose
   **Terminal → Run Task** and select one of the disk deployment tasks listed
   below. The task automatically prepares the copied disk images and installs
   them into the Pico's flash storage.
2. After deploying the disks, choose **Terminal → Run Task → Build and flash Pico - Phase 10 firmware** to build and deploy the Pico firmware for
   the Z80 boot process. This task preserves the disks you just installed.
3. With both the disks and Pico firmware deployed, boot CP/M on the real Z80
   and access the installed applications on the
   configured drives **A:** through **D:**.

The Pico serves the flash-backed disks while the Z80 executes CP/M and its
applications. Disk conversion and provisioning are handled by the dedicated
VS Code disk deployment tasks; no manual conversion commands are required. See
the [disk provisioning details](system/firmware-build.md#72-flash-provisioning)
for how this works. The firmware uses native 327,680-byte images rather than
the original 88-DCDD sector framing.

### VS Code disk deployment tasks

The tasks in [`.vscode/tasks.json`](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/.vscode/tasks.json)
use the following source images from `src/disks/source-altair/`:

| Task | Disks replaced | Source image |
| --- | --- | --- |
| **Flash Pico - All disks (A-D)** | A: through D: | All four images listed below |
| **Flash Pico - Drive A** | A: | `cpm63k.dsk` |
| **Flash Pico - Drive B** | B: | `bdsc-v1.60.dsk` |
| **Flash Pico - Drive C** | C: | `escape-posix.dsk` |
| **Flash Pico - Drive D** | D: | `blank.dsk` |

Copy an updated disk image using the filename expected by its task, or update
the task's source path to select a different application image. The A: tasks
regenerate the board-native boot disk; the B:–D: tasks convert the selected
Altair images at flash time. The tasks replace the selected disks without
backups and preserve firmware, the installed boot package, and ENV settings.
Follow the task's confirmation prompt before deployment.

Drive **A:** also contains the boot system and needs the board-native BIOS
and matching boot package. Drives **B:** through **D:** can hold application
disks converted from validated Altair images. See
[Phase 9: USB CP/M and flash storage](implementation/phase-9-flash-storage.md)
for the storage design and the
[disk media README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md)
for conversion commands, the default source-image mapping, and selective
installation of application disks.
