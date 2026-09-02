# D.6 Compatibility Boundaries

- Programs must fit within the `0x0100`-`0xE6FF` TPA after runtime, globals,
  heap, and stack requirements are included.
- The installed operating system is CP/M 2.2. Optional CP/M 3 or emulator-only
  BDOS extensions are not supplied by this BIOS/BDOS image. In particular,
  software should treat dcc's BDOS-105-backed clock functions as unavailable
  unless separately demonstrated on this target.
- CP/M filenames and disks retain CP/M 2.2 semantics: 8.3 names, FCB-based
  access, 128-byte logical records, and Ctrl-Z text-file conventions where the
  dcc API documents them.
- The terminal data port returns `0x00` if read while empty. Correct software
  must test status bit 0 first; BDOS and the custom BIOS already do so.
- Status bit 7 reports WebSocket client connection state to direct-port
  software, but CP/M and dcc do not require it for their standard console ABI.
- dcc's own emulator and physical-Z80 test history establishes general CP/M
  runtime portability, but it does not by itself qualify this board's Pico
  trap, custom BIOS, flash backend, or electrical timing.
