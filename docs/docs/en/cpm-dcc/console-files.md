# D.4 dcc Console and File-I/O Paths

The normal dcc runtime paths are compatible without recompiling the runtime for
these port numbers:

| dcc application operation | Runtime and operating-system path |
| --- | --- |
| `printf`, `puts`, `putchar`, and stdout/stderr writes | dcc console buffering -> BDOS functions 9 and 2 -> BIOS `CONOUT` -> terminal ports |
| `getchar`, `scanf`, and stdin reads | BDOS function 1 -> BIOS `CONST`/`CONIN` -> terminal ports |
| `kbhit` and `getch` | BDOS direct-console function 6 -> BIOS console entries -> terminal ports |
| `fopen`, `fread`, `fwrite`, `fclose`, and related file calls | CP/M FCB operations in BDOS -> BIOS `READ`/`WRITE` -> flash-disk ports |
| Program exit or abort | Page-zero warm boot -> BIOS `WBOOT` -> system reload and CCP prompt |

dcc's console runtime performs CP/M-oriented character handling above the BIOS.
In particular, normal text input treats Ctrl-Z as end-of-file, converts an
entered carriage return to C `\n`, and supplies the accompanying line-feed
echo. Console output converts C `\n` to CR/LF. The BIOS deliberately transports
bytes and reports readiness; it does not duplicate those runtime policies.

The dcc runtime buffers output. A newline, explicit `fflush`, full buffer,
input operation, or program termination flushes it to the Pico.

The Pico's terminal transmit queue is also bounded. With no WebSocket client,
limited output can wait for later delivery; an output-heavy program blocks in
BIOS `CONOUT` when status bit 1 reports no room. The Z80 program blocks, not
the Pico's I/O trap, and a connected client allows transmission to resume.
