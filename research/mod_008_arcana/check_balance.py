#!/usr/bin/env python3
"""Validate the user's deck constraints without game or external-mod data."""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    source = (HERE.parents[1] / "src/runtime/FfxHooksDll/hooks/ArcanaCore.h").read_text()
    capacity = int(re.search(r"kMaximumCardEffects=(\d+)", source).group(1))
    rows = json.loads((HERE / "effects.v1.json").read_text())["cards"]
    assert [r["id"] for r in rows] == list(range(78))
    stats = {"HpPercent", "MpPercent", "StrengthPercent", "MagicPercent", "DefensePercent",
             "MagicDefensePercent", "AccuracyFlat", "EvasionFlat", "LuckFlat"}
    signatures = set()
    for row in rows:
        effects = {e["kind"]: e["value"] for e in row["effects"]}
        assert effects and len(effects) <= capacity
        assert set(effects) == {"HpPercent"} or set(effects) - stats, row["id"]
        signature = tuple(sorted(effects.items()))
        assert signature not in signatures, (row["id"], "duplicate build")
        signatures.add(signature)
        for attack, wards in {"StrikeFire": ["WardFire"], "StrikeIce": ["WardIce"],
                              "StrikeLightning": ["WardLightning"], "StrikeWater": ["WardWater"],
                              "StrikeHoly": ["WardHoly"], "StrikeShadow": ["WardShadow"],
                              "StrikeEarth": ["WardEarth"], "StrikeWind": ["WardWind"],
                              "StrikeBio": ["WardBio"], "StrikeGravity": ["WardGravity"],
                              "ElementDamageFireIce": ["WardFire", "WardIce"],
                              "ElementDamageLightningWater": ["WardLightning", "WardWater"],
                              "ElementDamageHoly": ["WardHoly"]}.items():
            if attack in effects:
                assert all(effects.get(ward) for ward in wards), (row["id"], attack)
    by_id = [{e["kind"]: e["value"] for e in r["effects"]} for r in rows]
    assert by_id[0]["FirstStrike"] and by_id[0]["FirstCtbReduction"] >= 35
    assert by_id[13]["TouchDeath"] == 100 and by_id[13]["KillHp"] >= 20
    assert by_id[13]["DeathImmuneDamage"] >= 20 and by_id[13]["ProofDeath"]
    assert by_id[18]["AutoReflect"] and by_id[18]["EvadeCounter"] and by_id[18]["TouchConfuse"]
    assert by_id[21]["OverdriveDamage"] >= 50 and by_id[21]["CtbReduction"] >= 15
    assert "FreeMp" not in by_id[21]
    assert by_id[71]["ApBonus"] >= 75 and by_id[71]["MpReduction"] >= 25
    for effects in by_id:
        assert effects.get("DefendMp",0) <= 3 and effects.get("MpPerTurn",0) <= 1
    assert all(by_id[i]["KillMp"]==10 for i in (13,58,69))
    print("PASS 78 distinct builds: no pure-stat filler except HP; matching elemental defense; highlighted cards upgraded")


if __name__ == "__main__":
    main()
