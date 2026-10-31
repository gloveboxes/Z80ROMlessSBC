# Bank-1 fixed-address DCC library

## Implemented layout

Bank 1 owns CP/M disk allocation and directory state. Bank-0 BDOS calls enter
bank 1 through the shim at `EE00h`; bank-1 C code calls its own `0005h` BDOS
vector. Bank-0 console calls remain local. One DCC library can be loaded at
`0100h` at a time.

| Bank-1 address | Purpose |
| --- | --- |
| `0000h–00FFh` | CP/M vectors and page zero |
| `0100h–(heap_base-1)` | Fixed library image, static data and BSS |
| `heap_base–(C800h-stack-1)` | Library DCC `malloc` heap |
| `(C800h-stack)–C7FFh` | Library call stack, configured by the `.LIB` header |
| `C800h–DDFFh` | General `bmalloc` pool, 5,632 bytes |
| `DE00h–E5FFh` | BDOS proxy, loader, exports and service stack |
| `E600h–EDFFh` | Pinned extent allocator and metadata |
| `EE00h–FB3Fh` | Authoritative CP/M 2.2 BDOS |
| `FB40h–FB7Fh` | Mirrored warm-boot return trampoline |
| `FB80h–FEFFh` | BIOS buffer and core |
| `FF00h–FFFFh` | Mirrored bank-switch gateway |

The library image, C stack, and allocator are pinned. `breset` resets only the
general pool; it does not unload the library or reset its DCC heap.

## Library memory layout

A library owns the fixed region `0100h–C7FFh` (50,944 bytes). Its image,
BSS, DCC `malloc` heap and call stack share that region; only the stack size
is chosen explicitly. Everything from `C800h` up is outside the library: the
shared `bmalloc` pool, then pinned resident code loaded and verified by the
firmware at boot.

```text
0000h  +-----------------------------+
       | CP/M page zero              |  0005h = bank-1 BDOS vector
0100h  +-----------------------------+  ---- library region ----
       | Image: code and data        |  .LIB image length, loaded verbatim
       +-----------------------------+  0100h + image length
       | BSS                         |  zeroed by the loader
       +-----------------------------+  heap_base (linked __hstart)
       | DCC malloc heap             |  __brk = heap_base at load
       |            ...              |
       +-----------------------------+  __hlimit = C800h - stack
       | Call stack                  |  256..2048 bytes, grows down
C800h  +-----------------------------+  ---- shared pool ----
       | bmalloc pool                |  5,632 bytes, 22 x 256-byte extents
DE00h  +-----------------------------+  ---- pinned resident services ----
       | BDOS proxy                  |  E=6, used by the bank-0 BDOS shim
DE8Eh  | Loader, search, request and |  library request record at DF20h,
       | library context state       |  library DMA/search context at DF80h
E000h  | Library loader              |  validates the header, copies image
E300h  | Export table, capability    |  capability record at E360h,
       | record, descriptor helpers  |  descriptor helpers at E390h
E400h  | DMA buffer, FCB staging,    |  call token at E4D6h, library FCB
       | mailboxes, service stacks   |  at E500h, private stacks to E5FFh
E600h  | Dispatcher, allocator       |  dispatcher at E640h, tables at
       | tables, context hooks       |  E700h, context hooks at E780h
E800h  | Extent allocator            |  E=0..5
EC00h  | Capabilities and library    |  E=7 query, E=8 LOAD/INVALIDATE/
       | control                     |  QUERY/CALL
EE00h  +-----------------------------+  ---- CP/M ----
       | CP/M 2.2 BDOS               |  authoritative disk and allocation state
FB40h  | Warm-boot trampoline        |  mirrored; returns to bank-0 warm boot
FB80h  | BIOS buffer and core        |
FF00h  +-----------------------------+  ---- mirrored in both banks ----
       | Bank-switch gateway         |  READ/WRITE/INFO/service entries;
FFFFh  +-----------------------------+  FFF0h-FFFFh private workspace
```

The library may call its own BDOS through `0005h`. It accesses pool memory
only through what the host passes it: the copied call frame, or a pool
extent the host leases to it and validates, such as ISAM's bulk buffer. It
must not call the gateway or touch resident service memory; `DE00h–FFFFh`
is reserved for system code. Growth of the resident services never reduces
the library region.

| Region | Start | End (exclusive) | Set by |
| --- | --- | --- | --- |
| Image | `0100h` | `0100h` + image length | Linked by `l80c`; copied by the loader |
| BSS | Image end | `heap_base` | Linked by `l80c`; zeroed by the loader |
| DCC heap | `heap_base` | `C800h` − stack | `__brk` and `__hlimit` initialized by the loader |
| Call stack | `C800h` − stack | `C800h` | `dcc-stack-bytes`, recorded in the `.LIB` header |

### Size rules

The image, BSS and at least one heap byte must fit below the stack:
`heap_base < C800h − stack`, with `heap_base` at or above the end of both
the image and the BSS. There is no separate image-size cap; a larger image
leaves less heap. The stack must be 256–2,048 bytes in 256-byte units.

These rules are enforced at every stage:

- `dccmake` rejects an invalid `dcc-stack-bytes`, or one smaller than the
  compiler's computed stack requirement.
- `l80c --lib` fails with "bank-1 heap overlaps the configured stack" if the
  image, BSS and heap do not fit, and records `heap_base` and the stack size
  in the header.
- The loader rejects a header that breaks the same rules with FORMAT status
  before copying the image.

| Stack | Heap limit | Image + BSS + heap | Largest image (no BSS, 1-byte heap) |
| ---: | --- | ---: | ---: |
| 256 | `C700h` | 50,688 | 50,687 bytes, through `C6FEh` |
| 512 | `C600h` | 50,432 | 50,431 bytes, through `C5FEh` |
| 1,024 | `C400h` | 49,920 | 49,919 bytes, through `C3FEh` |
| 2,048 | `C000h` | 48,896 | 48,895 bytes, through `BFFEh` |

### Loader initialization

After validating the header and copying the image, the loader zeroes the BSS
from the image end up to `heap_base`, sets the linked DCC `__brk` to
`heap_base` and `__hlimit` to `C800h` minus the stack. The heap is not
cleared. A library linked without the DCC runtime records `FFFFh` for both
variable offsets and uses its image end as `heap_base`.

### Examples

| Library | Image | BSS | Heap | Stack |
| --- | --- | --- | --- | --- |
| LIBDEMO `DATA.LIB` | 8,863 bytes, `0100h–239Eh` | 4 bytes | 41,565 bytes, `23A3h–C5FFh` | 512 bytes, `C600h–C7FFh` |
| ISAM `ISAM.LIB` | 43,006 bytes, `0100h–A8FDh` | 1,104 bytes | 5,810 bytes, `AD4Eh–C3FFh` | 1,024 bytes, `C400h–C7FFh` |

Exact figures follow each build. Use DCC's `meminfo()` inside the library
to report heap capacity, committed bytes and remaining growth at run time.

## Library file and linker

DCC's native `l80c --lib`, maintained in its `src/l80c/l80c.c`, links a
fixed-origin image at `0100h` and emits one library format: a 26-byte
header, up to eight 12-byte export entries, then the image. Every export has
a canonical signature. Like a CP/M `.COM`, the image is loaded verbatim
without a checksum.

| Byte offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `B1LB` |
| 4 | 1 | Format version 1 |
| 5, 6, 7 | 3 | ABI major 1, minor 0, call revision 1 |
| 8 | 2 | Origin `0100h` |
| 10 | 2 | Image length |
| 12 | 2 | Export count, 1–8 |
| 14 | 2 | Reserved, zero |
| 16 | 2 | Stack bytes, 256–2,048 in 256-byte units |
| 18 | 2 | Header length, 26 + 12 × exports |
| 20, 22 | 4 | `__brk` and `__hlimit` image offsets, or `FFFFh` without the DCC runtime |
| 24 | 2 | Absolute heap base (`__hstart`) after the image and BSS |

Each export entry is:

| Entry offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | Zero-padded export name |
| 8 | 2 | Adapter offset relative to image origin |
| 10 | 2 | Canonical signature string offset relative to image origin |

All eight entries fit in the first 128-byte disk record.
Signature strings are nonempty, NUL-terminated ASCII, at most 255 characters,
inside the image. The full string is compared, not a short hash
that could collide. The loader validates both offsets against the image.

LIBDEMO separates `host/` and `lib/`, each with its own `dccmake.txt`
and `build/`. Its root configuration contains `dcc-projects=lib,host`;
one root `dccmake` invocation builds the library first, then its host.
The library configuration selects `dcc-output-format=lib`,
automatic exports, and DCC's native `l80c` through `l80c-tool`.
With `dcc-library-api=auto`, compiler-derived public definitions generate
`host/generated/API.H`, host wrappers and bank adapters. Buffer rules live in
the library source; no handwritten API header or JSON interface is needed.
Run `dccmake dcc-contract-only=true` in the root to generate the client
contract without assembly or linking; COM children are skipped. Normal builds
refresh it automatically. Clients include the generated header and compile
the generated wrappers with BANKLIB/BIOSBANK. `dccmake -help` describes the
workflow. A header path instead of `auto` validates a handwritten contract
against the same discovered, signed exports. M80 public symbols are limited
to six characters; generated adapter names are unique within that limit.

ABI 1.0 clients discover the versioned library service through BIOS operation 7
and submit LOAD/INVALIDATE/QUERY/CALL requests through operation 8. They do
not inspect loader-private mailboxes or allocator tables. See the exact
[capability/control records](bios.md#capability-and-library-control-records).
The linker writes, and the loader requires, format 1, ABI 1.0 and call
revision 1. An invalid header or read failure leaves the module uncallable,
with distinct FORMAT/IO status. The SDK runs `onload`/`onunload` automatically;
these C lifecycle hooks are SDK policy, not CP/M BDOS functions.

The resident dispatcher publishes a per-call two-byte token at the public
`E4D6h` ABI data address. The runtime never reuses a token within its
lifetime: after 65,534 invocations it returns `BL_EXHAUSTED` rather than
wrapping, reserving the last token for unload cleanup.
Bank-1 object constructors may use the token as an opaque handle.

The [LIBDEMO example](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/LIBDEMO)
builds `DATA.LIB`, an ordinary DCC C data service, and `DATATEST.COM`, its
bank-0 client. The host runs eight numbered end-to-end checks with explicit
PASS/FAIL results. Ordinary-looking `add16`, `upper`, `filerd`, `blknew`,
`blkread` and `blkfree` calls demonstrate scalar values, copied in/out text,
file open/read/close, flat record IN/OUT/INOUT transfers, and chunked reads
from a 4 KiB bank-owned block.
The two lifecycle hooks complete the eight exports. The tests also cover
export/signature enumeration, malformed calls, buffer bounds, stale handles,
reload invalidation, directory-search preservation, transfer-pool recovery,
and unloading an open block with no committed DCC heap left behind.
The emulator also executes a DCC library with a 32 KiB `malloc`/`free`
and a zero-initialized 2 KiB BSS. Its [library source](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/LIBDEMO/lib/DATA.C)
is the maintained example.

## Typed calls and object ownership

The SDK's preferred C names are descriptive: `bank_library_open`,
`bank_library_call`, `bank_library_last_error`, `bank_memory_allocate` and
the other `bank_library_*`, `bank_memory_*` and `bank_bios_*` operations.
Header aliases map them to six-character M80/L80 public symbols, which the
text below and generated wrappers also use; aliases add no wrapper call.

Shared client-side support lives in
[`examples/BANKSDK`](https://github.com/gloveboxes/Z80ROMlessSBC/tree/main/examples/BANKSDK),
separate from runnable examples. The reusable host runtime lives in
[`BANKLIB.H`](https://github.com/gloveboxes/Z80ROMlessSBC/blob/main/examples/BANKSDK/BANKLIB.H)
and its matching C implementation; BANKMEM and BIOSBANK provide checked memory
access and the BIOS calling-convention adapter. `blname` and `blsig` enumerate loaded
exports without a manifest. `blcall` checks the expected complete signature
before executing anything. `blerr` reports transport, format,
signature, allocation, bounds, lifecycle, and cleanup failures separately
from an exported function's ordinary result.

Open reserves one 512-byte bank-1 extent, first for FCB staging and then reused
for bounded call frames; close and failed open release it. Successful
steady-state calls perform no allocations/frees. Generated wrappers retain one
four-byte binding per export, checking its complete canonical signature once
per module generation. Raw `blcall` still validates the signature every time.
Invalidation/reload advances generation; RESET advances pool epoch. The SDK
queries both before writing a cached frame, and resident CALL validates them
again. Stale ownership returns `BL_STALE` and never frees or overwrites a
possibly reassigned extent. Metadata is immutable while loaded: raw debug
writes require invalidate/reload.

A two-byte frame-length prefix lets the generated adapter reject malformed
or truncated requests before accessing arguments. The body is at most 256
bytes; allocations and transfers are rounded to 16-byte BIOS bursts.
On successful dispatch the result frame is read back before outputs are
published. Failed calls retain the workspace for retry/close and do not
publish modified outputs.
Calls are synchronous and non-reentrant.

Host/library DMA and directory-search continuation are independent. Search
Next rereads its directory sector because the directory buffer is shared.
Current drive/user and allocation state remain shared. Tests interleave
searches across drives and check drive/user changes in both directions.

The typed ABI supports integer values, declared counted byte buffers and
single named, pointer-free flat structs through annotated pointers.
`#pragma dcc bank_record FUNCTION ARG in|out|inout` uses a 1-based argument
index and requires a complete struct of unqualified 8/16/32-bit integers or
fixed one-dimensional integer arrays. The compiler emits resolved field
metadata; `dccmake` generates complete struct definitions and compile-time
size, offset and integer-width checks in the client contract. Typedef
aliases resolve to the underlying named struct; generated prototypes use
its tag. Anonymous/nested structs, pointer members, const/volatile members,
unions, bitfields and by-value aggregate arguments/results are rejected.

Records use the DCC packed, little-endian layout and bounded bulk copies,
not per-field serialization. Fixed-size wrappers use exact-size local
frames, constant offsets and one adapter length check. The canonical
`DIRECTION:rec1le:TAG[SIZE]{FIELD:TYPE@OFFSET;ARRAY:TYPE[COUNT]@OFFSET}`
signature includes full field names, order, widths/signedness, array lengths,
offsets and size; comparing just a tag or size is insufficient. Records and
buffers share the 256-byte body limit and 255-character signature limit.
No loader, linker format or firmware ABI change is needed.

Record pointers cannot be NULL; `const` pointers support only IN. OUT
records start zero-filled. Outputs are copied back only after successful
dispatch, even when the ordinary C result reports an operation failure.
Transport rejection leaves caller outputs unchanged. Copied arguments are
independent snapshots; overlap involving a record is rejected if either
argument is writable. A library must not retain a frame-local pointer after returning.

Buffer direction and bounds must be explicit. Caller addresses never appear in the bank-1 frame;
adapters form bank-1-local pointers into copied bytes. Pointer graphs,
callbacks, varargs, and implicit deep serialization are not supported.

Large objects stay in the bank that owns them. The sample exposes a
validated opaque handle to a 4 KiB allocation and reads at most 128 bytes
per call. Closing an object invalidates its handle; opening another object
uses a fresh token. Unload closes outstanding objects and files. Handles
are meaningful only to the owning library and host runtime, not persistent
identifiers, memory addresses, or a security boundary.

## BDOS and bank switching

The bank-0 shim copies FCBs and DMA records through resident staging buffers,
returns CP/M results, and mirrors pointer-returned disk tables in bank 0.
Bank 1 retains the authoritative allocation vectors. A bank-0 directory
search can continue after a bank-1 library file operation; the proxy keeps a
separate search continuation snapshot. Calls are sequential; the gateway
masks interrupts during a bank switch. Library code must not re-enter the
bank-switch allocator, call bank-0 addresses, or use a bank-0 pointer directly.

The bank-1 BIOS warm-boot vector enters the mirrored `FB40h` trampoline and
returns to bank-0 warm boot. Drive A's system tracks contain the bank-0 BDOS
shim so warm boot restores the same arrangement. The firmware holds the
bank-1 BDOS/loader image; the CP/M boot image supplies the bank-0 shim and
matching gateway. Flash both firmware and CP/M system image for this ABI.

## Current limits

There is one library slot, no runtime relocation, and no nested bank calls.
Library `exit`, `abort`, program chaining, and transient-program startup
paths are outside the supported runtime subset. The host must call its
unload export before invalidating a library with open files. Direct bank-0
BIOS disk calls bypass the BDOS shim; ordinary CP/M applications use BDOS.
The emulator covers cold/warm boot, bank-0 directory and file reads, a bank-1
file read, interrupted directory search, header rejection, and restoration of
bank 0 after calls. Physical board validation remains necessary.
