# Appendix D: CP/M BIOS and dcc Compatibility

For a first hardware build, this section is supporting software reference,
not a prerequisite for wiring. You can use the
[interactive host emulator](interactive.md) to become familiar with CP/M
before the boards are ready. It tests the software path without powering a
Pico; it does not qualify the physical buses, flash recovery, or timing.

This system runs CP/M 2.2 with a custom BIOS designed specifically for this
board. The image builder packages the 64K CP/M CCP and BDOS with the native
BIOS assembled from
[src/cpm/z80_bios.asm](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/z80_bios.asm).
The BIOS is the layer that translates standard CP/M console and disk
operations into this board's Pico-serviced virtual I/O ports. Applications do
not need to know that the terminal is a WebSocket or that disk records reside
in Pico flash.

[dcc](https://github.com/gloveboxes/dcc) targets CP/M 2.2 on the Z80 and emits
ordinary `.COM` programs linked with its Z80 runtime. That execution model is
compatible with this system: the dcc runtime enters CP/M through the standard
page-zero vectors, CP/M BDOS performs console and file-system policy, and the
custom BIOS performs the final hardware-dependent transfer.

## In this section

- [Image provenance and machine boundary](image-provenance.md)
- [CP/M memory map and entry points](memory-map.md)
- [Custom BIOS responsibilities](bios.md)
- [dcc console and file-I/O paths](console-files.md)
- [Direct CP/M, BIOS, and port access](direct-access.md)
- [Compatibility boundaries](boundaries.md)
- [Validation requirements](validation.md)
- [Automated end-to-end test](end-to-end.md)
- [Interactive CP/M test](interactive.md)
