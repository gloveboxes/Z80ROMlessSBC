# D.9 Interactive CP/M Test

The shared [Z80 CP/M container](https://github.com/gloveboxes/z80-cpm-container)
provides a banked emulator, browser terminal, audio and VS Code C debugging.
Apple silicon macOS, the Apple `container` CLI, CMake, Python 3 and the VS Code
C/C++ extension are required. Initialize submodules and start the container
service:

```sh
git submodule update --init --recursive
container system start
```

In VS Code, select **Interactive CP/M emulator** under Run and Debug, or run
**Run interactive CP/M emulator**. The equivalent command is:

```sh
python3 scripts/z80_container.py start
```

Run the VS Code task **Refresh CP/M container and session disks** to rebuild
the container and session disks without starting CP/M. The launcher also
prepares them when starting an interactive or debugging session. Preparation
generates disks in `build/cpm-session/` from `src/disks/native/` and this
checkout's CP/M and BIOS. Drive A's system records and AUTORUN are updated;
all other application files are preserved. It replaces the session disks: stop
the emulator first and save any changes you want to keep with
`python3 src/disks/save_session_disk.py D`, substituting A–D.

Open <http://127.0.0.1:8080/> for the browser terminal. Press Ctrl+C to stop.

## Debug a DCC application

Run `dccmake -g` in the active C file's application directory so that its
`.COM` and `.DBG` are in its `build/` directory. Select **DCC CP/M: Debug
active C file in Apple container** and press F5. The debugger installs the
program into a private copy of drive B and uses this checkout's CP/M and BIOS.
Use <http://127.0.0.1:8081/> for target output and keyboard input; continue
execution before entering input. Breakpoints and stepping use VS Code. MI
memory operations address the active bank.

These are emulator tests, not physical hardware qualification.
