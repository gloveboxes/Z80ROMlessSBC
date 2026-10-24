{% include-markdown "../../../../src/stage06_sram_dma/README.md" %}

## VS Code tasks

Choose **Tasks: Run Task** in the Z80ROMlessSBC workspace.

| Diagnostic | Build task | Deployment task |
| --- | --- | --- |
| Phase 6 one-hour 20 MHz soak | **Build Pico - Phase 6 one-hour 20 MHz soak** | **Flash Pico - Phase 6 one-hour 20 MHz soak** |
| Phase 6 14-hour 20 MHz soak | **Build Pico - Phase 6 14-hour 20 MHz soak** | **Flash Pico - Phase 6 14-hour 20 MHz soak** |

Deployment tasks force-update without checking the previous firmware phase.
Complete this phase's hardware prerequisites and save work before flashing. See
[Pico tools](../tools/pico-tools.md#deployment-task-requirements).
