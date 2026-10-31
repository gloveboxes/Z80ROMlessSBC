# Appendix D: CP/M BIOS and dcc Compatibility

This software reference is optional for hardware assembly. The
[host emulator](interactive.md) lets you try CP/M without a Pico; physical
buses, flash recovery, and timing require hardware testing.

The system runs 64K CP/M 2.2 with a custom BIOS assembled from
[src/cpm/z80_bios.asm](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/src/cpm/z80_bios.asm).
The BIOS maps standard console and disk operations to Pico-serviced I/O,
keeping the WebSocket terminal and flash storage transparent to applications.

[dcc](https://github.com/gloveboxes/dcc) produces compatible Z80 `.COM` programs.
Its runtime uses standard CP/M page-zero vectors; BDOS handles console and
file services, and the BIOS handles hardware transfers.

## In this section

For the compiler, C library, and CP/M APIs, see the
[DCC documentation](https://davidly.github.io/dcc/).

- [Image provenance and machine boundary](image-provenance.md)
- [CP/M memory map and entry points](memory-map.md)
- [Z80 I/O with the PIO engine](pio-io.md)
- [Custom BIOS responsibilities](bios.md)
- [Bank-1 fixed-address DCC library](bank1-library-spec.md)
- [Compatibility boundaries](boundaries.md)
- [Validation requirements](validation.md)
- [Automated end-to-end test](end-to-end.md)
- [Interactive CP/M test](interactive.md)
