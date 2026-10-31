---
name: banked-memory-apps
description: 'Build, extend, debug, or review banked-memory CP/M applications for Z80ROMlessSBC with DCC, dccmake, Bank 1 fixed-address .LIB libraries, generated typed APIs, BANKSDK, bank_library calls, bank_memory access, opaque handles, copied buffers and records, and LIBDEMO. Use for splitting bank-0 hosts from bank-1 services, library lifecycle and heap/stack budgets, contract generation, ABI compatibility, and application validation.'
---

# Banked-Memory Applications

Build a bank-0 CP/M host and a bank-1 DCC service using the maintained typed
library workflow. Reuse BANKSDK and generated adapters rather than introducing
another bank-switch mechanism or handwritten marshalling.

Paths in commands are relative to the repository root unless stated otherwise.

## Read Before Designing

- [Bank-1 specification](../../../docs/docs/en/cpm-dcc/bank1-library-spec.md):
  authoritative memory layout, library format, typed calls, and current limits.
- [LIBDEMO guide](../../../examples/LIBDEMO/README.md):
  application structure, annotations, tool prerequisites, and build workflow.
- [Library source](../../../examples/LIBDEMO/lib/DATA.C) and
  [host tests](../../../examples/LIBDEMO/host/DATATEST.C):
  maintained scalar, buffer, record, file, handle, and lifecycle examples.
- [BANKSDK guide](../../../examples/BANKSDK/README.md) and public headers
  [BANKLIB.H](../../../examples/BANKSDK/BANKLIB.H),
  [BANKMEM.H](../../../examples/BANKSDK/BANKMEM.H), and
  [BIOSBANK.H](../../../examples/BANKSDK/BIOSBANK.H):
  supported operations, status codes, and calling conventions.
- [BIOS ABI](../../../docs/docs/en/cpm-dcc/bios.md):
  capability discovery and versioned library control records.
- [Banked CP/M emulator test](../../../src/common/tests/bank1_system_boot_z80_test.py):
  runs the built LIBDEMO client and library under the CP/M/BIOS bank-switching
  model.

Read current sources rather than assuming this skill's numeric limits apply
to a future ABI. Use `z80-romless-sbc` for changes to firmware, BIOS, memory
layout, or the resident loader. Use `flash-cpm-disks` for authorized disk
deployment; an application build alone does not authorize hardware access.

## 1. Partition the Application

1. Keep the executable host and console interaction in bank 0. Put large
   objects, data processing, and service-owned files in bank 1.
2. Plan for one fixed-origin library at a time, synchronous non-reentrant
   calls, and no nested bank calls or runtime relocation.
3. Define bounded operations: integer scalars, explicitly counted copied
   buffers, pointer-free records, or opaque handles with chunked transfers.
   The sample keeps a 4 KiB block in bank 1 and reads at most 128 bytes per call.
4. Specify who owns each allocation/file, its lifetime, cleanup on partial
   failure, and how callers distinguish transport errors from ordinary results.
5. Budget image, BSS, DCC heap, call stack, resident workspace, and copied call
   frames before implementation. Do not equate bank capacity with free heap.

### Memory and ABI Gates

- The library starts at `0100h`. Image, BSS and at least one heap byte must
  fit below `C800h` minus the stack (an image may reach `C6FEh` with a
  256-byte stack). BSS follows the image; linked `__hstart` sets `heap_base`.
- The loader clears BSS and initializes DCC heap bounds. The heap ends below
  the call stack, whose upper boundary is `C800h`. Stack requirements are
  256-byte aligned, up to 2 KiB; the sample requests 512 bytes.
- The `C800h`-`DDFFh` general bank-memory pool is only 5,632 bytes and is
  distinct from the library's DCC `malloc` heap. `breset` resets only that pool;
  it does not unload the library or reset its DCC heap.
- Open reserves a 512-byte bank-1 workspace. Successful steady-state calls
  reuse it without allocating/freeing; preserve that property.
- Libraries use `.LIB` format 1 with ABI 1.0 and call revision 1. Every
  export has a full canonical signature; the image is not checksummed.
- Use BIOS operation 7 for capability discovery and operation 8 through the
  SDK for library control. Do not read loader-private mailboxes or allocator
  tables, hard-code private addresses, or replace public checks with raw writes.

## 2. Implement the Service and Contract

Start from the maintained
[library configuration](../../../examples/LIBDEMO/lib/dccmake.txt),
[host configuration](../../../examples/LIBDEMO/host/dccmake.txt), and
[combined configuration](../../../examples/LIBDEMO/dccmake.txt).
Preserve separate `lib/`, `host/`, generated-contract, and build directories.

1. Select `dcc-output-format=lib`, `dcc-library-api=auto`, automatic exports,
   and native `l80c` using the current library configuration as the template.
   Order the root projects library-first, host-second.
2. Make only intended exports public; mark private helpers `static`.
   There are at most eight named exports, including lifecycle hooks.
   Let generation handle unique adapter symbols within M80's six-character
   public-symbol limit.
3. Declare buffer direction, length argument, and maximum with
   `#pragma dcc bank_buffer`, following the current library source.
   Reject invalid counts and null/nonzero buffer combinations.
4. For records, use `#pragma dcc bank_record FUNCTION ARG in|out|inout`,
   where `ARG` is 1-based. Use complete named flat structs containing
   unqualified 8/16/32-bit integers or fixed one-dimensional integer arrays.
   No pointer fields, nested/anonymous structs, unions, bitfields, qualified
   members, or by-value aggregates. Record pointers cannot be NULL.
5. Include the generated API header in clients and compile the generated host
   wrappers with BANKLIB and BIOSBANK, as the host configuration does.
   Do not hand-edit generated files, duplicate their declarations, or invent a
   JSON manifest. Generate contracts before implementing a dependent client.
6. Use descriptive SDK names such as `bank_library_open`,
   `bank_library_call`, `bank_library_last_error`, and `bank_memory_allocate`
   where applicable. The six-character symbols they map to are also valid;
   do not rename generated wrappers or add redundant wrapper layers.

### Boundary and Ownership Rules

- Call bodies are at most 256 bytes, with a two-byte frame-length prefix.
  Account for scalar fields, counts, records, and copied buffers together.
  Canonical signatures are at most 255 characters.
- Compare complete generated signatures, not hashes, record tags, or sizes
  alone. Retain generated width, size, and offset checks for packed
  little-endian records.
- Never pass bank-0 pointers as bank-1 addresses. Adapters form local pointers
  into copied data; these pointers must not outlive the call.
  Pointer graphs, callbacks, varargs, and implicit deep copies are unsupported.
- Copied arguments are independent snapshots. Overlap involving a record is
  rejected if either argument is writable. OUT records begin zero-filled.
- Transport rejection leaves caller outputs unchanged. After successful
  dispatch, outputs are copied back even if the ordinary C result reports an
  application-level failure. Check `bank_library_last_error()` separately
  from the exported function's result.
- Keep large objects bank-owned. Validate handle, offset, and length before
  access; invalidate handles on close/replacement/unload. Handles are not
  addresses, persistent identifiers, or a security boundary.
- Use SDK generation/epoch checks. Reload/invalidation or pool RESET can
  invalidate cached state; `BL_STALE` must not free or overwrite reassigned
  extents. Tokens are not reused during the resident runtime's lifetime;
  propagate `BL_EXHAUSTED` rather than wrapping or recycling them.
- Use automatic SDK `onload`/`onunload` lifecycle handling. Release outstanding
  files and objects on unload and leave no committed DCC heap. Do not bypass
  cleanup by directly invalidating a live module.
- Bank-1 C uses its own BDOS vector. Do not re-enter the bank-switch allocator,
  call bank-0 code addresses, or use `exit`, `abort`, program chaining, or
  transient-program startup paths in the library.
- Library and host DMA/search continuations are independent, but current
  drive/user and allocation state remain shared. Test these interactions if
  adding file operations.

## 3. Generate and Build

Find the actual DCC checkout; set `DCC_DIR` to it rather than assuming a local
installation path. Use the matching updated compiler/runtime, including
`meminfo()`, and the guide's `dccmake`, native `l80c`, `m80c`, `dccpeep`, and
`dccrtlstrip` tools. Inspect existing configuration before replacing tool paths.

For the maintained sample:

```sh
export DCC_DIR=/path/to/dcc
(cd examples/LIBDEMO && "$DCC_DIR/dccmake" dcc-contract-only=true)
(cd examples/LIBDEMO && "$DCC_DIR/dccmake")
```

Replace the placeholder before execution. For a new application, use its root
instead of `examples/LIBDEMO`. Contract-only mode skips COM children and needs
no assembly/link step; normal builds refresh the contract automatically.
Use `"$DCC_DIR/dccmake" -help` for current options.

The sample produces `lib/build/DATA.LIB` and `host/build/DATATEST.COM`, with
`host/generated/API.H` and `host/generated/B1HOST.C`. Rebuild both sides after
API changes. Inspect generated contracts and linker output for actual image,
BSS, heap, stack, signature, and frame limits; do not guess free memory.
Report missing tools or build errors explicitly, not as successful generation.

## 4. Validate the Application

1. Run contract generation and the actual application build. Contract generation
   alone does not prove that the linked library or client executes correctly.
2. Add focused host checks following DATATEST: scalar edges, maximum/zero/invalid
   buffer lengths, null rejection, all record directions, signature mismatch,
   malformed/truncated frames, failed-call output preservation, stale handles,
   double-free, bounds, and allocation failure as applicable.
3. Exercise repeated open/call/close, reload with live objects, cleanup after
   partial failure, and pool recovery. Check directory-search and shared
   drive/user behavior when using files.
4. For the sample on CP/M, place matching `DATA.LIB` and `DATATEST.COM` on the
   same drive, ensure its `A:AUTORUN.COM` fixture has at least 128 bytes, then run
   `DATATEST`. Require all eight numbered checks to report PASS.
   For a new application, provide equivalent explicit acceptance checks.
5. To run the maintained LIBDEMO end-to-end in the project's banked-memory
   emulator, build it first, then run this from the repository root:

   ```sh
   DCC_DIR=/path/to/dcc build/disk-emulator/bin/python \
     src/common/tests/bank1_system_boot_z80_test.py \
     BankOneSystemBoot.test_dcc_library_loaded_and_called_from_bank_zero
   ```

   Replace the DCC path with the actual checkout. The test builds a CP/M/BIOS
   image, installs `DATA.LIB` and `DATATEST.COM` into its test disk, and runs
   all eight client checks against modeled bank switching. This integration
   test is specific to LIBDEMO; `banked_heap_z80_test.py` is instead for
   `examples/BANKED` and its `BANKED_ONE_CYCLE` build. Neither emulator test
   establishes physical-board timing. For a new application, extend or create
   an equivalent emulator test rather than treating either sample fixture as
   a generic COM/LIB runner.
6. If changing CP/M images, loader, or ABI, also run the existing repository-root
   host suite and the engineering skill's affected build checks:

   ```sh
   python3 -m unittest discover -s src/cpm -p 'test_*.py' -v
   ```

   These tests cover image/ABI/emulator behavior, not physical board timing.
   Do not flash firmware for an ordinary application-only change. ABI changes
   require matching firmware and CP/M system images, deployed only with
   authorization.
7. Report artifacts, commands, observed PASS/FAIL results, actual memory budgets,
   and any untested target behavior. A build or emulator pass is not hardware
   qualification. Update directly related application documentation when the
   contract, build procedure, limits, or usage changes.
