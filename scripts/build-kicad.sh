#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
python_bin=${PYTHON:-python3}
kicad_dir="$repo_root/hardware/kicad"
temporary_netlist=$(mktemp "${TMPDIR:-/tmp}/z80-romless-sbc-netlist.XXXXXX.xml")
trap 'rm -f "$temporary_netlist"' EXIT

command -v kicad-cli >/dev/null
"$python_bin" "$repo_root/scripts/build-kicad-schematic.py"

cd "$kicad_dir"
kicad-cli sym upgrade --force z80sbc.kicad_sym
kicad-cli sch upgrade --force z80_romless_sbc.kicad_sch
kicad-cli sch erc \
  --severity-all \
  --exit-code-violations \
  --format json \
  -o reports/z80_romless_sbc-erc.json \
  z80_romless_sbc.kicad_sch

find exports -mindepth 1 -maxdepth 1 -type f -delete
kicad-cli sch export svg \
  --exclude-drawing-sheet \
  -o exports \
  z80_romless_sbc.kicad_sch
"$python_bin" -c \
  'from pathlib import Path; import sys; p=Path(sys.argv[1]); p.write_text("\n".join(line.rstrip() for line in p.read_text().splitlines()) + "\n")' \
  exports/z80_romless_sbc.svg
kicad-cli sch export pdf \
  -o exports/z80_romless_sbc.pdf \
  z80_romless_sbc.kicad_sch
kicad-cli sch export netlist \
  --format kicadsexpr \
  -o reports/z80_romless_sbc.net \
  z80_romless_sbc.kicad_sch
kicad-cli sch export netlist \
  --format kicadxml \
  -o "$temporary_netlist" \
  z80_romless_sbc.kicad_sch

"$python_bin" "$repo_root/scripts/check-kicad-netlist.py" \
  reports/net_manifest.json \
  "$temporary_netlist"
"$python_bin" "$repo_root/scripts/check-doc-interconnects.py"