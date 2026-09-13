# PCB Design Considerations

The PCB preserves the shared electrical architecture, but has its own
placement, return paths, manufacturing constraints, and qualification
requirements. The considerations below apply to this routed board, not to
the breadboard's socket rows or jumper lengths.

## Layer stack and routing rules

| Layer | Role |
| --- | --- |
| F.Cu | Signals and local locked routes |
| In1.Cu | GND reference plane; no autorouted signal tracks |
| In2.Cu | Signals, including the locked D4 route |
| B.Cu | Signals |

Signal tracks are 0.30 mm and power tracks are 0.60 mm. The `PICO_CLK` and
`Z80_CLK` nets use a dedicated 0.40 mm clearance rule. The autorouter is
restricted to F.Cu, In2.Cu, and B.Cu; In1.Cu remains the GND reference.
Inspect plane continuity and return paths after every routing or cutout
change. Four layers alone do not guarantee a useful return path.

The ground pour uses thermal reliefs except at explicitly generated pads
where the routed geometry would otherwise starve the thermal spokes.
Do not weaken clearance rules or suppress warnings to make a route pass.

## Placement and mechanical clearance

U2 SRAM, U1 Z80, U4 AHCT244, U8 MCP23S17, and U3 GAL form the main
address/control cluster. U4 remains local to U1's clock input. U9/U10 and U7
sit between the shared buses and the Pico.

The nearest U1/U4 pin-row centers are **5.76 mm apart**. This is not a
socket-body gap. Check the overhang, tolerances, and extraction clearance of
the actual purchased sockets; DIP courtyards and missing 3D socket models
cannot prove physical fit.

The horizontal Pico is in the bottom-left corner. USB faces outward at the
left board edge. Its antenna sits above an internal FR-4/copper cutout,
approximately 11.9 x 15.6 mm overall, with chamfered corners. Preserve the
official antenna keepout and router-radius margin, and keep the USB cable,
BOOTSEL access, and SWD access unobstructed. Review nearby enclosure material
and metal rather than assuming the board cutout alone qualifies radio
performance.

There are four 3.2 mm non-plated mounting holes. Verify standoffs, screws,
terminal-block access, electrolytic diameters, and component heights against
the planned enclosure and purchased parts.

## Power and startup safety

The main power path is:

`J1 +5 V -> D1 (1N5819) -> Pico VSYS -> Pico onboard 3.3 V regulator`

The main 5 V supply powers the computer, including the Pico, without USB.
The 5 V logic shares that supply; the Pico's 3.3 V output supplies the LVC
devices and 3.3 V bias resistors. Do not connect external 5 V to Pico VBUS,
the 3.3 V rail, or GPIOs, and do not add a competing external 3.3 V supply.

!!! warning "USB is not a substitute for the populated board's 5 V supply"
    Apply external +5 V before connecting USB. Disconnect USB before removing
    external +5 V. U9's AHCT245 data ports lack power-off isolation: Pico
    outputs can inject current into an unpowered U9 even when OE is inactive.
    USB-only programming or operation of the populated board is unsupported.
    Unexpected external-supply loss with USB attached needs additional
    hardware protection; the VSYS diode does not isolate signal pins.

Power every installed 5 V device whenever the 5 V rail is energized.
Pull-ups protect absent socketed drivers, not an installed IC with a missing
supply connection.

R23-R26 are 4.7 kOhm pull-downs on RESET#, DATA_ENABLE, DATA_DIR, and
ADDR_ENABLE. The supported ATF22V10B can source 100 uA at LOW inputs; a
10 kOhm pull-down cannot guarantee its 0.8 V maximum LOW. RN4/RN5 consolidate
sixteen 5 V control pull-ups without changing their nets.

Keep each 100 nF bypass close to its associated IC supply and provide a
short ground return. Distributed bulk capacitors do not replace local
bypassing. J1 must retain its `+5V`/`GND` labels, and C9-C12 must retain their
positive marks and negative-side shading. D1's banded cathode faces VSYS;
verify Q1's purchased E/B/C pin order before assembly.

## Shared bus-control requirements

Use the [shared pin mapping](../hardware/pin-mapping.md) and
[bus-isolation reference](../hardware/bus-isolation.md) for full connections.
PCB routing does not relax these electrical requirements:

- `CPU_OWNS_SRAM = RESET# AND BUSACK#`; reset or a granted bus selects the
  Pico's SRAM controls.
- GAL SRAM-control outputs pass through the AHCT244 because the GAL's
  guaranteed 2.4 V HIGH is insufficient for direct SRAM CMOS inputs.
- DATA_ENABLE LOW disables both data paths. Firmware changes DATA_DIR only
  while disabled and initializes GP10-GP17 as SIO inputs before the first
  data write.
- The MCP23S17 preloads OLAT before enabling address outputs and is reset
  to isolate the address bus.
- Incoming 5 V signals reach Pico GPIOs through the specified LVC buffers.

The GAL equations require their consensus terms to remain effective in the
fitted device. Source-level truth tables do not prove the programmed fuse
map's hazard behavior or actual ownership-transition timing.

## Trace lengths and multidrop buses

Address and data nets are shared trees with multiple drivers and receivers.
Total copper length is not the same as the path between a particular source
and the SRAM. Report these groups separately after any placement or route
change:

| Source/receiver group | Current path range |
| --- | ---: |
| Z80 to SRAM address | 13.9-54.1 mm |
| MCP23S17 to SRAM address | 60.5-87.5 mm |
| Z80 to/from SRAM data | 13.8-94.9 mm |
| Pico-write transceiver to SRAM data | 105.0-150.3 mm |
| SRAM to Pico-read transceiver data | 125.0-158.1 mm |

The reviewed baseline also has these driven paths:

| Path | Length |
| --- | ---: |
| Pico clock to U4 | 129.0 mm |
| U4 to Z80 clock input | 73.0 mm |
| U4 clock branch to TP2 | 24.2 mm |
| U4 to SRAM CE# | 54.7 mm |
| U4 to SRAM OE# | 99.0 mm |
| U4 to SRAM WE# | 122.7 mm |

Both complete clock nets are via-free. D4 uses a locked, via-free In2.Cu
route. These are route measurements, not propagation-delay measurements or
proof of operating frequency.

Prefer short shared trunks, short taps, few layer changes, and nearby return
paths. Do not add meanders merely to make total bus copper equal. A meander
on a shared trunk changes several source/receiver paths and adds capacitance
and coupling. The MCP23S17-to-SRAM paths matter during Pico loading/readback,
but need their own timing assessment rather than cosmetic matching to the
Z80 address paths.

The Pico/RP2350 can operate at up to 150 MHz while generating a much slower
Z80 clock. Edge rate, ringing, coupling, and receiver setup/hold margins
matter even when the Z80 clock is low. Re-run the repository's route-length
report after layout changes; the ranges above describe the reviewed layout,
not future edits.

## Timing and hardware qualification

Start at 1 MHz and establish a reliable baseline. The PCB targets
qualification through at least 8 MHz; the Z80's 20 MHz part rating does
not qualify the board, and faster SRAM alone would not establish that rate.

Reuse applicable functional checks and receiving-pin measurements from the
[frequency procedure](../implementation/frequency-qualification.md), but
keep a separate PCB record. Include repeated cold boots, SRAM pattern tests,
DMA readback, ownership transitions, and I/O WAIT/clock-stop behavior.
Measure SRAM setup/hold and write-pulse margins as well as clock levels,
ringing, overshoot, and supply behavior.

Use the [DHO814](../hardware/oscilloscope.md) for analogue quality and close
timing and the [DSLogic Plus capture groups](../hardware/logic-analyzer.md)
for digital ordering. Neither a clean DRC result, a 3D rendering, nor a
successful terminal session establishes all of these properties.
