"""ctypes host of the exact C++ Workshop model; no duplicate gameplay rules."""
from __future__ import annotations
import ctypes as C
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
class Packed(C.LittleEndianStructure):
    _pack_ = 1

class Piece(Packed):
    _fields_ = [("native", C.c_uint8 * 22), ("id", C.c_uint64),
                ("mode", C.c_uint8), ("rank", C.c_uint8),
                ("fifthUnlocked", C.c_uint8), ("ranks", C.c_uint8 * 5),
                ("abilities", C.c_uint64 * 5), ("fifth", C.c_uint16)]

class State(Packed):
    _fields_ = [("version", C.c_uint32), ("revision", C.c_uint64),
                ("nextId", C.c_uint64), ("rng", C.c_uint64), ("rolls", C.c_uint64),
                ("pieces", Piece * 200), ("items", C.c_uint16 * 112)]

class Request(Packed):
    _fields_ = [("op", C.c_uint32), ("revision", C.c_uint64),
                ("pieceId", C.c_uint64), ("otherId", C.c_uint64),
                ("slot", C.c_uint16), ("other", C.c_uint16), ("value", C.c_uint16),
                ("fromSlots", C.c_uint8 * 2), ("to", C.c_uint8 * 2),
                ("count", C.c_uint8), ("policy", C.c_uint8), ("gearTemplate", C.c_uint8 * 22)]

class Plan(Packed):
    _fields_ = [("after", State), ("costs", C.c_uint16 * 112), ("chosenAbility", C.c_uint64)]

assert (C.sizeof(Piece), C.sizeof(State), C.sizeof(Request), C.sizeof(Plan)) == (80, 16260, 62, 16492)
OPS = dict(zip(("swap", "retire", "create", "reforge", "fuse", "expand", "clear",
                "evolve", "mode", "refine", "unlock_fifth", "set_fifth"), range(1, 13)))

class WorkshopError(ValueError):
    pass

class Core:
    def __init__(self, library: Path | None = None):
        self.dll = C.CDLL(str(library or ROOT / "build" / ("workshop.dll" if os.name == "nt" else "libworkshop.so")))
        self.dll.ws_message.argtypes = [C.c_int]
        self.dll.ws_message.restype = C.c_char_p
        self.dll.ws_validate.argtypes = [C.POINTER(State)]
        self.dll.ws_plan.argtypes = [C.POINTER(State), C.POINTER(Request), C.POINTER(Plan)]
        self.dll.ws_import.argtypes = [C.POINTER(C.c_uint8), C.POINTER(C.c_uint16), C.c_uint64, C.POINTER(State)]

    def check(self, code: int):
        if code:
            raise WorkshopError(self.dll.ws_message(code).decode("ascii"))

    def decode(self, raw: bytes) -> State:
        if len(raw) != C.sizeof(State):
            raise WorkshopError("Wrong extension size")
        state = State.from_buffer_copy(raw)
        self.check(self.dll.ws_validate(C.byref(state)))
        return state

    def import_records(self, records: bytes, items: list[int], seed: int) -> State:
        if len(records) != 4400 or len(items) != 112 or any(not 0 <= n <= 255 for n in items):
            raise WorkshopError("Wrong inventory dimensions")
        state = State()
        self.check(self.dll.ws_import((C.c_uint8 * 4400).from_buffer_copy(records),
                                     (C.c_uint16 * 112)(*items), seed, C.byref(state)))
        return state

    def preview(self, state: State, request: Request) -> Plan:
        plan = Plan()
        self.check(self.dll.ws_plan(C.byref(state), C.byref(request), C.byref(plan)))
        return plan

def ability(piece: Piece, slot: int) -> int:
    word = piece.fifth if slot == 4 else int.from_bytes(bytes(piece.native)[14 + 2 * slot:16 + 2 * slot], "little")
    return word or 255
