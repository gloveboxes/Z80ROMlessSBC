# Z80 ROMless SBC DCC I/O Adapter

`z80sbc-io-adapter` is the project-owned, self-contained I/O-port library used
by `dcc-debug-host`. It combines the Z80 ROMless SBC native-disk protocol with
the complete host-compatible adapter from
`esp32-altair-8800/dcc_debug_io_adapter`: time, utility, weather, chat,
file-transfer, environment, interrupt-timer, shared-response, and terminal
input support. The copied Altair sources retain their MIT license in
[`LICENSE`](LICENSE).

## Build and test

```sh
cmake -S src/cpm/dcc_io_adapter -B build/cpm/dcc_io_adapter
cmake --build build/cpm/dcc_io_adapter
ctest --test-dir build/cpm/dcc_io_adapter --output-on-failure
```

The library is generated as:

- macOS: `build/cpm/dcc_io_adapter/libz80sbc-io-adapter.dylib`
- Linux: `build/cpm/dcc_io_adapter/libz80sbc-io-adapter.so`
- Windows: `build/cpm/dcc_io_adapter/z80sbc-io-adapter.dll`

Keep credentials in the ignored `assets/altair_env.txt`; the interactive
debugger automatically loads that file and adds the generated `DRIVE_A` through
`DRIVE_D` paths before starting the host. A clean clone falls back to the
tracked, credential-free `assets/altair_env.example.txt`. CMake likewise copies
the private file when present and otherwise copies the example to
`build/cpm/dcc_io_adapter/altair_env.txt`.

## Port map

| Ports | Direction | Function |
|---|---|---|
| `0x10`-`0x14` | IN/OUT | SBC native disk command, drive, LBA, and record data |
| `0x15` | OUT | SBC adapter activation |
| 24-31, 37-39, 41-44 | OUT | Altair time and timer requests |
| 24-30 | IN | Altair time and timer values |
| 45, 48, 49, 70 | OUT | Utility strings and host information |
| 46 / 47 | OUT / IN | OpenWeatherMap field selection and status |
| 52 | IN/OUT | Interrupt-timer rate and status |
| 60, 61 | IN/OUT | Host file-transfer protocol |
| 71, 72 | IN/OUT | Text-file-backed environment store |
| 120-124 | IN/OUT | OpenAI-compatible chat request, response, and status |
| 200 | IN | Shared NUL-terminated response buffer |

This mapping mirrors the authoritative switch in
`esp32-altair-8800/port_drivers/io_ports.c`. The optional Sense HAT extensions
from `altair_local` are intentionally excluded because they are not part of
that firmware port map.

The adapter environment file must define `DRIVE_A` through `DRIVE_D`, each
pointing to a 327,680-byte native image. The same file may contain `CHAT_*`,
`OWM_*`, and CP/M environment values. Chat and weather use libcurl when CMake
finds it; without libcurl their status/error interfaces remain available and
report that network support is unavailable rather than returning fabricated
data.

## ABI

The versioned C ABI is declared in
`include/dcc_debug_io_adapter.h`. A library exports one symbol:

```c
int dcc_debug_io_adapter_init(
    const dcc_debug_io_adapter_config_t *config,
    dcc_debug_io_adapter_t *adapter,
    char *error,
    size_t error_size);
```

Initialization receives the environment-file path, the debugger session's
native files root, and host interrupt services. It returns port input/output
callbacks and a required `close` callback. The host calls `close` before
unloading the library so adapters can stop threads and release resources.

The host-service table lets an adapter register interrupt providers without
linking against debugger internals. Poll callbacks run on the emulator thread;
`raise_interrupt` and `clear_interrupt` may be called through the supplied
service table.

ABI v2 also provides optional terminal input and poll callbacks. This adapter
uses them to normalize ANSI terminal keys to the CP/M control-key convention:

- Up/Down/Right/Left: Ctrl-E/Ctrl-X/Ctrl-D/Ctrl-S
- Insert/Delete: Ctrl-O/Ctrl-G
- Page Up/Page Down: Ctrl-R/Ctrl-V
- Backspace/Delete: Ctrl-H

A standalone Escape is emitted after a 30 ms grace period so it can be
distinguished from the start of an ANSI sequence. These mappings are adapter
policy; a generic host without this adapter passes terminal bytes through.

## Direct use

```sh
./build/cpm/dcc_debug_host/dcc-debug-host --interpreter=mi \
  --io-adapter ./build/cpm/dcc_io_adapter/libz80sbc-io-adapter.dylib \
  --env-file ./path/to/session.env
```

Omit `--io-adapter` to run the generic debugger with unmapped input ports
returning zero and output ports ignored.
