# D.1 CP/M Image Provenance and Machine Boundary

The resident CCP and BDOS come from the Burcon CP/M 2.2 distribution for the
MITS Altair 88-DCDD controller. Its BIOS reservation is `0x0700` bytes, so a
64K generation places the CCP at `0xE300`, BDOS at `0xEB00`, and BIOS at
`0xF900`. The checked-in `src/cpm/cpm64_system.bin` is the 5,632-byte CCP/BDOS
RAM region captured after the matching Burcon `MOVCPM 64` and `SYSGEN` tools
produced a system that cold-booted successfully in the Altair emulator. Its
SHA-256 fingerprint is enforced by the image builder.

CP/M generators are machine-specific. To reproduce this artifact, use the
[Burcon CP/M 2.2 utilities](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/):
the archive's `MOVCPM.COM` and `SYSGEN.COM`, following its
[Using MOVCPM guide](https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM%202.2/Burcon%20CPM/Using%20MOVCPM.pdf).
The matching generator reports `SAVE 38` after `MOVCPM 64`. Preserve the
Burcon `0x0700` BIOS reservation and verify the relocated entry vectors before
accepting any regenerated CCP/BDOS image.

The fingerprinted binary has also been recursively disassembled as Intel 8080
source in `src/cpm/cpm64_i8080.asm`; its control-flow, opcode-compatibility, and
optimization findings are recorded in `src/cpm/cpm64_i8080_audit.md`. The
boot-proven binary remains immutable. The image builder assembles the separate
`src/cpm/cpm64_z80.asm` port, whose unoptimized generation mode reproduces the
reference byte-for-byte. The active port uses relative branches, ten guarded
`DJNZ` sites, `LDIR`, native `SBC HL,DE` subtraction, `SUB 2` fusion, a branch
inversion, branch threading, tail transfers, and register-use rewrites. It
reclaims 263 bytes while preserving the fixed section sizes and relocates them
upward by `0x0400`. Recursive discovery includes the BDOS cleanup block reached through a
synthetic stack return. Host tests independently verify relocation and native
instruction encodings, while `src/cpm/test_z80_debug_host.py` boots the image
and completes `DIR`, transient loading, and a BIOS warm reload through a model
of the SBC terminal and disk ports.

Only the CCP/BDOS and their memory ABI are reused. The Altair disk BIOS and
cold loader use ports `0x08`-`0x0A` and are reference material, not executable
code for this board. At image-build time they are replaced by the SBC-native
BIOS assembled from `src/cpm/z80_bios.asm`, which uses the Pico-serviced terminal
and flash-disk ports described in the
[terminal](../system/operation.md#62-terminal-io-over-pico-websocket) and
[storage](../system/operation.md#63-onboard-flash-cpm-disk-storage)
architecture pages.
It intentionally targets the Z80 rather than the 8080-compatible subset, using
relative branches, `DJNZ`, `BIT`, rotate/carry status tests, carry-to-mask
`SBC A,A`, `INIR`/`OTIR` block I/O, and `EXX` alternate-register banking in
`read_record`/`write_record` (safe because this system never enables
interrupts, so the shadow register set is otherwise unused). Build it with
`z80asm`.
