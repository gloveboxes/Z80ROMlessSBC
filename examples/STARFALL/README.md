# STARFALL: Z80-driven WebGL game

Catch ten yellow stars with the cyan paddle before missing three. The Z80 runs
the game logic; an xterm.js addon renders a separate WebGL canvas through the
same acknowledged console connection. Green bars count catches; red bars show
remaining lives. No bank-1 library or sound hardware is required.

## Build and run

From this directory:

```sh
DCC_DIR=/path/to/dcc /path/to/dcc/dccmake
cp build/STARFALL.COM STARFALL.COM
```

From the repository root, install the built game into the source drive B:

```sh
python3 src/disks/refresh_drive_b.py --program STARFALL.COM
```

Build the terminal firmware with `cmake --build build --target
z80_stage10_websocket_terminal -j`. Deploy firmware and drive B using the
[maintained deployment tasks](../../docs/docs/en/system/firmware-build.md#system-and-storage-deployment),
then reload the browser terminal and run **`B:STARFALL`**. Building does not flash
anything. The interactive launcher also rebuilds and stages STARFALL when DCC
is available; its text-only terminal does not render graphics.

Use **A/D**, **Left/Right**, or the terminal's on-screen arrow pad.
**Q**, **Esc**, **Ctrl+C**, or **Stop game (Q)** quits. Click the terminal or
canvas for keyboard focus. Movement uses key repeat, not key-up state.
Keyboard input is polled continuously. The paddle and stars update every 50 ms
using stopwatch 0 on port `17h`;
slow transport reduces the update rate rather than queuing catch-up frames.
This is not a measured hardware frame-rate guarantee.

Opening waits up to three seconds for a graphics-terminal reply. A missing
addon, unavailable WebGL context, cancellation or timer failure reports an
error and returns to CP/M. After a disconnect, reconnect and stop/restart the
application; graphics state is not replayed.

## Reusable C API

Compile [GFX.C](GFX.C) with an application and include [GFX.H](GFX.H):

```c
if (gfx_open(320, 240)) {
    gfx_clear(GFX_BLACK);
    gfx_line(0, 0, 319, 239, GFX_YELLOW);
    gfx_sprite(0, 48, 8, GFX_LIGHT_CYAN);
    gfx_move(0, 136, 220, 1);
    gfx_present();
    /* Poll application input, then close when finished. */
    gfx_close();
}
```

`gfx_rect` draws filled rectangles. Sprites are retained **solid rectangles**,
not uploaded images; defining a sprite hides it until moved with visibility 1.
`gfx_clear` clears static shapes but retains sprites. `gfx_present` submits the
whole scene as one GPU draw. Keep the application running between present and
close to view it. Text remains in the normal terminal.

The API owns stopwatch 0 and console input during open negotiation.
`gfx_millis()` returns elapsed milliseconds or `FFFFFFFFh` on timer failure.
Graphics commands and `gfx_write()` use BDOS function 6 output: ordinary
cooked console output can interpret the arrow pad's Ctrl-S as XOFF.
Use `gfx_write()` for ASCII status text during play.
The API uses a fixed 96-byte command buffer, no heap, and a 512-byte application
stack. It is synchronous and non-reentrant. Coordinate/palette errors are
reported visibly by the browser and close the graphics pane.

## Wire protocol

The [browser addon](../../src/stage10_websocket_terminal/Terminal/graphics.js)
registers private OSC **777** with xterm.js. Each command is printable ASCII:

```text
ESC ] 777 ; Z80G ; 1 ; operation [ ; decimal arguments ... ] ESC \
```

The `Z80G;...` payload is at most 128 characters. Integers are unsigned,
canonical decimal with at most four digits. xterm assembles sequences across
WebSocket frames; the normal consumption ACK still applies. An ACK means
parsed/submitted, not that the display has presented a frame.

| Operation | Arguments | Effect |
| --- | --- | --- |
| `open` | width;height | New black canvas, 64..640 by 64..480 |
| `close` | none | Hide canvas and release the scene/GPU resources |
| `clear` | ink | Set background and remove static shapes |
| `rect` | x;y;width;height;ink | Append a filled rectangle |
| `line` | x1;y1;x2;y2;ink | Append a one-pixel-wide line |
| `sprite` | id;width;height;ink | Define/replace a hidden solid sprite |
| `move` | id;x;y;visible | Position sprite; visibility is 0 or 1 |
| `present` | none | Draw background, static shapes, then sprites |

Coordinates originate at the top left. Shapes/sprites must fit wholly inside
the canvas. There are at most **256 static shapes** and **32 sprites**, IDs
0..31; sprite draw order follows definition order. Palette indices 0..15 use
the colours named in GFX.H. A zero-length line draws one pixel.
Open replies through normal terminal input with **`Z80G1` + CR** on success or
**`Z80G0` + CR** on rejection. No other command replies.

Invalid private commands fail closed with a visible error; unrelated OSC
payloads are left to xterm. Close, terminal reset (`ESC c`), disconnect, stop
and disposal clear graphics state. WebGL context loss reports an error and
requires restarting the game. The addon accepts neither executable code nor
application-supplied shaders. Ordinary VT100 applications retain their text
renderer and existing key mappings.

## Checks

From the repository root:

```sh
node --test src/stage10_websocket_terminal/terminal.test.mjs \
  src/stage10_websocket_terminal/graphics.test.mjs
python3.14 src/common/tests/emulator_z80_test.py
python3.14 src/common/tests/starfall_z80_test.py
```

The game regression executes the built COM on the C Z80 emulator with the
production CP/M/BIOS and simulated timer/graphics replies. It checks negotiation,
failure, paddle bounds, quit, win and loss. This does not qualify Wi-Fi latency
or physical hardware.
