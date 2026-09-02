# D.3 Custom BIOS Responsibilities

The BIOS supplies the standard CP/M 2.2 jump table expected by BDOS and by
dcc's optional direct-BIOS functions. Its principal mappings are:

| CP/M BIOS operation | Board implementation |
| --- | --- |
| `BOOT` / `WBOOT` | Initialize page zero or reload CCP/BDOS from Drive A |
| `CONST` | Read terminal status port `0x01`; return `0xFF` when bit 0 reports input ready |
| `CONIN` | Wait for receive-ready, then read terminal data port `0x00` |
| `CONOUT` | Wait for transmit-room bit 1, then write the character to port `0x00` |
| `READER` | Return CP/M text EOF (`0x1A`); no reader device is implemented |
| `LIST` / `PUNCH` | Discard output; printer and punch devices are intentionally not implemented |
| `LISTST` | Return not ready because no list device exists |
| `SELDSK` | Validate drives A-D and return the corresponding disk parameter header |
| `SETTRK` / `SETSEC` / `SETDMA` | Record the logical CP/M transfer location and SRAM DMA address |
| `READ` / `WRITE` | Convert track and sector to a linear 128-byte LBA, transfer through ports `0x10`-`0x14`, and preserve CP/M write-type semantics for flash caching |
| `SECTRAN` | Return the sector unchanged because native disk images use linear sector order |

CP/M BDOS remains responsible for filenames, FCBs, directory searches,
allocation, sequential/random record selection, and text-file conventions.
The BIOS sees only drive selection and 128-byte logical records. Consequently,
dcc file APIs do not require a board-specific runtime backend: their BDOS file
calls eventually reach the custom `READ` and `WRITE` entries.

Console ports occupy the aligned `0x00` group and disk ports the aligned
`0x10` group. Moving them would not reduce Pico trap work because the same
8-bit port decoder handles every I/O cycle, so these established direct-I/O
addresses remain stable.
