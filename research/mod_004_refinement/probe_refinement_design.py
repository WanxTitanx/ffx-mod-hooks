#!/usr/bin/env python3
"""RT0 model for MOD-004 refinement. All ingredients are proposed, never vanilla facts."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import random
import re
import struct
from dataclasses import dataclass
from pathlib import Path

ABILITY_SOURCE_SHA = "bfad6da0f4e5d299f20497af9eba5916ac3aef687b08a6f663ce8f103f461dfa"
ITEM_SOURCE_SHA = "6bb4f04c6318384763d02af66ad37ad9b220ce3aced0434a06f135c2e7a0b08d"
KERNEL_SHA = "d0610e7d37cde6e65298da116f6dc05a69236126d9ff157c2db65d17a97729aa"
MAX_RANK = 10
MILESTONES = {4, 7, 10}
UNRESOLVED = {20: "No AP", 29: "No Encounters", 123: "Aeon Immunities"}
CATALOG_TSV = Path(__file__).with_name("ability_ingredient_candidates.tsv")


@dataclass(frozen=True)
class Candidate:
    family: str
    base_item: int | None
    milestone_item: int | None
    effect_gate: str


def catalog() -> dict[int, Candidate]:
    rows: dict[int, Candidate] = {}

    def add(ids: range | tuple[int, ...], family: str, base: int | None,
            milestone: int | None, gate: str) -> None:
        for ability_id in ids:
            if ability_id in rows:
                raise ValueError(f"Duplicate ability id {ability_id}")
            rows[ability_id] = Candidate(family, base, milestone, gate)

    opening = {
        0: ("SENSOR", 100, 75, "FIELD_UI"),
        1: ("OPENING_CTB", 54, 55, "CTB"),
        2: ("OPENING_CTB", 54, 55, "CTB"),
        3: ("COUNTER", 88, 75, "COUNTER_EVENT"),
        4: ("COUNTER", 92, 75, "COUNTER_EVENT"),
        5: ("COUNTER", 90, 75, "COUNTER_EVENT"),
        6: ("MAGIC_BOOST", 60, 69, "DAMAGE_MP"),
        7: ("ALCHEMY", 21, 75, "ITEM_RESULT"),
        8: ("AUTO_ITEM", 0, 2, "AUTO_ITEM_RESULT"),
        9: ("AUTO_ITEM", 15, 63, "AUTO_ITEM_RESULT"),
        10: ("AUTO_ITEM", 6, 7, "AUTO_ITEM_RESULT"),
        11: ("PIERCING", 87, 53, "ARMOR_DAMAGE"),
        12: ("MP_ECONOMY", 66, 69, "MP_COST"),
        13: ("MP_ECONOMY", 69, 69, "MP_COST"),
        14: ("OVERDRIVE", 107, 111, "OD_AWARD"),
        15: ("OVERDRIVE", 111, 111, "OD_AWARD"),
        16: ("OVERDRIVE", 107, 111, "OD_AWARD"),
        17: ("OVERDRIVE_TO_AP", 107, 111, "AP_AWARD"),
        18: ("AP_ECONOMY", 74, 111, "AP_AWARD"),
        19: ("AP_ECONOMY", 111, 111, "AP_AWARD"),
        21: ("STEAL", 105, 106, "STEAL_ROLL"),
        22: ("STEAL", 106, 106, "STEAL_ROLL"),
        23: ("BREAK_LIMIT", 85, 108, "MAX_HP"),
        24: ("BREAK_LIMIT", 86, 69, "MAX_MP"),
        25: ("BREAK_LIMIT", 87, 53, "DAMAGE_CAP"),
        26: ("GIL", 52, 111, "GIL_AWARD"),
        27: ("FIELD_REGEN", 59, 21, "FIELD_HP"),
        28: ("FIELD_REGEN", 60, 8, "FIELD_MP"),
    }
    for ability_id, (family, base, milestone, gate) in opening.items():
        add((ability_id,), family, base, milestone, gate)

    # Each elemental group is Strike, Ward, Proof, Eater.
    for start, basic, advanced, gem in (
        (30, 26, 27, 28), (34, 23, 24, 25),
        (38, 29, 30, 31), (42, 32, 33, 34),
    ):
        for offset, family in enumerate(("ELEMENT_STRIKE", "ELEMENT_WARD",
                                         "ELEMENT_PROOF", "ELEMENT_EATER")):
            add((start + offset,), family, basic if offset < 2 else advanced,
                advanced if offset == 1 else gem, "ELEMENT_RESULT")

    # Eight status groups are Strike, Touch, Proof, Ward.
    for start, basic, advanced in (
        (46, 50, 51), (50, 14, 63), (54, 49, 53), (58, 45, 15),
        (62, 38, 38), (66, 39, 39), (70, 40, 40), (74, 46, 47),
    ):
        for offset, family in enumerate(("STATUS_STRIKE", "STATUS_TOUCH",
                                         "STATUS_PROOF", "STATUS_WARD")):
            add((start + offset,), family, basic, advanced, "STATUS_RESULT")
    for start, item in ((78, 102), (80, 103), (82, 63)):
        add((start,), "STATUS_PROOF", item, item, "STATUS_RESULT")
        add((start + 1,), "STATUS_WARD", item, item, "STATUS_RESULT")

    # Auto and SOS statuses require new numeric side effects, not status bits x2.
    for ability_id, basic, advanced in (
        (84, 56, 56), (85, 57, 57), (86, 54, 55), (87, 59, 21),
        (88, 58, 58), (89, 56, 56), (90, 57, 57), (91, 54, 55),
        (92, 59, 21), (93, 58, 58),
    ):
        add((ability_id,), "AUTO_STATUS" if ability_id <= 88 else "SOS_STATUS",
            basic, advanced, "STATUS_SCALAR")
    for ability_id, basic, advanced in (
        (94, 32, 34), (95, 23, 25), (96, 29, 31), (97, 26, 28),
    ):
        add((ability_id,), "SOS_NUL", basic, advanced, "ELEMENT_RESULT")

    for start, sphere in ((98, 87), (102, 89), (106, 88),
                          (110, 90), (114, 85), (118, 86)):
        add(range(start, start + 4), "STAT_PERCENT", sphere, 75, "STAT_RECALC")
    add((122,), "CAPTURE", 73, 109, "CAPTURE_REWARD")
    for ability_id, distiller, sphere in (
        (124, 16, 70), (125, 17, 71), (126, 18, 72), (127, 19, 73),
    ):
        add((ability_id,), "DISTILL", distiller, sphere, "DROP_REWARD")
    add((128,), "RIBBON", 15, 53, "STATUS_DAMAGE")
    add((129, 130), "DROP", 106, 108, "DROP_REWARD")
    for ability_id in UNRESOLVED:
        add((ability_id,), "UNRESOLVED", None, None, "SPEC_REQUIRED")
    if set(rows) != set(range(131)):
        raise ValueError(f"Coverage failure: {sorted(set(range(131)) - set(rows))}")
    return rows


def source_dictionary(path: Path, expected_sha: str) -> dict[int, str]:
    raw = path.read_bytes()
    actual_sha = hashlib.sha256(raw).hexdigest()
    if actual_sha != expected_sha:
        raise ValueError(f"Source identity changed: {path} sha256={actual_sha}")
    return {int(key): name for key, name in re.findall(
        r'\{\s*(\d+),\s*"([^"]+)"\s*\}', raw.decode("utf-8-sig"))}


def render_catalog(names: dict[int, str], items: dict[int, str],
                   rows: dict[int, Candidate]) -> str:
    lines = ["ability_id\tability\tfamily\tbase_item_id\tbase_item\t"
             "milestone_item_id\tmilestone_item\teffect_gate\tstatus"]
    for ability_id in sorted(rows):
        row = rows[ability_id]
        base = row.base_item
        milestone = row.milestone_item
        lines.append("\t".join((
            str(ability_id), names[ability_id], row.family,
            "" if base is None else str(base), "" if base is None else items[base],
            "" if milestone is None else str(milestone),
            "" if milestone is None else items[milestone], row.effect_gate,
            "UNRESOLVED" if ability_id in UNRESOLVED else "PROPOSAL",
        )))
    return "\n".join(lines) + "\n"


def ability_cost(ability_id: int, rank: int,
                 rows: dict[int, Candidate]) -> collections.Counter[int]:
    if not 1 <= rank <= MAX_RANK:
        raise ValueError("Rank must be 1..10")
    row = rows[ability_id]
    if row.base_item is None or row.milestone_item is None:
        raise ValueError(f"Ability {ability_id} has no honest effect/cost design")
    result = collections.Counter({row.base_item: 1 + (rank - 1) // 3})
    if rank in MILESTONES:
        result[row.milestone_item] += 1
    return result


def global_cost(ability_ids: tuple[int, ...], rank: int,
                rows: dict[int, Candidate]) -> collections.Counter[int]:
    if not 1 <= len(ability_ids) <= 5:
        raise ValueError("Expected 1..5 occupied ability slots")
    total: collections.Counter[int] = collections.Counter()
    for ability_id in ability_ids:  # Duplicates pay independently.
        total.update(ability_cost(ability_id, rank, rows))
    return total


def catch_up(ability_id: int, gear_rank: int,
             rows: dict[int, Candidate]) -> collections.Counter[int]:
    total: collections.Counter[int] = collections.Counter()
    for rank in range(1, gear_rank + 1):
        total.update(ability_cost(ability_id, rank, rows))
    return total


def global_step(ability_ids: tuple[int, ...], rank: int,
                inventory: dict[int, int], rows: dict[int, Candidate]
                ) -> tuple[int, dict[int, int], collections.Counter[int] | None]:
    if not 0 <= rank <= MAX_RANK:
        raise ValueError("Gear rank must be 0..10")
    if rank == MAX_RANK:
        return rank, inventory.copy(), None
    cost = global_cost(ability_ids, rank + 1, rows)
    if any(inventory.get(item_id, 0) < amount for item_id, amount in cost.items()):
        return rank, inventory.copy(), None
    updated = inventory.copy()
    for item_id, amount in cost.items():
        updated[item_id] -= amount
    return rank + 1, updated, cost


def random_step(ability_ids: tuple[int, ...], ranks: tuple[int, ...], owner: str,
                inventory: dict[str, int], seed: int,
                rows: dict[int, Candidate]) -> tuple[tuple[int, ...], dict[str, int], int | None]:
    if len(ability_ids) != len(ranks) or not 1 <= len(ranks) <= 5:
        raise ValueError("One rank per occupied ability slot is required")
    if any(not 0 <= rank <= MAX_RANK for rank in ranks):
        raise ValueError("Every ability rank must be 0..10")
    if any(rows[ability_id].effect_gate == "SPEC_REQUIRED" for ability_id in ability_ids):
        raise ValueError("Unresolved ability prevents refinement of whole item")
    eligible = [slot for slot, rank in enumerate(ranks) if 0 <= rank < MAX_RANK]
    if not eligible:
        return ranks, inventory.copy(), None
    # Character catalyst is virtual in this probe: no new game item ID is claimed.
    cost = {f"CATALYST_{owner}": 1, "ITEM_73": 1}  # Ability Sphere id 73.
    if any(inventory.get(key, 0) < amount for key, amount in cost.items()):
        return ranks, inventory.copy(), None
    updated_inventory = inventory.copy()
    for key, amount in cost.items():
        updated_inventory[key] -= amount
    selected = eligible[random.Random(seed).randrange(len(eligible))]
    updated_ranks = list(ranks)
    updated_ranks[selected] += 1
    return tuple(updated_ranks), updated_inventory, selected


def kernel_evidence(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    actual_sha = hashlib.sha256(data).hexdigest()
    if actual_sha != KERNEL_SHA:
        raise ValueError(f"Kernel identity changed: sha256={actual_sha}")
    assert struct.unpack_from("<HHH", data, 0x08) == (0, 133, 0x6C)
    def word(ability_id: int, offset: int) -> int:
        return struct.unpack_from("<H", data, 0x14 + ability_id * 0x6C + offset)[0]
    assert word(85, 0x5A) == 0x0010  # Auto-Protect is a temporal status bit.
    assert word(86, 0x5A) == 0x0800  # Auto-Haste is a temporal status bit.
    assert data[0x14 + 100 * 0x6C + 0x55] == 10  # Strength +10% numeric amount.
    assert word(100, 0x56) == 0x0400
    return {"sha256": actual_sha, "rows": 134, "auto_protect_bit": "0x0010",
            "auto_haste_bit": "0x0800", "strength_10_amount": 10,
            "rows_without_names_in_editor_dictionary": [131, 132, 133]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor-root", type=Path, required=True)
    parser.add_argument("--ability-bin", type=Path)
    catalog_mode = parser.add_mutually_exclusive_group()
    catalog_mode.add_argument("--write-catalog", action="store_true")
    catalog_mode.add_argument("--check-catalog", action="store_true")
    args = parser.parse_args()
    root = args.editor_root
    names = source_dictionary(root / "FFXProjectEditor/FfxLib/Dictionaries/AutoAbility_Dictionary.cs",
                              ABILITY_SOURCE_SHA)
    items = source_dictionary(root / "FFXProjectEditor/FfxLib/Dictionaries/Item_Dictionary.cs",
                              ITEM_SOURCE_SHA)
    rows = catalog()
    assert set(names) == set(rows) == set(range(131))
    assert set(items) == set(range(112))
    assert all(row.base_item in items and row.milestone_item in items
               for row in rows.values() if row.effect_gate != "SPEC_REQUIRED")
    catalog_text = render_catalog(names, items, rows)
    if args.write_catalog:
        CATALOG_TSV.write_text(catalog_text, encoding="utf-8")
    if args.check_catalog and (
        not CATALOG_TSV.exists() or CATALOG_TSV.read_text(encoding="utf-8") != catalog_text
    ):
        raise ValueError("Generated candidate catalog differs from pinned dictionaries/rules")
    sample = (86, 85, 115, 128)  # Auto-Haste, Auto-Protect, HP +10%, Ribbon.
    first = global_cost(sample, 1, rows)
    last = global_cost(sample, 10, rows)
    assert sum(first.values()) == 4
    assert max(last.values()) <= 99
    assert catch_up(86, 10, rows) == collections.Counter({54: 22, 55: 3})
    assert max(global_cost((86,) * 5, 10, rows).values()) <= 99
    assert global_step(sample, 0, {}, rows) == (0, {}, None)
    paid_rank, paid_inventory, paid_cost = global_step(sample, 0, dict(first), rows)
    assert paid_rank == 1 and all(amount == 0 for amount in paid_inventory.values())
    assert paid_cost == first
    assert global_step(sample, 10, dict(first), rows) == (10, dict(first), None)
    before = {"CATALYST_Tidus": 1, "ITEM_73": 1}
    ranks, after, chosen = random_step(sample, (9, 10, 9, 10), "Tidus", before, 23, rows)
    assert chosen in (0, 2) and sum(ranks) == 39
    assert after == {"CATALYST_Tidus": 0, "ITEM_73": 0}
    failed = random_step(sample, (9, 10, 9, 10), "Tidus", {}, 23, rows)
    assert failed == ((9, 10, 9, 10), {}, None)
    capped = random_step(sample, (10, 10, 10, 10), "Tidus", before, 23, rows)
    assert capped == ((10, 10, 10, 10), before, None)
    maxima: dict[int, int] = {}
    for slot_count in (4, 5):
        ids = (86, 85, 115, 128, 0)[:slot_count]
        levels = (0,) * slot_count
        remaining = {"CATALYST_Tidus": 10 * slot_count,
                     "ITEM_73": 10 * slot_count}
        for attempt in range(10 * slot_count):
            levels, remaining, selected = random_step(
                ids, levels, "Tidus", remaining, attempt, rows)
            assert selected is not None and sum(levels) == attempt + 1
            assert all(0 <= level <= MAX_RANK for level in levels)
        assert levels == (10,) * slot_count
        assert remaining == {"CATALYST_Tidus": 0, "ITEM_73": 0}
        maxima[slot_count] = sum(levels)
    try:
        ability_cost(29, 1, rows)
    except ValueError:
        pass
    else:
        raise AssertionError("No Encounters must remain unresolved")
    try:
        random_step((86, 29), (0, 0), "Tidus", before, 23, rows)
    except ValueError:
        pass
    else:
        raise AssertionError("Unresolved ability must block random mode too")
    result: dict[str, object] = {
        "verdict": "PASS_RT0_RULE_MODEL",
        "named_abilities_classified": len(rows),
        "vanilla_items_resolved": len(items),
        "candidate_catalog_sha256": hashlib.sha256(catalog_text.encode("utf-8")).hexdigest(),
        "unresolved_ability_ids": sorted(UNRESOLVED),
        "family_counts": dict(sorted(collections.Counter(
            row.family for row in rows.values()).items())),
        "sample_abilities": [names[ability_id] for ability_id in sample],
        "global_plus_1_cost": {items[key]: qty for key, qty in sorted(first.items())},
        "global_plus_10_step_cost": {items[key]: qty for key, qty in sorted(last.items())},
        "auto_haste_catch_up_to_10": {items[key]: qty for key, qty in
                                     sorted(catch_up(86, 10, rows).items())},
        "random_example": {"before": [9, 10, 9, 10], "chosen_slot": chosen,
                           "after": list(ranks), "cost": {"CATALYST_Tidus": 1, "Ability Sphere": 1}},
        "maximum_four_slots": maxima[4],
        "maximum_five_slots": maxima[5],
        "note": "Costs and effect classes are proposals; no game runtime was used.",
    }
    if args.ability_bin:
        result["kernel"] = kernel_evidence(args.ability_bin)
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
