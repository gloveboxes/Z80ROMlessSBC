# D.7 Validation and Hardware Qualification

## Host coverage

- **Image tests:** Check the Burcon reference fingerprint, assembled CCP/BDOS
  and BIOS layout, page-zero reset target, package headers and CRCs, disk
  geometry, and complete 4 MiB flash layout.
- **DCC host test:** Executes the optimized image and board BIOS through
  emulated I/O, covering boot, `CLEAR`, `DIR`, `LS.COM`, warm reload,
  multi-extent loading, and cross-drive file copies.
- **Banked-memory tests:** Check the BIOS transfer ABI, bank isolation,
  allocator, and heap operations in Z80 emulation.

See the [automated tests](end-to-end.md) and
[interactive emulator](interactive.md). Host tests do not establish physical
bus timing, Pico I/O behavior, or flash power-loss recovery.

## Required hardware qualification

Complete [Phase 8](../implementation/phase-8-virtual-io.md),
[Phase 9](../implementation/phase-9-flash-storage.md), and
[Phase 10](../implementation/phase-10-websocket.md), including logic-analyzer,
all-LBA disk, and flash fault-injection checks. Run representative dcc `.COM`
programs covering:

1. **Output:** `printf`/`puts`, literal `$`, CR/LF, buffering, and `fflush`.
2. **Input:** `getchar`, `scanf`, `kbhit`, `getch`, Ctrl-Z, and browser reconnects.
3. **Files:** Create, close/reopen, sequential/random I/O, rename, delete,
   directory updates, disk-full handling, and warm boot.
4. **Persistence:** Read/write all drives A–D; repeat writes, power-cycle,
   and compare recovered data byte-for-byte with the host.
5. **Memory:** Exercise stack/heap near the TPA limit (`0xE600` exclusive)
   and confirm reliable exit and CCP reload.

Hardware console and file-I/O compatibility requires these checks to pass.
