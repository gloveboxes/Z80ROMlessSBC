---
name: rigol-scope
description: 'Use for Rigol DHO814 MCP control over LAN: discovery, acquisition, triggers/decode, measurements, waveform downloads, screenshots, SCPI lookup, mask/search/recording, and setup preservation/restoration.'
---

# Rigol Scope

## Workflow

1. Read the [operation reference](./references/operation.md) before instrument
   calls. Bond the DHO814 chassis to protective earth before connecting leads;
   ordinary passive probes are not isolated.
2. Discover tools from the selected registered MCP server. Use one server per
   scope and strictly sequential calls; do not substitute direct sockets,
   ad hoc scripts or another server when tools are unavailable.
3. Call `idn`, `get_capabilities`, then `get_scope_state` sequentially.
   Confirm the intended instrument before changing it. Initial connection clears
   SCPI errors; capability verification also drains them unless disabled.
4. Choose the [task-specific tool](./references/operation.md#choose-the-tool)
   from its discovered schema. Prefer semantic tools; use `scpi_catalog` before
   `scpi_execute`. Keep unrestricted `send_raw` disabled unless authorized.
5. Record prior settings and make only authorized changes. Save/restore setup for
   broad temporary reconfiguration; restore acquisition and transfer state too.
   After communication failure, inspect state before retrying a possible write.
6. Keep captures file-backed and inspect bounded excerpts, not raw samples,
   catalogs or base64 dumps. Container `/data` paths are not host paths.
7. Report model/firmware, measured results, warnings, changes/restoration and
   unverified behavior. Open-input noise and the `9.9E37` sentinel are not valid
   timing measurements; offline tests do not establish hardware accuracy.

The reference covers server setup, acquisition side effects, timeouts,
single-use setup confirmation and error-queue handling.
