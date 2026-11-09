---
name: flash-cpm-disks
description: 'Use for disk-only CP/M .dsk/.img flashing on Pico 2 W: selective drives A-D, BDS C/Escape, Altair conversion, installed-layout checks, journal recovery, BOOTSEL and read-back verification without rebuilding firmware.'
argument-hint: 'Disk image paths and target drives A-D'
user-invocable: true
---

# Flash CP/M Disks

Read the [deployment procedure](./references/deployment.md) before hardware access.
Use it only for the established layout; firmware, BIOS or geometry changes belong
in [firmware engineering](../z80-romless-sbc/SKILL.md).

## Workflow

1. Resolve the user's exact images and selected drives; ask only when ambiguous.
   Routine replacements use **no backups** unless separately requested.
2. Use `scripts/pico_flash.py disks`, not custom serial, conversion or picotool
   clients. In every fresh shell, activate `.venv-flash` and verify `serial`
   imports in the same `&&` chain as the command. Stop on preflight failure.
3. Validate all inputs and installed-layout metadata. Native disks are exactly
   524,288 bytes; B-D Altair inputs are converted by the maintained tool.
   Drive A must match the installed board-native boot package.
4. Confirm deployment authorization, saved CP/M work and external +5 V.
   Apply external power before USB; disconnect USB before removing it.
   Discover the device rather than assuming a port or serial.
5. Let the tool gracefully disconnect verified picocom, stop/flush, enter
   BOOTSEL and check every journal header. Abort on unknown owners, unconfirmed
   flushes or pending replay; never erase the journal to bypass recovery.
6. Write only selected slots and require read-back verification before reboot.
   Preserve firmware, boot data, journal and unselected disks. Never use mass
   erase or a complete flash image for a disk update.
7. Report source-to-drive mappings, verification and actual failure/device state.
   Leave serial free; routine flashing ends after verified writes and the
   requested reboot, without extra boot/health checks.

The reference contains exact commands, BOOTSEL exceptions, compatibility gates
and focused tests for converter or flashing-tool changes.
