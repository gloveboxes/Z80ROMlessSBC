# Appendix C: Source Code Index

Canonical repository: [github.com/gloveboxes/Z80ROMlessSBC](https://github.com/gloveboxes/Z80ROMlessSBC).
All phase applications are cumulative: each stage links the shared modules
proven by earlier stages, while remaining independently buildable for hardware
bring-up.

## Project Build Files

| File | Purpose |
| --- | --- |
| [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/CMakeLists.txt) | Pico SDK import, Pico 2 W target, project languages, and protected firmware flash linker boundary |
| [src/CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/CMakeLists.txt) | Shared firmware libraries, stage registration, and the `z80_cpm_images` artifact target |
| [.gitignore](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/.gitignore) | Generated build, Python cache, and assembler-output exclusions |
| [package.json](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/package.json), [package-lock.json](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/package-lock.json) | Repository utility commands, including generated breadboard layout |
| [MkDocs configuration](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/docs/docs/mkdocs.yml), [requirements](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/docs/docs/requirements.txt) | Documentation navigation, theme, plugins, and pinned Python dependencies |
| [documentation workflow](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/.github/workflows/docs.yml) | Strict MkDocs build and GitHub Pages deployment |

### Cumulative Stage Applications

| Phase | Application | Target definition | Responsibility |
| ---: | --- | --- | --- |
| 0 | [Power checklist](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage00_power/README.md) | Hardware-only | Empty-socket wiring, rail, resistance, and startup-state checks |
| 1 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage01_supervisor/CMakeLists.txt) | Safe supervisor startup and GPIO walk |
| 2 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage02_buffers_clock/CMakeLists.txt) | Buffered controls and variable-frequency clock |
| 3 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage03_mcp23s17/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage03_mcp23s17/CMakeLists.txt) | MCP23S17 register and port diagnostics |
| 4 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage04_address_bus/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage04_address_bus/CMakeLists.txt) | 16-bit address-bus drive, sample, and isolation tests |
| 5 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage05_data_bus/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage05_data_bus/CMakeLists.txt) | 8-bit data-bus drive, sample, and isolation tests |
| 6 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage06_sram_dma/CMakeLists.txt) | SRAM DMA and full-memory validation |
| 7 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage07_z80_cpu/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage07_z80_cpu/CMakeLists.txt) | Z80 reset, execution, clock, and BUSREQ/BUSACK tests |
| 8 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage08_virtual_io/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage08_virtual_io/CMakeLists.txt) | Virtual-ROM preload and synchronous I/O trapping |
| 9 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage09_flash_storage/CMakeLists.txt) | Manifest boot, journal recovery, and persistent flash disks |
| 10 | [main.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/main.c) | [CMakeLists.txt](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/CMakeLists.txt) | Final CP/M, flash-disk, Wi-Fi, and WebSocket integration |

### Shared Firmware Modules

| Module | Public interface | Implementation | Responsibility |
| --- | --- | --- | --- |
| Pin map | [pins.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/pins.h) | Header-only | Authoritative Pico GPIO assignments |
| Supervisor | [supervisor.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/supervisor.h) | [supervisor.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/supervisor.c) | Safe GPIO startup and fail-safe isolation defaults |
| Clock | [clock.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/clock.h) | [clock.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/clock.c) | PWM frequency selection, stop/resume, and single-cycle clocking |
| MCP23S17 | [mcp23s17.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/mcp23s17.h) | [mcp23s17.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/mcp23s17.c) | SPI register access and 16-bit address expansion |
| Buses | [bus.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/bus.h) | [bus.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/bus.c) | Contention-safe address/data direction, isolation, drive, and sample operations |
| SRAM | [sram.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/sram.h) | [sram.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/sram.c) | DMA byte access, image load/verify, and RAM diagnostics |
| CPU | [cpu.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/cpu.h) | [cpu.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/cpu.c) | Reset-held DMA, run control, bus ownership, and fail-closed state |
| I/O trap | [io_trap.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/io_trap.h) | [io_trap.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/io_trap.c) | Synchronous Z80 IN/OUT interception and fault counters |
| Flash disk | [flash_disk.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/flash_disk.h) | [disk_device.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/disk_device.c) | Z80-facing command/status, drive, LBA, and 128-byte data ports |
| Flash layout | [flash_layout.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/flash_layout.h) | Header-only | Firmware, journal, boot-package, and disk-slot offsets |
| Flash backend | [flash_backend.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/flash_backend.h) | [flash_backend.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/flash_backend.c) | Manifest validation, SRAM boot load, journal recovery, and core-1 commits |
| Terminal | [terminal.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/include/z80sbc/terminal.h) | [terminal_bridge.c](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_bridge.c), [terminal_network.cpp](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/common/terminal_network.cpp) | Queue-backed terminal ports, Wi-Fi lifecycle, HTTP, and WebSocket service |

### CP/M, Disk, and Web Tooling

| Area | Files | Purpose |
| --- | --- | --- |
| Native CP/M | [README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/README.md), [z80_bios.asm](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/z80_bios.asm), [build_images.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/build_images.py), [test_build_images.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/test_build_images.py) | 64K CCP/BDOS image, native BIOS, boot package, full-flash composition, and host regression tests |
| Dynamic debug I/O | [adapter README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/dcc_io_adapter/README.md), [adapter entry point](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/dcc_io_adapter/src/dcc_debug_io_adapter.c), [port dispatcher](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/dcc_io_adapter/host/PortDrivers/io_ports.c) | Project-hosted SBC disk adapter, complete Altair host port drivers, interrupt service, and ANSI terminal-input pipeline |
| Disk geometry and conversion | [README](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/README.md), [geometry.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/geometry.py), [convert_altair_disks.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/convert_altair_disks.py) | Shared CP/M geometry and deterministic Altair-media conversion |
| Generated disk artifacts | [generated directory](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/disks/generated), [manifest.json](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/generated/manifest.json) | Four exact 320 KiB intermediate disk slots and source/output hashes |
| Preserved source media | [source-altair directory](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/src/disks/source-altair), [altair_88dskrom.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/source-altair/altair_88dskrom.h), [altair_disk_loader.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/disks/source-altair/altair_disk_loader.h) | Original framed disks, Altair loader references, and upstream license |
| Browser terminal | [terminal.html](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/terminal.html), [embed_html.py](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/embed_html.py) | Embedded Stage 10 terminal client and build-time HTML conversion |
| Network configuration | [lwipopts.h](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/lwipopts.h), [wifi_config.h.in](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/stage10_websocket_terminal/wifi_config.h.in) | lwIP settings and build-time Wi-Fi credential template |
