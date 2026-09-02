# Hardware Design

The hardware chapters progress from the bill of materials through pin-level
wiring, address generation, physical construction, buffering, and final bus
isolation. Read chapters 0-5 in order before following the staged
[implementation plan](../implementation/index.md).

| Chapter | Purpose |
| --- | --- |
| [0. Project inventory](inventory.md) | Parts, passive components, power distribution, and required bench equipment |
| [1. Reference pin mapping](pin-mapping.md) | Authoritative IC pin maps, signal ownership, and logic-domain checks |
| [2. Address interface](address-interface.md) | MCP23S17 address expansion and SPI control |
| [3. Physical construction](construction.md) | Breadboard partitioning, placement, and interconnect topology |
| [4. Output buffer](output-buffer.md) | AHCT244 channel allocation and enable behavior |
| [5. Bus isolation](bus-isolation.md) | Address, data, and control transceiver operating modes |

The instrument procedures are cross-cutting qualification references rather
than additional hardware chapters. Use the
[RIGOL DHO814 capture plan](oscilloscope.md) for analogue integrity and exact
timing, and the [DSLogic Plus capture plan](logic-analyzer.md) for bus-wide
digital state and event ordering. Individual implementation pass gates name
the captures required at each stage.