# Current measured route

Measured from `hardware/kicad/z80_romless_sbc.kicad_pcb` with
`.github/skills/kicad-pcb/scripts/report-route-lengths.py`.

These are review baselines, not timing qualification. Re-run the report after
any placement, routing, netclass, or footprint change.

The RP2350 supervisor may run at up to 150 MHz while generating a deliberately
slower Z80 clock. The lengths below therefore matter for fast edge quality and
coupling even though the four-layer PCB's Z80 clock target is 8 MHz.

## Bus ranges

| Group | Total copper range | Longest pad-to-pad range | Current outlier |
| --- | ---: | ---: | --- |
| Address `A0-A15` | 104.2–128.7 mm | 84.2–128.5 mm | A7: 128.5 mm |
| Z80 data `D0-D7` | 129.2–215.9 mm | 129.2–168.7 mm | D4: 168.7 mm |
| Pico data `PICO_D0-D7` | 132.2–156.0 mm | 111.4–156.0 mm | PICO_D0: 156.0 mm |

The broad total-copper spread is expected because each net includes different
multi-device branches. D4 has the largest tree total, but its longest endpoint
path fell from 194.7 mm to 168.7 mm and it now uses no vias. Do not equalize
these nets with serpentine routing.

## Timing/control totals

| Net | Total copper | Longest current pad path | Vias |
| --- | ---: | ---: | ---: |
| `PICO_CLK` | 141.9 mm | 126.1 mm | 0 |
| `Z80_CLK` | 94.2 mm | 94.2 mm | 0 |
| `WAIT_N` | 144.8 mm | 144.8 mm | 1 |
| `SRAM_CE_N` | 80.4 mm | 80.4 mm | 1 |
| `SRAM_OE_N` | 87.1 mm | 87.1 mm | 3 |
| `SRAM_WE_N` | 105.9 mm | 105.9 mm | 3 |

Both clock nets are via-free. U4 remains physically beside U1. `PICO_CLK`
includes the Pico-to-U4 path plus its startup-bias branch; use the report's
critical endpoint section to distinguish the driven path from total tree
copper.

| Critical driven path | Length |
| --- | ---: |
| Pico clock A1.4 → U4.2 | 126.1 mm |
| Z80 clock U4.18 → U1.6 | 70.0 mm |
| Z80 clock U4.18 → TP2.1 | 24.2 mm |
| SRAM CE U4.3 → U2.22 | 53.9 mm |
| SRAM OE U4.5 → U2.24 | 60.7 mm |
| SRAM WE U4.7 → U2.29 | 79.8 mm |

## Review thresholds

These are engineering prompts, not automatic acceptance limits:

- Investigate any address/data bit whose longest path grows by more than about
  20 mm relative to its group neighbors.
- Investigate new vias on `Z80_CLK`.
- Investigate clock/control routes that leave their local component cluster or
  run parallel at minimum clearance for long distances.
- Keep SRAM control-source paths and their return geometry grouped.
- Keep In1.Cu free of signal tracks so it remains a continuous GND reference.
- Dynamic qualification still requires the documented scope and logic-analyzer
  captures at 1 MHz first, then stepped testing through the 8 MHz target.
