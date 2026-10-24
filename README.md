# Z80 ROMless SBC

This repository contains a work-in-progress engineering specification and
reference implementation for a ROMless Z80 computer supervised by a Raspberry
Pi Pico 2 W. The design has not yet been physically built or qualified; treat
all electrical and timing claims as proposed until the staged bench tests pass.

## Documentation

- [Browse the MkDocs source](docs/docs/en/index.md)
- [Read the published documentation](https://gloveboxes.github.io/Z80ROMlessSBC/)
- [Follow the staged breadboard implementation plan](docs/docs/en/implementation/index.md)
- [Review the KiCad schematic](hardware/kicad/exports/z80_romless_sbc.pdf)
- [PCB design, inventory, and fabrication](docs/docs/en/pcb/index.md)

The documentation covers the component inventory, pin maps, voltage domains,
breadboard construction, Z80-assisted loading, firmware architecture, CP/M storage,
DHO814/DSLogic Plus measurements, and the complete phase-by-phase bring-up
procedure.

Phases 0-10 build the three-BB830 breadboard prototype. The PCB is a separate
physical implementation of the **previous circuit**, left unchanged for now.
Do not manufacture its existing Gerbers for this redesign. The current
eight-package breadboard circuit uses fixed HCT32 logic and Z80-executed RAM
loading, with no PLD programmer or address expander.

## Related projects

Build C applications, package CP/M disks, and run them on this Z80 computer.
The supplied disks already include a default set of applications.

<table>
  <tr>
    <td width="50%" valign="top">
      <a href="https://davidly.github.io/dcc/">
        <img src="docs/docs/en/images/projects/dcc.svg" width="64" height="64" alt="DCC C Compiler" /><br />
        <strong>DCC C Compiler</strong>
      </a>
      <p>Build Z80 CP/M applications on Windows, macOS, or Linux. C89 with selected C99/C11 features.</p>
      <a href="https://davidly.github.io/dcc/">Explore DCC ↗</a>
    </td>
    <td width="50%" valign="top">
      <a href="https://gloveboxes.github.io/Altair-8800-AI-Edition/">
        <img src="docs/docs/en/images/projects/altair.svg" width="64" height="64" alt="Altair 8800 AI Edition" /><br />
        <strong>Altair 8800 AI Edition</strong>
      </a>
      <p>Explore CP/M applications and build Altair disk images for deployment to this project's Pico flash storage.</p>
      <a href="https://gloveboxes.github.io/Altair-8800-AI-Edition/">Explore Altair ↗</a>
    </td>
  </tr>
</table>

See the [application and disk workflow](https://gloveboxes.github.io/Z80ROMlessSBC/related-projects/)
when you reach Phases 9–10, after completing the earlier hardware bring-up tests.

## Build

Configure and build the Pico firmware and CP/M artifacts from the repository
root:

```sh
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico2_w
cmake --build build --target z80_cpm_images -j
```

Initialize the WebSocket library with `git submodule update --init --recursive`
before configuring. Phase 10 builds on the Phase 9 automatic loader.
Wi-Fi credentials and the remote-FS server address are provisioned over USB
with Ctrl-] n and stored in ENV, not compiled into firmware. The browser
terminal takes over CP/M while connected and returns it to USB on disconnect.

See [Firmware and CP/M build](docs/docs/en/system/firmware-build.md) for
prerequisites and provisioning, and [Building the documentation](docs/docs/en/development/building-docs.md)
for MkDocs and local preview commands.
