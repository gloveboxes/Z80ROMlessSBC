# Z80 ROMless SBC KiCad sources

Revision B implements the current eight-package, Z80-assisted-loader circuit:
Pico 2 W, Z80, SRAM, HCT32, AHCT244, AHCT245, LVC245 and LVC244. SRAM A16
is driven by GP8 through U4. The design target is **20 MHz**, retaining the
project's bench-proven IC set; this PCB still requires its own qualification.

## Mechanical design

- **85 x 96 mm**, four-layer, 1.6 mm FR-4, socket-compatible through-hole parts.
  Stack-up: F.Cu signals / In1.Cu GND plane / In2.Cu +5V plane / B.Cu
  signals, written into the board file (7628 prepreg, 1.065 mm core).
- Four **2.7 mm NPTH holes for M2.5 standoffs**, one in each corner, with
  centres 4 mm from both adjacent board edges. Hole spacing is **77 x 88 mm**.
  Use standoffs/screw heads no wider than 5.4 mm overall within the
  footprint's 5.9 mm courtyard.
- Pico USB faces out of the left edge. Its antenna has a chamfered physical
  FR-4/copper cutout. Keep metal standoffs, wiring and enclosure material
  away from the antenna; mechanical clearance is not RF qualification.
- A2 reserves the complete **38 x 12 mm SSD1315 OLED** body at the upper right.
  The local footprint models the user-supplied
  [dimension drawing](https://ae-pic-a1.aliexpress-media.com/kf/Sdca8c02df9d7460298598704a34813faS.jpg):
  header centres 1.50 mm from the left edge, SDA 2.19 mm from the top, and
  2.54 mm pitch. Viewed from above, header order bottom-to-top is
  **GND, VCC, SCK, SDA**. Fit a 1x4 socket; verify its pins fit the 1.0 mm
  drills and its assembled height. Support the far end with an insulating
  spacer. No module mounting holes were dimensioned, so none are invented.
- J1 is the **onboard 21.6 x 12.7 mm USB-C power breakout** on the top
  edge, USB-C facing outward. Solder all six 2.54 mm header pins into the
  six 1.0 mm plated carrier holes. Only VBUS (+5 V) and GND are wired; CC2,
  D+, D- and CC1 have isolated pads for mechanical attachment. No module
  screw holes are used. The outline is scaled from the vendor dimension
  photo (pin row 1.36 mm from the rear edge; shell overhangs the board edge
  by about 1.5 mm), so **dry-fit the purchased module before fabrication**.
  Check USB cable access and insulate its underside; strain-relieve the
  cable.
  The 12 x 13.8 mm module description is not the selected part.
- J3 remains an optional auxiliary input/probe header:
  **pin 1 +5 V / pin 2 GND**. It shares J1's rail without source isolation:
  **connect only one supply at a time**. D1 supplies Pico VSYS; never join
  the main +5 V rail to Pico VBUS.
- J2 is the 1x4 expansion header: GP22/SDA, GP26/SCL, 3.3 V, GND. GP8 is
  dedicated to SRAM bank select and is not exposed.
  GP8 is reserved for bank select. Do not connect a second display with
  conflicting address/pull-ups while A2 is fitted.

The PCB uses **six bussed 10 kOhm arrays for all 40 pull branches**:
RN1/RN2 address pull-ups, RN3 Pico data pull-downs, RN4 control pull-ups,
RN5 four 3.3 V startup pull-ups and RN6 four startup pull-downs. RN5/RN6
replace breadboard R17-R24 electrically; the manifest records and validates
this mapping without changing the breadboard installation instructions.

## Sources and validation

- `scripts/build-kicad-schematic.py`: symbols, values, exact nets and manifest.
- `scripts/build-kicad-pcb.py`: placement, outline, cutout, pre-routes, rules,
  GND/+5V planes, stack-up and board invariants.
- `Z80SBC.pretty/` and `fp-lib-table`: project-local OLED and USB-C module
  attachment geometry.
- `reports/z80_romless_sbc.dsn`: normalized unrouted router input.
- `reports/z80_romless_sbc.ses`: authoritative accepted route.
- `reports/route-lengths.json`: measured copper, source-specific paths and
  layer changes, including actual bypass-to-supply-pin paths.

With the project closed, run from the repository root:

```sh
PYTHON=.venv-kicad/bin/python npm run kicad
```

The default is the full transactional pipeline: ERC, manifest and documented
wiring parity, exact PCB/session/DSN reproduction, DRC after zone refill,
route measurements, project-rule stability, drawings and fabrication.
Use `npm run kicad -- --schematic-only` only for schematic-only work; it does
not validate or update the PCB package.

KiCad's bundled Python is detected on macOS. Set `KICAD_PYTHON` and
`KICAD_FOOTPRINT_DIR` if needed. The schematic environment uses
`scripts/requirements-kicad.txt`; install dependencies only if missing.

## Routing with Apple container

The router-only image does not depend on Ubuntu KiCad package availability:

```sh
container build --file scripts/freerouting/Containerfile \
  --tag z80-freerouting:2.4.1 scripts/freerouting

KICAD_PYTHON=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
"$KICAD_PYTHON" scripts/build-kicad-pcb.py

container run --rm --network none --cpus 4 --memory 6g \
  --mount "type=bind,source=$PWD/hardware/kicad/reports,target=/work" \
  z80-freerouting:2.4.1 --gui.enabled=false \
  --router.layers.routable=true,false,false,true -da \
  -de /work/z80_romless_sbc.dsn -do /work/z80_romless_sbc.ses -mp 500 -mt 1

"$KICAD_PYTHON" scripts/build-kicad-pcb.py \
  --session hardware/kicad/reports/z80_romless_sbc.ses
PYTHON=.venv-kicad/bin/python npm run kicad
```

Before routing, run placement DRC: only unconnected items may remain.
Network access is disabled during routing; `-da` also disables analytics.
The image pins Java 25 and the Freerouting 2.4.1 JAR checksum.
Only F.Cu and B.Cu route signals. In1.Cu is the GND plane and In2.Cu the
+5V plane; both are exported as DSN planes. The Z80 and Pico clocks are
locked, via-free F.Cu preroutes with 0.30 mm Clock clearance.
The router receives 0.21 mm signal clearance to provide a 0.01 mm numerical
margin over KiCad's 0.20 mm requirement, not a relaxation of the rules.

## Manufacturing and qualification

`fabrication/z80_romless_sbc-gerbers.zip` contains four copper layers,
masks, silkscreens, Edge.Cuts, job information, and PTH/NPTH drills. BOM,
placement CSV, IPC-D-356, drill maps/reports, PDF/SVG drawings and 3D preview
are adjacent outputs. Inspect the archive and confirm the fabricator's
actual stack-up and internal-cutout process before ordering.

The generator enforces silkscreen strokes of at least **0.16 mm** (JLCDFM flags exactly 0.15 mm as a warning) and
clearance of at least **0.20 mm from every drill**, including vias, and
**0.20 mm from same-side pad copper** (plus any solder-mask expansion).
Labels are positioned clear of holes and pads; straight body-outline and
polarity-hatch strokes receive small generated gaps around holes and pads
without changing copper or removing polarity marks.
The full pipeline checks these dimensions as well as KiCad ERC/DRC.
Run `scripts/test_kicad_silkscreen.py` with KiCad's bundled Python for the
focused geometry regression tests. Re-run the manufacturer's DFM check on
each new upload; local checks are not a substitute for its production review.

Both clock paths are via-free. Current paths are 49.56 mm Pico-to-U4 and
62.16 mm U4-to-Z80. Bus matching is assessed by source/receiver path, not
total tree copper. See the [design review](../../docs/docs/en/pcb/design-considerations.md)
for measurements and the long raw-RD path requiring receive-pin timing review.

Zero ERC/DRC and exact regeneration do not qualify physical operation.
Start PCB bring-up at 1 MHz, then qualify up to 20 MHz using SRAM patterns,
bank switching, repeated boots, CP/M/I/O soak and receive-pin measurements.
Do not treat one 50 ns clock period as the complete Z80 memory-access window.

Apply J1 power (or J3 power, not both) before connecting Pico USB; disconnect
Pico USB before removing main power. Pico-USB-only operation of the populated
AHCT245 circuit is unsupported.
Inspect actual socket envelopes, OLED height, screw-head clearance and
electrolytic/diode polarity; the render lacks socket, OLED, USB-C breakout
and standoff models.
