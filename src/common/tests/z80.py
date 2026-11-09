"""ctypes front end to the project's C Z80 emulator (libz80sbc-emulator).

Build the library with:
    cmake -S src/cpm/z80_debug_host -B build/cpm/z80_debug_host \
        -DZ80_DEBUG_HOST_BUILD_EXAMPLES=OFF
    cmake --build build/cpm/z80_debug_host --target z80sbc-emulator

The library holds one machine per process: constructing Z80Machine resets it.
Bank selection is modeled in C; see machine.bank() and machine.active_bank.
"""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

_REPO = Path(__file__).resolve().parents[3]
_SUFFIX = {"darwin": ".dylib", "win32": ".dll"}.get(sys.platform, ".so")
_PREFIX = "" if sys.platform == "win32" else "lib"


def _load() -> ctypes.CDLL:
    override = os.environ.get("Z80SBC_EMULATOR")
    path = Path(override) if override else (
        _REPO / "build/cpm/z80_debug_host" / f"{_PREFIX}z80sbc-emulator{_SUFFIX}")
    if not path.exists():
        raise ImportError(f"{path} not found; build the z80sbc-emulator CMake target")
    return ctypes.CDLL(str(path))


_lib = _load()
_IN = ctypes.CFUNCTYPE(ctypes.c_uint8, ctypes.c_uint16, ctypes.c_void_p)
_OUT = ctypes.CFUNCTYPE(None, ctypes.c_uint16, ctypes.c_uint8, ctypes.c_void_p)
_WRITE = ctypes.CFUNCTYPE(None, ctypes.c_uint16, ctypes.c_uint8, ctypes.c_void_p)

_lib.sbc_memory.restype = ctypes.POINTER(ctypes.c_uint8)
_lib.sbc_bank_memory.restype = ctypes.POINTER(ctypes.c_uint8)
_lib.sbc_bank_memory.argtypes = [ctypes.c_int]
_lib.sbc_select_bank.argtypes = [ctypes.c_int]
_lib.sbc_get_reg.restype = ctypes.c_uint16
_lib.sbc_get_reg.argtypes = [ctypes.c_int]
_lib.sbc_set_reg.argtypes = [ctypes.c_int, ctypes.c_uint16]
_lib.sbc_run.restype = ctypes.c_uint32
_lib.sbc_run.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
_lib.sbc_add_breakpoint.argtypes = [ctypes.c_uint16]
_lib.sbc_remove_breakpoint.argtypes = [ctypes.c_uint16]
_lib.sbc_set_halted.argtypes = [ctypes.c_int]
_lib.sbc_set_io.argtypes = [_IN, _OUT, ctypes.c_void_p]
_lib.sbc_set_write_hook.argtypes = [_WRITE, ctypes.c_void_p]
_lib.sbc_watch.argtypes = [ctypes.c_uint16, ctypes.c_uint32, ctypes.c_int]

(_AF, _BC, _DE, _HL, _IX, _IY, _SP, _PC, _AF2, _BC2, _DE2, _HL2,
 _I, _R, _IFF1, _IFF2, _IM) = range(17)


def _view(pointer) -> memoryview:
    return memoryview((ctypes.c_uint8 * 65536).from_address(
        ctypes.addressof(pointer.contents))).cast("B")


def _pair(register: int, doc: str) -> property:
    return property(lambda self: _lib.sbc_get_reg(register),
                    lambda self, value: _lib.sbc_set_reg(register, value & 0xFFFF), doc=doc)


def _byte(register: int, shift: int) -> property:
    mask = 0xFF << shift

    def get(self) -> int:
        return (_lib.sbc_get_reg(register) >> shift) & 0xFF

    def put(self, value: int) -> None:
        current = _lib.sbc_get_reg(register) & ~mask & 0xFFFF
        _lib.sbc_set_reg(register, current | ((value & 0xFF) << shift))

    return property(get, put)


class Z80Machine:
    _TICKS_HIT, _HALT_HIT, _BREAKPOINT_HIT = 1, 2, 4
    WRITE_MARK = 1

    af, bc, de, hl = (_pair(r, n) for r, n in ((_AF, "af"), (_BC, "bc"), (_DE, "de"), (_HL, "hl")))
    ix, iy, sp, pc = (_pair(r, n) for r, n in ((_IX, "ix"), (_IY, "iy"), (_SP, "sp"), (_PC, "pc")))
    a, f = _byte(_AF, 8), _byte(_AF, 0)
    b, c = _byte(_BC, 8), _byte(_BC, 0)
    d, e = _byte(_DE, 8), _byte(_DE, 0)
    h, l = _byte(_HL, 8), _byte(_HL, 0)
    iff1 = property(lambda self: bool(_lib.sbc_get_reg(_IFF1)),
                    lambda self, value: _lib.sbc_set_reg(_IFF1, bool(value)))
    iff2 = property(lambda self: bool(_lib.sbc_get_reg(_IFF2)),
                    lambda self, value: _lib.sbc_set_reg(_IFF2, bool(value)))
    halted = property(lambda self: bool(_lib.sbc_halted()),
                      lambda self, value: _lib.sbc_set_halted(bool(value)))

    def __init__(self) -> None:
        _lib.sbc_reset()
        self.memory = _view(_lib.sbc_memory())
        self.ticks_to_stop = 0
        self._error: BaseException | None = None
        self._in = _IN(lambda port, _context: 0xFF)
        self._out = _OUT(lambda port, value, _context: None)
        self._install()

    def _install(self) -> None:
        _lib.sbc_set_io(self._in, self._out, None)

    def set_input_callback(self, callback) -> None:
        def guarded(port: int, _context) -> int:
            try:
                return callback(port) & 0xFF
            except BaseException as error:
                self._fail(error)
                return 0xFF

        self._in = _IN(guarded)
        self._install()

    def set_output_callback(self, callback) -> None:
        def guarded(port: int, value: int, _context) -> None:
            try:
                callback(port, value)
            except BaseException as error:
                self._fail(error)

        self._out = _OUT(guarded)
        self._install()

    def set_write_callback(self, callback) -> None:
        """Report stores to addresses passed to mark_addr; see sbc_set_write_hook."""
        def guarded(address: int, value: int, _context) -> None:
            try:
                callback(address, value)
            except BaseException as error:
                self._fail(error)

        self._write = _WRITE(guarded)
        _lib.sbc_set_write_hook(self._write, None)

    def mark_addr(self, address: int, mark: int) -> None:
        _lib.sbc_watch(address, 1, bool(mark & self.WRITE_MARK))

    def mark_addrs(self, address: int, count: int, mark: int) -> None:
        _lib.sbc_watch(address, count, bool(mark & self.WRITE_MARK))

    def _fail(self, error: BaseException) -> None:
        self._error = self._error or error
        _lib.sbc_set_halted(1)

    def run(self) -> int:
        used = ctypes.c_uint32()
        events = _lib.sbc_run(self.ticks_to_stop, ctypes.byref(used))
        self.ticks_to_stop = max(0, self.ticks_to_stop - used.value)
        error, self._error = self._error, None
        if error:
            raise error
        return events

    def set_breakpoint(self, address: int) -> None:
        if not _lib.sbc_add_breakpoint(address & 0xFFFF):
            raise RuntimeError("too many breakpoints")

    def clear_breakpoint(self, address: int) -> None:
        _lib.sbc_remove_breakpoint(address & 0xFFFF)

    def step_over_breakpoint(self) -> None:
        """Execute one instruction without stopping at a breakpoint."""
        used = ctypes.c_uint32()
        _lib.sbc_run(1, ctypes.byref(used))
        self.ticks_to_stop = max(0, self.ticks_to_stop - used.value)
        error, self._error = self._error, None
        if error:
            raise error

    def clear_breakpoints(self) -> None:
        _lib.sbc_clear_breakpoints()

    @property
    def active_bank(self) -> int:
        return _lib.sbc_active_bank()

    def select_bank(self, index: int) -> None:
        _lib.sbc_select_bank(index)

    def bank(self, index: int) -> memoryview:
        """The 64 KiB contents of one SRAM bank, active or parked."""
        return _view(_lib.sbc_bank_memory(index))
