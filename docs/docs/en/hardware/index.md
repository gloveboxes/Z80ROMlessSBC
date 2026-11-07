# Hardware Design

The hardware chapters progress from the bill of materials through pin-level
wiring, address generation, physical construction, buffering, and final bus
isolation. For a first build, read the inventory and construction basics,
then follow the phased [implementation plan](../implementation/index.md).
Consult the other hardware chapters when a phase refers to them; their
chapter order is not the chip-installation order.

| Chapter | Purpose |
| --- | --- |
| [0. Project inventory](inventory.md) | Parts, passive components, power distribution, and required bench equipment |
| [1. Reference pin mapping](pin-mapping.md) | Authoritative IC pin maps, signal ownership, and logic-domain checks |
| [2. Address interface](address-interface.md) | Current 128 KiB SRAM address map, bank select, Z80-assisted loading, and four-bit port monitoring |
| [3. Physical construction](construction.md) | Breadboard topology, socket placement, and jumper routing |
| [4. Output buffer](output-buffer.md) | AHCT244 channel allocation and enable behavior |
| [5. Bus isolation](bus-isolation.md) | Address, data, and control transceiver operating modes |

There are [two four-layer PCB designs](../pcb/variants.md): a hand-assembled
DIP / through-hole board and a compact, JLC-assembled partial-SMT board.
They use different packages, SRAM parts and BOMs. Each board's mechanical
assembly and 20 MHz qualification are separate from the breadboard
implementation sequence.

The instrument procedures are cross-cutting qualification references rather
than additional hardware chapters. Use the
[RIGOL DHO814 capture plan](oscilloscope.md) for analogue integrity and exact
timing, and the [DSLogic Plus capture plan](logic-analyzer.md) for bus-wide
digital state and event ordering. Individual implementation pass gates name
the captures required at each phase.
