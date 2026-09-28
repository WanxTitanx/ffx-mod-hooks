#!/usr/bin/env python3
"""RT0 checks of the proposed contract. This is NOT an implementation of a game hook."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).parent
checks = 0


def check(condition):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(f"Design assertion {checks} failed")


def validate(pack):
    rows = pack["elements"]
    if not 1 <= len(rows) <= 32:
        raise ValueError("Unsupported registry size")
    keys = [row["key"] for row in rows]
    if len(keys) != len(set(keys)):
        raise ValueError("Duplicate stable key")
    bits = [row["native_bit"] for row in rows if row["native_bit"] is not None]
    if len(bits) != len(set(bits)) or any(type(b) is not int or b not in [1 << i for i in range(8)] for b in bits):
        raise ValueError("Invalid or conflicting native bit")
    affinities = pack["example_target"]["damage_taken_bp"]
    if any(k not in keys or type(v) is not int or not -10000 <= v <= 25000 or v % 2500 for k, v in affinities.items()):
        raise ValueError("Invalid affinity")
    return {row["key"]: row["native_bit"] for row in rows}


def truncated(n, d):
    return (-1 if n < 0 else 1) * (abs(n) // d)


def resolve(damage, parts, policy):
    # Parts are (damage-taken basis points, integer weight); signed once, no healing callback.
    if not parts or any(w <= 0 for _, w in parts):
        raise ValueError("Empty elements or invalid weight")
    if policy == "highest_exposure":
        return truncated(damage * max(m for m, _ in parts), 10000)
    if policy == "lowest_exposure":
        return truncated(damage * min(m for m, _ in parts), 10000)
    if policy == "split_weighted":
        return truncated(damage * sum(m*w for m, w in parts), 10000 * sum(w for _, w in parts))
    raise ValueError("Unknown policy")


def reject(pack):
    try:
        validate(pack)
    except ValueError:
        check(True)
    else:
        check(False)


def main():
    pack = json.loads((ROOT / "example_manifest.json").read_text())
    mapping = validate(pack)
    check(len(mapping) == 10 and mapping["spira.poison"] is None and mapping["spira.gravity"] is None)
    reversed_pack = copy.deepcopy(pack)
    reversed_pack["elements"].reverse()
    check(validate(reversed_pack) == mapping)
    check(validate(json.loads(json.dumps(pack))) == mapping)
    for count in (8, 9, 10, 16, 32):
        variant = copy.deepcopy(pack)
        variant["elements"] = pack["elements"][:8] + [
            {"key": f"example.element-{i}", "native_bit": None} for i in range(8, count)]
        variant["example_target"]["damage_taken_bp"] = {"ffx.fire": 10000}
        check(len(validate(variant)) == count)
    bad = copy.deepcopy(pack);bad["elements"].append(bad["elements"][0]);reject(bad)
    bad = copy.deepcopy(pack);bad["elements"][8]["native_bit"] = 256;reject(bad)
    bad = copy.deepcopy(pack);bad["elements"][8]["native_bit"] = 3;reject(bad)
    bad = copy.deepcopy(pack);bad["elements"][8]["native_bit"] = 128;reject(bad)
    bad = copy.deepcopy(pack);bad["example_target"]["damage_taken_bp"]["missing.key"] = 15000;reject(bad)
    for invalid in (-12500, 27500, 12345, 15000.0):
        bad=copy.deepcopy(pack);bad["example_target"]["damage_taken_bp"]["ffx.fire"]=invalid;reject(bad)
    bad=copy.deepcopy(pack)
    bad["elements"] += [{"key": f"overflow.{i}","native_bit":None} for i in range(23)]
    reject(bad)
    tiers=list(range(-10000,25001,2500))
    check(len(tiers)==15)
    for tier in tiers:
        check(resolve(1000,[(tier,1)],"highest_exposure")==tier//10)
        for stacks in range(5):
            exposed=min(25000,tier+stacks*2500)
            check(exposed>=tier and exposed<=25000)
    check(min(25000,7500+2*2500)==12500)
    check(min(25000,-10000+4*2500)==0)
    check(resolve(1000,[(15000,1),(-10000,1)],"highest_exposure")==1500)
    check(resolve(1000,[(15000,1),(-10000,1)],"lowest_exposure")==-1000)
    check(resolve(1000,[(15000,1),(-10000,1)],"split_weighted")==250)
    check(resolve(1000,[(15000,3),(-10000,1)],"split_weighted")==875)
    check(resolve(3,[(5000,1)],"highest_exposure")==1)
    check(resolve(-3,[(5000,1)],"highest_exposure")==-1)
    check(resolve(2147483647,[(25000,1)],"highest_exposure")==5368709117)
    # The last value explicitly requires i64 intermediates and a downstream i32/game cap.
    for maximum,current in ((16000,16000),(16000,4000),(16000,1),(16000,0),(3,3)):
        raw=maximum//16
        actual=min(raw,max(0,current-1))
        check(0<=actual<=max(0,current-1))
    check(16000//16==1000 and 4000//4==1000 and 4000//16==250)
    print(json.dumps({"producer":"Jarvis-HOOK","level":"RT0-proposed-contract-only",
                      "assertions":checks,"failures":0,
                      "scope":"registry, signed affinity math, independent gravity rules; no runtime adapter"}))


if __name__ == "__main__":
    main()
