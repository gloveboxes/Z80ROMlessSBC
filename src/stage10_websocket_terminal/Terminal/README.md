# Phase 10 browser terminal

Based on the updated `esp32-altair-8800/terminal` web terminal (version 3.2.3),
with the Pico transport and CP/M adaptations retained. The refreshed UI uses
80 columns by 30 rows, a separate right-side scrollbar strip, styled scrollbars,
and horizontal terminal scrolling on narrow screens rather than clipping columns.
The xterm.js UI uses the page's host (or `?altair=<pico-ip>`) and fixed Pico
port **8088** for a root-path WebSocket, not the ESP32 `/ws` endpoint.
This also works when the HTML is opened locally or served on another port.
The emulator-only CPU Monitor button is now Enter; Ctrl-M sends carriage
return, and Page Up/Down match Phase 9's CP/M mappings.

The page speaks the version-1 acknowledged binary terminal protocol described
in the [Phase 10 guide](../../../docs/docs/en/implementation/phase-10-websocket.md).
It is not a raw-byte WebSocket client. Output is acknowledged after xterm
consumes it; input is sent in 64-byte frames only as the Pico acknowledges
queue acceptance. Retries use the same sequence number to prevent duplicate
input/output. Up to 64 KiB of unconfirmed input is buffered; excess new input
is rejected with a visible error. Unconfirmed bytes are cleared, never replayed,
on disconnect. Reload the page after updating the firmware.

The page loads xterm.js and the fit addon from public CDNs; xterm CSS is inline.
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
