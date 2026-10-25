# CP/M Image Provenance

The system boots **`64K CP/M 2.2`**: a Z80-optimised
port of Burcon CP/M 2.2 with a custom BIOS for this board.

## Original system

- **Source:** Burcon CP/M 2.2 for the Altair 88-DCDD controller.
- **Reference image:** `src/cpm/cpm64_system.bin`, the 5,632-byte CCP/BDOS
  region captured after the matching Burcon `MOVCPM 64` and `SYSGEN` tools
  produced a bootable system in the Altair emulator.
- **Integrity:** The image builder checks its SHA-256 fingerprint. This
  reference binary is preserved unchanged.
- **Original layout:** CCP at `0xE300`, BDOS at `0xEB00`, and BIOS at
  `0xF900`, with a `0x0700`-byte BIOS reservation.

To regenerate the reference, use the matching
[Burcon utilities](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/)
and [MOVCPM instructions](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/Using%20MOVCPM.pdf).
The generator reports `SAVE 38`; preserve the BIOS reservation and verify
relocated entry vectors.

## Changes for this board

- **Z80 CP/M port:** `src/cpm/cpm64_z80.asm` replaces the original CCP/BDOS
  at build time. Z80 instruction optimisations reclaim 263 bytes while
  preserving section sizes; the sections move upward by `0x0400`.
  See the [memory map](memory-map.md) for the running system's layout.
- **Custom BIOS:** `src/cpm/z80_bios.asm` replaces the Altair BIOS and cold
  loader. It uses the board's Pico-serviced terminal and flash-disk ports,
  rather than the Altair controller's ports `0x08`–`0x0A`.
  See [Custom BIOS](bios.md).
- **Validation:** The port's unoptimised mode reproduces the reference
  byte-for-byte. Host tests check relocation and Z80 instructions, plus
  cold boot, `DIR`, program loading, and warm boot.
- **Audit details:** `src/cpm/cpm64_i8080.asm` and
  `src/cpm/cpm64_i8080_audit.md` record the original disassembly and findings.

For supplied applications, DCC builds, and disk deployment, see
[Related projects](../related-projects.md).
