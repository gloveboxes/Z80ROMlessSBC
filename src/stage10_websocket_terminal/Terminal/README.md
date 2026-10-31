# Phase 10 browser terminal

Based on the updated `esp32-altair-8800/terminal` web terminal, now version 3.3.0,
with the Pico transport and CP/M adaptations retained. The refreshed UI uses
80 columns by 30 rows, xterm's overlay scrollbar kept in a separate right-side strip,
and horizontal terminal scrolling on narrow screens rather than clipping columns.
The xterm.js UI uses the page's host (or `?altair=<pico-ip>`) and fixed Pico
port **8088** for a root-path WebSocket, not the ESP32 `/ws` endpoint.
This also works when the HTML is opened locally or served on another port.
The **Keys** toolbar button toggles an on-screen cursor pad (arrows, Esc, Tab,
Space, Enter) for tablets without a keyboard. Arrows send the same WordStar
codes as the physical keys (Ctrl-E/S/D/X), held keys auto-repeat, and taps never
focus the terminal, so the tablet keyboard stays hidden. It defaults on for touch
devices and the choice is remembered. The pad floats semi-transparently over the
page, including the terminal, and turns opaque while touched. It starts over the terminal's bottom-right corner;
drag any part of the panel other than a key (grip bar, border or gaps) to move
it. Its position is remembered and kept on-screen after rotation. Esc and Enter live on the pad rather than
the toolbar. Ctrl-M sends carriage
return, and Page Up/Down match Phase 9's CP/M mappings.

The page speaks the version-4 acknowledged binary terminal protocol described
in the [Phase 10 guide](../../../docs/docs/en/implementation/phase-10-websocket.md).
It is not a raw-byte WebSocket client. Output is acknowledged after xterm
consumes it, using cumulative ACKs and at most four queued output frames
(1,024 bytes). Input is sent in 64-byte frames only as the Pico acknowledges
queue acceptance; a full Pico RX queue retains one pending frame and admits
it as soon as space is available. Retries use the same sequence number to prevent duplicate
input/output. Up to 64 KiB of unconfirmed input is buffered; excess new input
is rejected with a visible error. Unconfirmed bytes are cleared, never replayed,
on disconnect. Reload the page after updating the firmware; version-1 pages
are not compatible. Browser reconnect attempts are capped at five.

The page loads `@xterm/xterm` 6.0.0 and `@xterm/addon-webgl` 0.19.0 from the
jsDelivr CDN and renders with WebGL, falling back to the DOM renderer if WebGL
is unavailable. The xterm 6.0.0 CSS is inline.
The browser therefore needs Internet access on first use; the Pico serves the
HTML, not those external assets. The copied favicon is retained as an asset;
the current embedded HTTP handler only serves `/`.

## Updating

Edit [index.html](index.html), then build `z80_stage10_websocket_terminal`.
CMake automatically runs [embed_html.py](../embed_html.py), producing a
deterministic gzip byte array in the build directory. Do not commit that
generated header.

The [update_static_html.sh](update_static_html.sh) tool retains the ESP32
asset-generation workflow but is adapted to this layout and invokes the same
deterministic generator. Do not copy the ESP32 generated header into the build:

```sh
./src/stage10_websocket_terminal/Terminal/update_static_html.sh
# Optional explicit output:
./src/stage10_websocket_terminal/Terminal/update_static_html.sh /tmp/static_html_hex.h
node --test src/stage10_websocket_terminal/terminal.test.mjs
```
