#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
python_bin=${PYTHON:-python3}
if [[ "$python_bin" != /* ]]; then
  if [[ -x "$repo_root/$python_bin" ]]; then
    python_bin="$repo_root/$python_bin"
  else
    python_bin=$(command -v "$python_bin")
  fi
fi
kicad_dir="$repo_root/hardware/kicad"
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/z80-romless-sbc-kicad.XXXXXX")
temporary_netlist="$temporary_dir/netlist.xml"
temporary_board="$temporary_dir/check.kicad_pcb"
temporary_dsn="$temporary_dir/check.dsn"
staged_exports="$temporary_dir/exports"
staged_fabrication="$temporary_dir/fabrication"
staged_reports="$temporary_dir/reports"
mkdir -p "$staged_exports" "$staged_fabrication/gerbers" \
  "$staged_fabrication/drill" "$staged_reports"
trap 'rm -rf "$temporary_dir"' EXIT

if [[ -n "${KICAD_PYTHON:-}" ]]; then
  kicad_python="$KICAD_PYTHON"
elif "$python_bin" -c 'import pcbnew' >/dev/null 2>&1; then
  kicad_python="$python_bin"
elif [[ -x /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 ]]; then
  kicad_python=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
  export PYTHONPATH=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/lib/python3.9/site-packages${PYTHONPATH:+:$PYTHONPATH}
else
  echo "KiCad Python with pcbnew is required; set KICAD_PYTHON" >&2
  exit 1
fi

command -v kicad-cli >/dev/null
"$python_bin" "$repo_root/scripts/build-kicad-schematic.py"

cd "$kicad_dir"
kicad-cli sym upgrade --force z80sbc.kicad_sym
kicad-cli sch upgrade --force z80_romless_sbc.kicad_sch
"$kicad_python" "$repo_root/scripts/build-kicad-pcb.py" \
  --sync-project-rules
kicad-cli sch erc \
  --severity-all \
  --exit-code-violations \
  --format json \
  -o "$staged_reports/z80_romless_sbc-erc.json" \
  z80_romless_sbc.kicad_sch

kicad-cli sch export svg \
  --exclude-drawing-sheet \
  -o "$staged_exports" \
  z80_romless_sbc.kicad_sch
"$python_bin" -c \
  'from pathlib import Path; import sys; p=Path(sys.argv[1]); p.write_text("\n".join(line.rstrip() for line in p.read_text().splitlines()) + "\n")' \
  "$staged_exports/z80_romless_sbc.svg"
kicad-cli sch export pdf \
  -o "$staged_exports/z80_romless_sbc.pdf" \
  z80_romless_sbc.kicad_sch
kicad-cli sch export netlist \
  --format kicadsexpr \
  -o "$staged_reports/z80_romless_sbc.net" \
  z80_romless_sbc.kicad_sch
kicad-cli sch export netlist \
  --format kicadxml \
  -o "$temporary_netlist" \
  z80_romless_sbc.kicad_sch

"$python_bin" "$repo_root/scripts/check-kicad-netlist.py" \
  reports/net_manifest.json \
  "$temporary_netlist"
"$python_bin" "$repo_root/scripts/check-doc-interconnects.py"

routing_session=reports/z80_romless_sbc.ses
if [[ ! -f "$routing_session" ]]; then
  echo "missing committed routing session: $kicad_dir/$routing_session" >&2
  exit 1
fi
(
  cd "$temporary_dir"
  "$kicad_python" "$repo_root/scripts/build-kicad-pcb.py" --check
  "$kicad_python" "$repo_root/scripts/build-kicad-pcb.py" \
    --output "$temporary_board" \
    --dsn "$temporary_dsn"
)
if ! cmp -s "$temporary_dsn" reports/z80_romless_sbc.dsn; then
  echo "committed Specctra DSN is stale; regenerate the routed PCB" >&2
  exit 1
fi
kicad-cli pcb drc \
  --refill-zones \
  --severity-all \
  --exit-code-violations \
  --format json \
  -o "$staged_reports/z80_romless_sbc-drc.json" \
  z80_romless_sbc.kicad_pcb
kicad-cli pcb export stats \
  --format json \
  -o "$staged_reports/z80_romless_sbc-stats.json" \
  z80_romless_sbc.kicad_pcb

kicad-cli pcb export gerbers \
  --check-zones \
  --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts \
  -o "$staged_fabrication/gerbers" \
  z80_romless_sbc.kicad_pcb
kicad-cli pcb export drill \
  --excellon-units mm \
  --excellon-separate-th \
  --generate-map \
  --map-format pdf \
  --generate-report \
  --report-path "$staged_fabrication/drill/z80_romless_sbc-drill-report.txt" \
  -o "$staged_fabrication/drill" \
  z80_romless_sbc.kicad_pcb
kicad-cli pcb export pos \
  --side both \
  --format csv \
  --units mm \
  -o "$staged_fabrication/z80_romless_sbc-positions.csv" \
  z80_romless_sbc.kicad_pcb
kicad-cli sch export bom \
  --fields Reference,Value,Footprint,QUANTITY,DNP \
  --labels Refs,Value,Footprint,Qty,DNP \
  --group-by Value,Footprint \
  --exclude-dnp \
  -o "$staged_fabrication/z80_romless_sbc-bom.csv" \
  z80_romless_sbc.kicad_sch
kicad-cli pcb export ipcd356 \
  -o "$staged_fabrication/z80_romless_sbc.ipc" \
  z80_romless_sbc.kicad_pcb
kicad-cli pcb export svg \
  --mode-single \
  --fit-page-to-board \
  --exclude-drawing-sheet \
  --check-zones \
  --layers F.Cu,B.Cu,F.Silkscreen,Edge.Cuts \
  -o "$staged_exports/z80_romless_sbc-pcb.svg" \
  z80_romless_sbc.kicad_pcb
"$python_bin" -c \
  'from pathlib import Path; import sys; p=Path(sys.argv[1]); p.write_text("\n".join(line.rstrip() for line in p.read_text().splitlines()) + "\n")' \
  "$staged_exports/z80_romless_sbc-pcb.svg"
kicad-cli pcb export pdf \
  --mode-multipage \
  --check-zones \
  --layers F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,F.Fab,Edge.Cuts \
  -o "$staged_exports/z80_romless_sbc-pcb.pdf" \
  z80_romless_sbc.kicad_pcb
kicad-cli pcb render \
  --quality high \
  --background transparent \
  --width 1800 \
  --height 1400 \
  --rotate 45,0,-35 \
  -o "$staged_exports/z80_romless_sbc-pcb.png" \
  z80_romless_sbc.kicad_pcb

(
  cd "$staged_fabrication"
  rm -f z80_romless_sbc-gerbers.zip
  zip -q z80_romless_sbc-gerbers.zip gerbers/* drill/*.drl
)

for report in \
  z80_romless_sbc-erc.json \
  z80_romless_sbc.net \
  z80_romless_sbc-drc.json \
  z80_romless_sbc-stats.json
do
  cp "$staged_reports/$report" "reports/$report.new"
  mv "reports/$report.new" "reports/$report"
done

for directory in exports fabrication
do
  new_directory="$kicad_dir/$directory.new"
  old_directory="$kicad_dir/$directory.old"
  rm -rf "$new_directory" "$old_directory"
  cp -R "$temporary_dir/$directory" "$new_directory"
  if [[ -d "$kicad_dir/$directory" ]]; then
    mv "$kicad_dir/$directory" "$old_directory"
  fi
  if mv "$new_directory" "$kicad_dir/$directory"; then
    rm -rf "$old_directory"
  else
    [[ ! -d "$old_directory" ]] ||
      mv "$old_directory" "$kicad_dir/$directory"
    exit 1
  fi
done