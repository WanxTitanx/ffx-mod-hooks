#!/usr/bin/env python3
"""Check the design dataset and pure ownership rules; no live/native-game validation."""
import hashlib
import itertools
import json
from copy import deepcopy
from pathlib import Path
from contract_model import valid_loadout, validate, equip, change_mode, award

HERE = Path(__file__).resolve().parent
checks = 0


def check(value, label):
    global checks
    checks += 1
    if not value:
        raise AssertionError(label)


def reject(call, label):
    try:
        call()
    except ValueError:
        check(True, label)
        return
    raise AssertionError(label)


def main():
    raw = (HERE / "cards.proposed.json").read_bytes()
    doc = json.loads(raw)
    rows = doc["cards"]
    catalog = {c["key"]: c for c in rows}
    check(len(rows) == len(catalog) == 78, "78 unique keys")
    check([c["id"] for c in rows] == list(range(78)), "Unique stable catalog IDs")
    check(sum(c["arcana"] == "major" for c in rows) == 22, "22 majors")
    for suit in ("wands", "cups", "swords", "pentacles"):
        check([c["rank"] for c in rows if c["suit"] == suit] == list(range(1, 15)), suit)
    check(rows[0]["printed_number"] == "0", "Fool 0")
    check(rows[8]["name_en"] == "Strength" and rows[8]["printed_number"] == "VIII", "Strength VIII")
    check(rows[11]["name_en"] == "Justice" and rows[11]["printed_number"] == "XI", "Justice XI")
    check(sum(c["printed_number"] is None for c in rows) == 16, "16 unnumbered court cards")
    check(len({c["asset"] for c in rows}) == 78, "78 distinct art paths")
    for c in rows:
        check(c["character_whitelist"] is None and c["copies_per_save"] == 1, "Universal unique card")
        expected = " - ".join(x for x in (c["printed_number"], c["name_en"], c["ffx_reference"]) if x)
        check(c["item_name_en"] == expected, "Item naming convention")
        check('Bottom name plaque text (verbatim): "'+c["printed_name"]+'"' in c["prompt"], "Bottom title in generation prompt")
        check(c["effect_implementation"] in ("NOT_IMPLEMENTED", "TYPED_RUNTIME_HANDLERS_PLAYTEST_PENDING"), "Catalog does not claim live gameplay acceptance")
    majors = [c["key"] for c in rows if c["arcana"] == "major"]
    minors = [c["key"] for c in rows if c["arcana"] == "minor"]
    for mode in ("A", "B"):
        for major_count in range(5):
            for minor_count in range(5):
                selected = majors[:major_count] + minors[:minor_count]
                expected = (major_count + minor_count <= 2) if mode == "A" else (major_count + minor_count <= 3 and 2*major_count + minor_count <= 4)
                check(valid_loadout(selected, mode, catalog) == expected, "A/B cardinality matrix")
    triples = 0
    for chosen in itertools.combinations(catalog, 3):
        expected = sum(catalog[k]["arcana"] == "major" for k in chosen) <= 1
        check(valid_loadout(chosen, "B", catalog) == expected, "Every 3-card B combination")
        triples += 1
    check(not valid_loadout([majors[0], majors[0]], "A", catalog), "Local duplicate denied")
    check(not valid_loadout(["missing"], "B", catalog), "Unknown key denied")
    state = {"mode":"B", "revision":0, "acquired":list(catalog), "actors":{i:[None,None,None] for i in range(7)}}
    state = equip(state, catalog, 0, 0, 0, majors[0])
    snapshot = deepcopy(state)
    reject(lambda: equip(state, catalog, 1, 1, 0, majors[0]), "Cross-owner transfer needs confirmation")
    check(state == snapshot, "Rejected transfer is atomic")
    moved = equip(state, catalog, 1, 1, 0, majors[0], allow_transfer=True)
    check(moved["actors"][0][0] is None and moved["actors"][1][0] == majors[0], "Exactly one owner after transfer")
    check(state == snapshot, "Preview/commit model preserves input")
    reject(lambda: equip(moved, catalog, 1, 0, 0, majors[1]), "Stale revision rejected")
    reject(lambda: equip(moved, catalog, 2, 7, 0, majors[1]), "Unsupported actor rejected")
    reject(lambda: equip(moved, catalog, 2, False, 0, majors[1]), "Boolean actor rejected")
    reject(lambda: equip(moved, catalog, 2, 0, -1, majors[1]), "Negative slot rejected")
    three = equip(moved, catalog, 2, 1, 1, minors[0])
    three = equip(three, catalog, 3, 1, 2, minors[1])
    snapshot = deepcopy(three)
    reject(lambda: equip(three, catalog, 4, 1, 1, majors[1]), "Two majors plus minor rejected")
    check(three == snapshot, "Rejected capacity change is atomic")
    reject(lambda: change_mode(three, catalog, 4, "A"), "No silent third-slot removal")
    resolved = {a:slots[:2] for a,slots in three["actors"].items()}
    a = change_mode(three, catalog, 4, "A", resolved)
    check(a["mode"] == "A" and all(len(s) == 2 for s in a["actors"].values()), "Explicit A resolution")
    reject(lambda: change_mode(three, catalog, 4, "A", {1:resolved[1]}), "Partial mode resolution denied")
    b = change_mode(a, catalog, 5, "B")
    check(all(len(s) == 3 and s[2] is None for s in b["actors"].values()), "A-to-B preserves two slots")
    corrupt = deepcopy(b); corrupt["actors"][0][0] = majors[0]
    reject(lambda: validate(corrupt, catalog), "Global duplication denied")
    empty = {"mode":"A", "revision":0, "acquired":[], "actors":{i:[None,None] for i in range(7)}}
    first = award(empty, catalog, minors[0])
    check(award(first, catalog, minors[0]) == first, "Award idempotency")
    reject(lambda: equip(first, catalog, 1, 0, 0, majors[0]), "Unacquired card denied")
    report = {"result":"PASS_RT0_MODEL_ONLY", "checks":checks, "three_card_combinations":triples,
              "catalog_sha256":hashlib.sha256(raw).hexdigest(), "cards":78, "major":22, "minor":56,
              "court_cards_unnumbered":16, "game_io":False, "native_functions_executed":False,
              "limits":["No native Equip injection", "No gameplay effects", "No native save writer or crash consistency proof", "No graphics upload or RT2"]}
    (HERE / "contract-validation.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))


if __name__ == "__main__":
    main()
