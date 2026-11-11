# PCB Layout

PCB design is an iterative process shared by the user and agent. Agree on the
board's goals, components and constraints first; then generate, route and review
the layout against those criteria before preparing manufacturing files.

1. **Define goals.** The user describes what the board should do and how it
   will be used.
2. **Agree on components and constraints.** The user and agent settle key parts,
   electrical needs, connectors, component locations, board size, assembly,
   quantity and budget. The agent explains trade-offs; the user confirms the
   design criteria before implementation.
3. **Generate the layout.** The agent directs KiCad's Python tools to create
   the schematic, component placement, board shape and routing rules.
4. **Route the connections.** The agent gives the placed design to Freerouting,
   which searches for copper paths and returns them for KiCad to import. The
   router does not choose component locations.
5. **Review against the criteria.** Check that connections are complete and
   design rules pass, then assess requirements such as keeping a component in
   its agreed location or matching critical trace lengths when the design
   requires it.
6. **Iterate and deliver.** If the layout misses a criterion, adjust placement
   or rules and route again. Once checks pass, generate and review the
   manufacturing files.

```mermaid
flowchart TD
    A["1. User defines board goals"]
    B["2. User and agent agree components,<br/>locations and design constraints"]
    C["3. Agent directs KiCad Python tools<br/>to generate placement and routing inputs"]
    D["4. Freerouting finds copper paths;<br/>KiCad imports the returned routes"]
    E["5. Review against agreed criteria:<br/>connections, design rules,<br/>locations and critical trace lengths"]
    F{"Criteria met?"}
    G["6. Adjust placement or rules<br/>and route again"]
    H["6. Generate manufacturing files<br/>and review manufacturer feedback"]

    A --> B --> C --> D --> E --> F
    F -- "No" --> G --> C
    F -- "Yes" --> H
```

The routing input is a **DSN** (Specctra Design file); the returned **SES**
(Specctra Session file) contains the proposed routes. **Gerber** and drill
files describe the board layers and holes. For partial-SMT assembly, the
**BOM** (bill of materials) lists parts, and the **CPL** (component placement
list) gives their positions and orientations. Computer checks do not replace
reviewing manufacturer feedback and the assembly placement preview.
