# D.5 Direct CP/M, BIOS, and Port Access

dcc also exposes non-C89 target extensions for software that intentionally
bypasses part of the normal runtime:

| API | Compatibility on this system |
| --- | --- |
| `bdos()` / `bdoshl()` | Compatible for implemented CP/M 2.2 BDOS functions through `CALL 0x0005` |
| `bios()` / `bioshl()` / `biosreg()` | Compatible with the standard 17-entry BIOS jump table installed at `0xFD00` |
| `inp(port)` | Executes an 8-bit Z80 `IN`; suitable for reading the virtual terminal or disk ports |
| `outp(port, value)` | Executes an 8-bit Z80 `OUT`; suitable for writing the virtual terminal or disk ports |

Only the low eight bits of a dcc `inp` or `outp` port argument are significant,
which matches the supervisor's eight-bit I/O decode. Direct terminal code must
poll receive-ready before reading port `0x00` and transmit-room before writing
it. Direct disk code must follow the complete command/status and exact
128-byte-transfer protocol in the
[flash-storage architecture](../system/operation.md#63-onboard-flash-cpm-disk-storage).
Using BDOS is preferred for ordinary
console and file access because it preserves CP/M buffering, FCB, error, and
warm-boot behavior.

A normal dcc `.COM` file linked with the dcc runtime is not a standalone
bare-metal binary even if it uses `inp` and `outp`: startup, memory discovery,
file services, and exit still depend on CP/M page zero, BDOS, and BIOS. A truly
bare-metal dcc program would need a different startup/runtime arrangement and
is outside this compatibility claim.
