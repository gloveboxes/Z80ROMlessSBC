#!/usr/bin/env bash
# Validate the partial-SMT board and publish its JLCPCB fabrication and
# assembly package. Writes only under hardware/kicad-smt.
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

name=z80_romless_sbc_smt
kicad_dir="$repo_root/hardware/kicad-smt"
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/z80-romless-sbc-smt.XXXXXX")
temporary_netlist="$temporary_dir/netlist.xml"
temporary_board="$temporary_dir/check.kicad_pcb"
temporary_dsn="$temporary_dir/check.dsn"
staged_exports="$temporary_dir/exports"
staged_fabrication="$temporary_dir/fabrication"
staged_reports="$temporary_dir/reports"
mkdir -p "$staged_exports" "$staged_fabrication/gerbers" \
  "$staged_fabrication/drill" "$staged_fabrication/assembly" "$staged_reports"
trap 'rm -rf "$temporary_dir"' EXIT

strip_trailing_space() {
  "$python_bin" -c \
    'from pathlib import Path; import sys; p=Path(sys.argv[1]); p.write_text("\n".join(line.rstrip() for line in p.read_text().splitlines()) + "\n")' \
    "$1"
}

"$python_bin" "$repo_root/scripts/build-kicad-smt-schematic.py"

cd "$kicad_dir"
kicad-cli sym upgrade --force z80sbc_smt.kicad_sym
kicad-cli sch upgrade --force "$name.kicad_sch"
project_rules_before=$(shasum -a 256 "$name.kicad_pro")
kicad-cli sch erc \
  --severity-all \
  --exit-code-violations \
  --format json \
  -o "$staged_reports/$name-erc.json" \
  "$name.kicad_sch"
kicad-cli sch export svg \
  --exclude-drawing-sheet \
  -o "$staged_exports" \
  "$name.kicad_sch"
strip_trailing_space "$staged_exports/$name.svg"
kicad-cli sch export pdf -o "$staged_exports/$name.pdf" "$name.kicad_sch"
kicad-cli sch export netlist --format kicadsexpr \
  -o "$staged_reports/$name.net" "$name.kicad_sch"
kicad-cli sch export netlist --format kicadxml \
  -o "$temporary_netlist" "$name.kicad_sch"
"$python_bin" "$repo_root/scripts/check-kicad-netlist.py" \
  reports/net_manifest.json "$temporary_netlist"

routing_session=reports/$name.ses
if [[ ! -f "$routing_session" ]]; then
  echo "missing committed routing session: $kicad_dir/$routing_session" >&2
  exit 1
fi
(
  cd "$temporary_dir"
  "$kicad_python" "$repo_root/scripts/build-kicad-smt-pcb.py" --check
  "$kicad_python" "$repo_root/scripts/build-kicad-smt-pcb.py" \
    --output "$temporary_board" \
    --dsn "$temporary_dsn"
)
if ! cmp -s "$temporary_dsn" "reports/$name.dsn"; then
  echo "committed Specctra DSN is stale; regenerate the routed PCB" >&2
  exit 1
fi
kicad-cli pcb drc \
  --refill-zones \
  --severity-all \
  --exit-code-violations \
  --format json \
  -o "$staged_reports/$name-drc.json" \
  "$name.kicad_pcb"
kicad-cli pcb export stats --format json \
  -o "$staged_reports/$name-stats.json" "$name.kicad_pcb"
"$kicad_python" "$repo_root/.github/skills/kicad-pcb/scripts/report-route-lengths.py" \
  --check \
  --board "$name.kicad_pcb" \
  --manifest reports/net_manifest.json \
  --json "$staged_reports/route-lengths.json"

kicad-cli pcb export gerbers \
  --check-zones \
  --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts \
  -o "$staged_fabrication/gerbers" \
  "$name.kicad_pcb"
kicad-cli pcb export drill \
  --excellon-units mm \
  --excellon-separate-th \
  --generate-map \
  --map-format pdf \
  --generate-report \
  --report-path "$staged_fabrication/drill/$name-drill-report.txt" \
  -o "$staged_fabrication/drill" \
  "$name.kicad_pcb"
"$python_bin" "$repo_root/.github/skills/jlcpcb-gerbers/scripts/audit-silk-pad.py" \
  "$staged_fabrication/gerbers"
"$kicad_python" "$repo_root/scripts/export-jlc-assembly.py" \
  --board "$name.kicad_pcb" \
  --manifest reports/net_manifest.json \
  --bom "$staged_fabrication/assembly/$name-jlc-bom.csv" \
  --cpl "$staged_fabrication/assembly/$name-jlc-cpl.csv"
kicad-cli pcb export pos \
  --side both \
  --format csv \
  --units mm \
  -o "$staged_fabrication/$name-positions.csv" \
  "$name.kicad_pcb"
kicad-cli sch export bom \
  --fields Reference,Value,Footprint,LCSC,QUANTITY,DNP \
  --labels Refs,Value,Footprint,LCSC,Qty,DNP \
  --group-by Value,Footprint,LCSC \
  --exclude-dnp \
  -o "$staged_fabrication/$name-bom.csv" \
  "$name.kicad_sch"
cp "$staged_fabrication/$name-bom.csv" "$staged_exports/$name-bom.csv"
kicad-cli pcb export ipcd356 -o "$staged_fabrication/$name.ipc" "$name.kicad_pcb"
kicad-cli pcb export svg \
  --mode-single \
  --fit-page-to-board \
  --exclude-drawing-sheet \
  --check-zones \
  --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Silkscreen,Edge.Cuts \
  -o "$staged_exports/$name-pcb.svg" \
  "$name.kicad_pcb"
strip_trailing_space "$staged_exports/$name-pcb.svg"
kicad-cli pcb export pdf \
  --mode-multipage \
  --check-zones \
  --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Silkscreen,B.Silkscreen,F.Fab,Edge.Cuts \
  -o "$staged_exports/$name-pcb.pdf" \
  "$name.kicad_pcb"
kicad-cli pcb render \
  --quality high \
  --background transparent \
  --width 1800 \
  --height 1400 \
  --rotate 45,0,-35 \
  -o "$staged_exports/$name-pcb.png" \
  "$name.kicad_pcb"

(
  cd "$staged_fabrication"
  zip -q "$name-gerbers.zip" gerbers/* drill/*.drl
)

if [[ "$(shasum -a 256 "$name.kicad_pro")" != "$project_rules_before" ]]; then
  echo "KiCad project rules changed during validation" >&2
  exit 1
fi

for report in \
  "$name-erc.json" \
  "$name.net" \
  "$name-drc.json" \
  "$name-stats.json" \
  route-lengths.json
do
  cp "$staged_reports/$report" "reports/$report.new"
  mv "reports/$report.new" "reports/$report"
done
cp "$temporary_netlist" reports/netlist.xml

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
echo "PASS: partial-SMT board validated; fabrication package in $kicad_dir/fabrication"
