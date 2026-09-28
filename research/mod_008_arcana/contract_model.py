"""Offline ownership model for MOD-008. Does not read/write game files or install hooks."""
from copy import deepcopy


def valid_loadout(cards, mode, catalog):
    if mode not in ("A", "B") or len(cards) != len(set(cards)):
        return False
    if any(key not in catalog for key in cards):
        return False
    if mode == "A":
        return len(cards) <= 2
    return len(cards) <= 3 and sum(2 if catalog[k]["arcana"] == "major" else 1 for k in cards) <= 4


def validate(state, catalog):
    if state["mode"] not in ("A", "B") or type(state["revision"]) is not int or state["revision"] < 0:
        raise ValueError("Invalid mode or revision")
    if len(state["acquired"]) != len(set(state["acquired"])) or any(k not in catalog for k in state["acquired"]):
        raise ValueError("Invalid collection")
    seen = set()
    for actor, slots in state["actors"].items():
        if type(actor) is not int or actor not in range(7):
            raise ValueError("Actor is outside the initial permanent-party roster")
        expected = 2 if state["mode"] == "A" else 3
        if len(slots) != expected:
            raise ValueError("Invalid slot count")
        active = [k for k in slots if k is not None]
        if not valid_loadout(active, state["mode"], catalog):
            raise ValueError("Invalid actor loadout")
        for key in active:
            if key not in state["acquired"] or key in seen:
                raise ValueError("Unowned or globally duplicated card")
            seen.add(key)
    return True


def equip(state, catalog, expected_revision, actor, slot, key, allow_transfer=False):
    """Return a new state only after full validation. Existing state is never mutated."""
    validate(state, catalog)
    if type(expected_revision) is not int or state["revision"] != expected_revision:
        raise ValueError("Stale preview")
    if type(actor) is not int or actor not in state["actors"] or type(slot) is not int or not 0 <= slot < len(state["actors"][actor]):
        raise ValueError("Invalid destination")
    if key is not None and key not in state["acquired"]:
        raise ValueError("Card not acquired")
    result = deepcopy(state)
    if key is not None:
        for owner, slots in result["actors"].items():
            for position, existing in enumerate(slots):
                if existing != key:
                    continue
                if owner != actor and not allow_transfer:
                    raise ValueError("Explicit transfer confirmation required")
                slots[position] = None
    result["actors"][actor][slot] = key
    validate(result, catalog)
    result["revision"] += 1
    return result


def change_mode(state, catalog, expected_revision, mode, resolved_actors=None):
    validate(state, catalog)
    if type(expected_revision) is not int or state["revision"] != expected_revision or mode not in ("A", "B"):
        raise ValueError("Invalid mode request")
    result = deepcopy(state)
    if resolved_actors is not None:
        if set(resolved_actors) != set(state["actors"]):
            raise ValueError("Resolve every actor together")
        # Changing modes may free cards, but never silently introduce/transfer them.
        for actor, slots in resolved_actors.items():
            if not {k for k in slots if k is not None}.issubset({k for k in state["actors"][actor] if k is not None}):
                raise ValueError("Mode resolution cannot introduce or transfer cards")
        result["actors"] = deepcopy(resolved_actors)
    elif mode != state["mode"]:
        for actor, slots in result["actors"].items():
            if mode == "B":
                slots.append(None)
            elif slots[2] is not None:
                raise ValueError("Explicit third-slot resolution required")
            else:
                result["actors"][actor] = slots[:2]
    result["mode"] = mode
    validate(result, catalog)
    result["revision"] += 1
    return result


def award(state, catalog, key):
    validate(state, catalog)
    if key not in catalog:
        raise ValueError("Unknown card")
    if key in state["acquired"]:
        return deepcopy(state)
    result = deepcopy(state)
    result["acquired"].append(key)
    result["revision"] += 1
    validate(result, catalog)
    return result
