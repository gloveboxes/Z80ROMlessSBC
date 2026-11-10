# SOUND: speaker engine tour

A self-checking tour of the port `0x15` speaker through the `snd_*` API in
[DXSVC](https://github.com/gloveboxes/esp32-altair-8800/tree/main/Apps/COMMON).
It plays all ten effect primitives (including a do-re-mi note sequence), all
63 presets, scaled playback, and the priority, queue, live-update, clear and
stop feedback paths. Each request's status result is checked; the summary
reports engine completed/rejected counts and check failures. Any key stops.

Repeating presets are held with countdown timer 0 on port `0x17` while the
engine keeps playing, so the program never times sounds itself.

Build from this folder:

```sh
DCC_DIR=/path/to/dcc DXSVC_DIR=/path/to/esp32-altair-8800/Apps/COMMON \
  /path/to/dcc/dccmake
```

The interactive launcher rebuilds `SOUND.COM` when it finds the DXSVC SDK
(`DXSVC_DIR`, or a sibling `esp32-altair-8800` checkout) and installs it on
drive B. Run `B:SOUND` in the host emulator to hear it through the computer's
speakers, or on Stage 10 with the
[ATtiny85 speaker wired to the OLED bus](../../docs/docs/en/implementation/phase-10-websocket.md#attiny85-speaker).
