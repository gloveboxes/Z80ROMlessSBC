# Z80 ROMless SBC

This repository contains a work-in-progress engineering specification and
reference implementation for a ROMless Z80 computer supervised by a Raspberry
Pi Pico 2 W. The design has not yet been physically built or qualified; treat
all electrical and timing claims as proposed until the staged bench tests pass.

## Documentation

- [Browse the MkDocs source](docs/docs/en/index.md)
- [Read the published documentation](https://gloveboxes.github.io/Z80ROMlessSBC/)
- [Follow the staged implementation plan](docs/docs/en/implementation/index.md)
- [Review the KiCad schematic](hardware/kicad/exports/z80_romless_sbc.pdf)

The documentation covers the component inventory, pin maps, voltage domains,
breadboard construction, bus arbitration, firmware architecture, CP/M storage,
DHO814/DSLogic Plus measurements, and the complete phase-by-phase bring-up
procedure.

## Build

Configure and build the Pico firmware and CP/M artifacts from the repository
root:

```sh
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico2_w \
  -DZ80_WIFI_SSID='your-network' \
  -DZ80_WIFI_PASSWORD='your-password'
cmake --build build --target z80_cpm_images -j
```

See [Firmware and CP/M build](docs/docs/en/system/firmware-build.md) for
prerequisites and provisioning, and [Building the documentation](docs/docs/en/development/building-docs.md)
for MkDocs and local preview commands.
