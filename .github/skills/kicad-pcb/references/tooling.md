# Tooling and file formats

## Languages and runtimes

| Surface | Language/runtime | Purpose |
| --- | --- | --- |
| Schematic generator | Python 3 + `kiutils==1.4.8` | Symbols, schematic, deterministic UUIDs, net manifest |
| PCB generator/checker | KiCad 10 bundled Python 3.9, `wx`, `pcbnew` SWIG API | Footprints, nets, placement, rules, cutouts, zones, DSN/SES, invariant checks |
| Router | Java 25 + Freerouting 2.4.1 | Four-layer autorouting from Specctra DSN to SES, with only F.Cu/B.Cu routable (In1.Cu GND and In2.Cu +5V planes) |
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

## Router container

Use local KiCad for generation/DRC and the repository's router-only Apple
container for Java 25 and Freerouting 2.4.1. This avoids depending on pinned
Ubuntu KiCad packages that may disappear from their repositories.

```sh
container build --file scripts/freerouting/Containerfile \
  --tag z80-freerouting:2.4.1 scripts/freerouting

container run --rm --network none --cpus 4 --memory 6g \
  --mount "type=bind,source=$PWD/hardware/kicad/reports,target=/work" \
  z80-freerouting:2.4.1 --gui.enabled=false \
  --router.layers.routable=true,false,false,true \
  -da -de /work/z80_romless_sbc.dsn \
  -do /work/z80_romless_sbc.ses -mp 500 -mt 1
```

The Containerfile pins the Java base-image digest and router JAR SHA256.
Routing has no network access; `-da` also disables analytics. Do not pass
`--analytics.enabled=false`, which is not supported by this router version.
Exit code zero does not establish a complete route: import the SES, refill
zones and require zero KiCad DRC violations and unconnected items.

The committed SES is authoritative because multithreaded autorouting is not
deterministic. A new route must be reviewed, imported, validated, and committed
as a new intentional artifact.

Run placement DRC with a same-basename `.kicad_pro` beside the board. A
temporary board without its project uses KiCad defaults rather than the
generated rules and can report spurious edge-clearance failures.

## Format flow

```text
Python/kiutils -> .kicad_sch + net_manifest.json
KiCad pcbnew   -> unrouted .kicad_pcb + normalized .dsn
Java router    -> .ses using F.Cu and B.Cu
KiCad pcbnew   -> routed .kicad_pcb + filled In1.Cu GND / In2.Cu +5V planes
                  + stack-up S-expression
kicad-cli      -> ERC/DRC/stats + Gerber/Excellon/BOM/IPC/PDF/SVG/PNG
```

Normalize the DSN header so it contains no developer-local absolute path.
Export DSN before importing SES; otherwise the "router input" will accidentally
contain finished wiring.

`pcbnew.SaveBoard()` can update `.kicad_pro`. The generator explicitly writes
the desired project rules afterward, and the canonical pipeline verifies the
project file and its hash stability.
