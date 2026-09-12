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
| Address `A0-A15` | 103.8–135.1 mm | 79.1–130.4 mm | A7: 130.4 mm |
| Z80 data `D0-D7` | 125.1–215.9 mm | 125.1–168.7 mm | D4: 168.7 mm |
| Pico data `PICO_D0-D7` | 134.9–155.6 mm | 114.4–155.6 mm | PICO_D0: 155.6 mm |

The broad total-copper spread is expected because each net includes different
multi-device branches. D4 has the largest tree total, but its longest endpoint
path fell from 194.7 mm to 168.7 mm and it now uses no vias. Do not equalize
these nets with serpentine routing.

## Timing/control totals

| Net | Total copper | Longest current pad path | Vias |
| --- | ---: | ---: | ---: |
| `PICO_CLK` | 141.9 mm | 126.1 mm | 0 |
| `Z80_CLK` | 92.6 mm | 87.5 mm | 1 |
| `WAIT_N` | 143.2 mm | 120.5 mm | 2 |
| `SRAM_CE_N` | 80.0 mm | 80.0 mm | 1 |
| `SRAM_OE_N` | 87.0 mm | 87.0 mm | 0 |
| `SRAM_WE_N` | 112.7 mm | 112.7 mm | 3 |

The driven paths for both clock nets are via-free. U4 remains physically
beside U1. The separate Z80_CLK test-point branch uses one via. `PICO_CLK`
includes the Pico-to-U4 path plus its startup-bias branch; use the report's
critical endpoint section to distinguish the driven path from total tree
copper.

| Critical driven path | Length |
| --- | ---: |
| Pico clock A1.4 → U4.2 | 126.1 mm |
| Z80 clock U4.18 → U1.6 | 70.0 mm |
| Z80 clock U4.18 → TP2.1 | 27.7 mm |
| SRAM CE U4.3 → U2.22 | 54.2 mm |
| SRAM OE U4.5 → U2.24 | 62.3 mm |
| SRAM WE U4.7 → U2.29 | 89.0 mm |

## Source-to-SRAM path groups

The address and data nets are multi-drop trees, so there is no single
electrically meaningful "length" for a bus bit. The relevant source/receiver
groups currently measure:

| Group | Path range | Spread |
| --- | ---: | ---: |
| Z80 → SRAM address | 18.9–53.8 mm | 34.9 mm |
| MCP23S17 → SRAM address | 62.2–88.0 mm | 25.8 mm |
| Z80 ↔ SRAM data | 15.8–92.9 mm | 77.1 mm |
| Pico write transceiver → SRAM data | 104.7–150.3 mm | 45.6 mm |
| SRAM → Pico read transceiver data | 125.1–158.1 mm | 33.0 mm |

At typical FR-4 propagation velocity, even the largest spread is below about
0.5 ns, compared with a 125 ns period at 8 MHz. Matching these paths to 2 mm
would require independent tuning at each source branch; a single meander on a
shared trunk changes several groups at once. The required added copper would
increase capacitance, coupling, and routing density without materially
improving the timing budget. Keep these source-specific measurements in the
report, but prefer short, low-via paths and measured receive-pin timing over
decorative length equality.

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
