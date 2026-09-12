# Tooling and file formats

## Languages and runtimes

| Surface | Language/runtime | Purpose |
| --- | --- | --- |
| Schematic generator | Python 3 + `kiutils==1.4.8` | Symbols, schematic, deterministic UUIDs, net manifest |
| PCB generator/checker | KiCad 10 bundled Python 3.9, `wx`, `pcbnew` SWIG API | Footprints, nets, placement, rules, cutouts, zones, DSN/SES, invariant checks |
| Router | Java 25 + Freerouting 2.4.1 | Four-layer autorouting from Specctra DSN to SES, with In1.Cu disabled for the GND plane |
| Pipeline | Bash + `kicad-cli` | ERC/DRC, netlists, BOM, drawings, renders, Gerber/drill/IPC exports |
| Entry point | npm script | `npm run kicad` invokes the Bash pipeline |

Node.js does not implement the PCB; npm is only the repository task entry
point.

## KiCad Python APIs used

`scripts/build-kicad-pcb.py` uses:

- `pcbnew.BOARD`, `LoadBoard`, `SaveBoard`
- `FootprintLoad`, `FOOTPRINT`, `PAD`
- `NETINFO_ITEM`, `NETCLASS`, `NET_SETTINGS`
- `PCB_SHAPE`, `PCB_TEXT`, `PCB_TRACK`, `PCB_VIA`
- `ZONE`, `ZONE_FILLER`
- `ExportSpecctraDSN`, `ImportSpecctraSES`
- board connectivity, design settings, netclass, track, zone, and footprint
  inspection APIs

Create `wx.App(False)` before importing `pcbnew`. On macOS, a typical
interpreter/module pairing is:

```sh
export KICAD_PYTHON=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
export PYTHONPATH=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/lib/python3.9/site-packages
```

Use `KICAD_FOOTPRINT_DIR` when the standard footprint root is not discoverable.

## Router command

The committed route was generated with Freerouting 2.4.1 and Java 25:

```sh
java -jar freerouting-2.4.1.jar \
  --gui.enabled=false \
  --router.layers.routable=true,false,true,true \
  -de hardware/kicad/reports/z80_romless_sbc.dsn \
  -do hardware/kicad/reports/z80_romless_sbc.ses \
  -mp 500 -mt 1
```

The committed SES is authoritative because multithreaded autorouting is not
deterministic. A new route must be reviewed, imported, validated, and committed
as a new intentional artifact.

## Format flow

```text
Python/kiutils -> .kicad_sch + net_manifest.json
KiCad pcbnew   -> unrouted .kicad_pcb + normalized .dsn
Java router    -> .ses using F.Cu, In2.Cu and B.Cu
KiCad pcbnew   -> routed .kicad_pcb + filled In1.Cu GND plane
kicad-cli      -> ERC/DRC/stats + Gerber/Excellon/BOM/IPC/PDF/SVG/PNG
```

Normalize the DSN header so it contains no developer-local absolute path.
Export DSN before importing SES; otherwise the "router input" will accidentally
contain finished wiring.

`pcbnew.SaveBoard()` can update `.kicad_pro`. The generator explicitly writes
the desired project rules afterward, and the canonical pipeline verifies the
project file and its hash stability.
