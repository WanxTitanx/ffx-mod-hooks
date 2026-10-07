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

class Policy(Packed):
    _fields_ = [(name, C.c_uint32) for name in ("mode", "baseItem", "baseAmount", "refinementDivisor",
                                               "fusionDivisor", "fusionGilPerAbility", "modRecipeQuantity", "devFreeMaterials", "devFreeGil", "devIgnoreProgression")]
    def __init__(self, **values):
        super().__init__()
        defaults = dict(mode=2, baseItem=70, baseAmount=1, refinementDivisor=60,
                        fusionDivisor=3, fusionGilPerAbility=10000, modRecipeQuantity=30, devFreeMaterials=0, devFreeGil=0, devIgnoreProgression=0)
        if values.keys() - defaults.keys():
            raise ValueError("Unknown Workshop policy field")
        defaults.update(values)
        for name, value in defaults.items():
            if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
                raise ValueError("Invalid Workshop policy integer")
            setattr(self, name, value)

class AeonProgress(Packed):
    _fields_ = [("obtained", C.c_uint32), ("crests", C.c_uint32), ("gear", C.c_uint8 * 20)]
    def __init__(self):
        super().__init__()
        self.gear[:] = [255] * 20

class CatalogEntry(Packed):
    _fields_ = [(name, C.c_uint16) for name in ("word", "kind", "item", "reserved")]

class Catalog(Packed):
    _fields_ = [("proof", C.c_uint64), ("entries", CatalogEntry * 13)]

class Economy(Packed):
    _fields_ = [("policy", Policy), ("gil", C.c_uint32), ("customizeUnlocked", C.c_uint32), ("aeons", AeonProgress), ("catalog", Catalog)]
    def __init__(self, gil=0, policy=None, customize_unlocked=False, aeons=None, catalog=None):
        super().__init__()
        if type(gil) is not int or not 0 <= gil <= 0xFFFFFFFF:
            raise ValueError("Invalid native Gil value")
        self.policy = policy if policy is not None else Policy()
        self.gil = gil
        if type(customize_unlocked) is not bool:
            raise ValueError("Customize admission must be an observed boolean")
        self.customizeUnlocked = int(customize_unlocked)
        self.aeons = aeons if aeons is not None else AeonProgress()
        self.catalog = catalog if catalog is not None else Catalog()

class Plan(Packed):
    _fields_ = [("after", State), ("costs", C.c_uint16 * 112), ("chosenAbility", C.c_uint64),
                ("requirements", C.c_uint16 * 112), ("gilBefore", C.c_uint32),
                ("gilCost", C.c_uint32), ("gilDebit", C.c_uint32), ("customizeUnlocked", C.c_uint32), ("policy", Policy), ("aeons", AeonProgress), ("catalog", Catalog)]

assert (C.sizeof(Piece), C.sizeof(State), C.sizeof(Request), C.sizeof(Plan)) == (80, 16260, 62, 16912)
assert (C.sizeof(Policy), C.sizeof(Catalog), C.sizeof(Economy)) == (40, 112, 188)
OPS = dict(zip(("swap", "retire", "create", "reforge", "fuse", "expand", "clear",
                "evolve", "mode", "refine", "unlock_fifth", "set_fifth"), range(1, 13)))

class WorkshopError(ValueError):
    pass

class Core:
    def __init__(self, library: Path | None = None):
        self.dll = C.CDLL(str(library or ROOT / "build" / ("workshop.dll" if os.name == "nt" else "libworkshop.so")))
        try:
            self.dll.ws_plan_abi.restype = C.c_uint
            if self.dll.ws_plan_abi() != 5:
                raise WorkshopError("Workshop library/host ABI mismatch; rebuild the core")
        except AttributeError as error:
            raise WorkshopError("Workshop library is older than this host; rebuild the core") from error
        self.dll.ws_customize_unlocked.argtypes = [C.c_uint]
        self.dll.ws_customize_unlocked.restype = C.c_uint
        self.dll.ws_message.argtypes = [C.c_int]
        self.dll.ws_message.restype = C.c_char_p
        self.dll.ws_validate.argtypes = [C.POINTER(State)]
        self.dll.ws_plan_economy_v5.argtypes = [C.POINTER(State), C.POINTER(Request), C.POINTER(Economy), C.POINTER(Plan)]
        self.dll.ws_aeon_progress.argtypes = [C.POINTER(C.c_uint8), C.c_size_t, C.POINTER(AeonProgress)]
        self.dll.ws_aeon_access.argtypes = [C.POINTER(Piece), C.c_uint, C.POINTER(AeonProgress)]
        self.dll.ws_import.argtypes = [C.POINTER(C.c_uint8), C.POINTER(C.c_uint16), C.c_uint64, C.POINTER(State)]
        try:
            self.dll.ws_fifth_cost.argtypes = [C.c_uint, C.c_uint, C.POINTER(Policy), C.POINTER(C.c_uint), C.POINTER(C.c_uint)]
            self.dll.ws_fifth_cost.restype = C.c_uint
            self.dll.ws_customize_eligibility.argtypes = [C.POINTER(Piece), C.c_uint, C.c_uint]
            self.dll.ws_customize_eligibility.restype = C.c_int
            self.dll.ws_fifth_cost_v5.argtypes = [C.c_uint, C.c_uint, C.POINTER(Policy), C.POINTER(Catalog), C.POINTER(C.c_uint), C.POINTER(C.c_uint)]
            self.dll.ws_fifth_cost_v5.restype = C.c_uint
            self.dll.ws_customize_eligibility_v5.argtypes = [C.POINTER(Piece), C.c_uint, C.c_uint, C.POINTER(Catalog)]
            self.dll.ws_customize_eligibility_v5.restype = C.c_int
        except AttributeError as error:
            raise WorkshopError("Workshop catalog query is missing; rebuild the core") from error

    def fifth_cost(self, kind: int, word: int, policy: Policy, catalog: Catalog | None = None) -> tuple[int, int] | None:
        if type(kind) is not int or kind not in (0, 1) or type(word) is not int or not 0 <= word <= 65535:
            return None
        item, quantity = C.c_uint(), C.c_uint()
        catalog = catalog if catalog is not None else Catalog()
        if self.dll.ws_fifth_cost_v5(kind, word, C.byref(policy), C.byref(catalog), C.byref(item), C.byref(quantity)):
            return item.value, quantity.value
        return None

    def can_customize(self, piece: Piece, slot: int, word: int, catalog: Catalog | None = None) -> bool:
        catalog = catalog if catalog is not None else Catalog()
        return type(slot) is int and 0 <= slot <= 4 and type(word) is int and 0 <= word <= 65535 and self.dll.ws_customize_eligibility_v5(C.byref(piece), slot, word, C.byref(catalog)) == 0

    def customize_unlocked(self, story: int) -> bool:
        return type(story) is int and 0 <= story <= 65535 and bool(self.dll.ws_customize_unlocked(story))

    def aeon_progress(self, save: bytes) -> AeonProgress:
        if len(save) != 26880:
            raise WorkshopError("Wrong native save size for Aeon progression")
        payload = save[64:]
        progress = AeonProgress()
        self.check(self.dll.ws_aeon_progress((C.c_uint8 * len(payload)).from_buffer_copy(payload), len(payload), C.byref(progress)))
        return progress

    def aeon_access(self, piece: Piece, slot: int, progress: AeonProgress) -> int:
        return self.dll.ws_aeon_access(C.byref(piece), slot, C.byref(progress))

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

    def quote(self, state: State, request: Request, economy: Economy | None = None) -> tuple[int, Plan]:
        plan = Plan()
        economy = economy if economy is not None else Economy()
        code = self.dll.ws_plan_economy_v5(C.byref(state), C.byref(request), C.byref(economy), C.byref(plan))
        return code, plan

    def preview(self, state: State, request: Request, economy: Economy | None = None) -> Plan:
        code, plan = self.quote(state, request, economy)
        self.check(code)
        return plan

def ability(piece: Piece, slot: int) -> int:
    word = piece.fifth if slot == 4 else int.from_bytes(bytes(piece.native)[14 + 2 * slot:16 + 2 * slot], "little")
    return word or 255
