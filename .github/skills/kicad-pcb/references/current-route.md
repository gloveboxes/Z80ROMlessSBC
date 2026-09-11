# Current measured route

Measured from `hardware/kicad/z80_romless_sbc.kicad_pcb` with
`.github/skills/kicad-pcb/scripts/report-route-lengths.py`.

These are review baselines, not timing qualification. Re-run the report after
any placement, routing, netclass, or footprint change.

The RP2350 supervisor may run at up to 150 MHz while generating a deliberately
slower Z80 clock. The lengths below therefore matter for fast edge quality and
coupling even when the Z80 bus itself is qualified only in the low-MHz range.

## Bus ranges

| Group | Total copper range | Longest pad-to-pad range | Current outlier |
| --- | ---: | ---: | --- |
| Address `A0-A15` | 100.9–154.9 mm | 77.7–154.9 mm | A8: 154.9 mm |
| Z80 data `D0-D7` | 125.8–200.8 mm | 125.8–194.7 mm | D4: 194.7 mm |
| Pico data `PICO_D0-D7` | 134.7–163.0 mm | 134.7–163.0 mm | PICO_D6: 163.0 mm |

The broad spread is expected because each net includes different pull-network
and multi-device branches. Do not equalize these with serpentine routing.
Inspect D4 first after future placement work because it is materially longer
than the other Z80 data bits.

## Timing/control totals

| Net | Total copper | Longest current pad path | Vias |
| --- | ---: | ---: | ---: |
| `PICO_CLK` | 160.1 mm | 145.0 mm | 5 |
| `Z80_CLK` | 92.1 mm | 87.0 mm | 0 |
| `WAIT_N` | 142.3 mm | 142.3 mm | 2 |
| `SRAM_CE_N` | 82.1 mm | 77.7 mm | 2 |
| `SRAM_OE_N` | 115.6 mm | 106.6 mm | 3 |
| `SRAM_WE_N` | 120.7 mm | 114.2 mm | 4 |

`Z80_CLK` has no vias and U4 remains physically beside U1. `PICO_CLK` includes
the Pico-to-U4 path plus its startup-bias branch; use the report's critical
endpoint section to distinguish the driven path from total tree copper.

| Critical driven path | Length |
| --- | ---: |
| Pico clock A1.4 → U4.2 | 132.3 mm |
| Z80 clock U4.18 → U1.6 | 70.0 mm |
| Z80 clock U4.18 → TP2.1 | 27.2 mm |
| SRAM CE U4.3 → U2.22 | 58.6 mm |
| SRAM OE U4.5 → U2.24 | 106.6 mm |
| SRAM WE U4.7 → U2.29 | 99.3 mm |

## Review thresholds

These are engineering prompts, not automatic acceptance limits:

- Investigate any address/data bit whose longest path grows by more than about
  20 mm relative to its group neighbors.
- Investigate new vias on `Z80_CLK`.
- Investigate clock/control routes that leave their local component cluster or
  run parallel at minimum clearance for long distances.
- Keep SRAM control-source paths and their return geometry grouped.
- Dynamic qualification still requires the documented scope and logic-analyzer
  captures at 1 MHz first, then stepped testing.
