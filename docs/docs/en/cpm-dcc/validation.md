# D.7 Validation Status and Required Qualification

The host regression suite assembles the custom BIOS and verifies its placement,
the CCP/BDOS fingerprint, page-zero reset target, boot-package headers and CRCs,
native disk construction, CP/M geometry, and complete 4 MiB flash layout. These
are structural and reproducibility checks; they do not execute dcc programs
through this BIOS or measure a physical I/O cycle.

Separately, the source Burcon system has been cold-booted in the Altair host
emulator as 64K CP/M, reached the `A>` prompt, and completed `DIR`. That test
validates the relocated CCP/BDOS artifact and Burcon memory ABI, but it executes
the Altair BIOS rather than this board's custom BIOS. It therefore does not
validate the Pico I/O trap, virtual terminal and disk ports, flash persistence,
or Z80 bus timing.

The design remains unvalidated in hardware as stated in the
[Overview](../index.md#overview). Before claiming dcc compatibility on the
completed machine, perform [Phase 8](../implementation/phase-8-virtual-io.md),
[Phase 9](../implementation/phase-9-flash-storage.md), and
[Phase 10](../implementation/phase-10-websocket.md), then run representative
dcc `.COM` programs that cover:

1. `printf`/`puts`, literal `$`, CR/LF conversion, buffering, and `fflush`.
2. `getchar`, `scanf`, `kbhit`, `getch`, Ctrl-Z, and browser reconnect behavior.
3. File create, close, reopen, sequential read/write, random access, rename,
   delete, directory updates, disk-full handling, and warm boot.
4. Transfers on drives A-D, including repeated writes followed by power-cycle
   recovery and byte-for-byte host comparison.
5. A dcc program near the TPA limit, confirming that stack/heap growth does not
  cross `0xE700` and that exit reliably reloads the CCP.

Passing those application tests, the existing all-LBA and fault-injection disk
plan, and the logic-analyzer requirements is the point at which console and
file-I/O compatibility may be described as proven on this hardware.
