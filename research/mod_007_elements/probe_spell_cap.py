#!/usr/bin/env python3
"""RT0: pin cap bytes/command classification and exercise the proposed magical cap policy."""
import argparse
import hashlib
import json
from pathlib import Path
import pefile

EXE_SHA = "78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced"
COMMAND_SHA = "db4c87f33f27a7df41bc8a520bc2246b664167319386fe99da9880ae06000429"


def native_cap(equipment_bdl, flags):
    if flags & 0x80:
        return 99999
    if flags & 0x40:
        return 9999
    return 99999 if equipment_bdl else 9999


def proposed_clamp(value, equipment_bdl, flags, is_spell, component, enabled):
    original = native_cap(equipment_bdl, flags)
    upper = 999999 if enabled and is_spell and component == "hp" and original == 99999 else original
    # The user requested more damage; the existing healing floor remains independent.
    return max(-original, min(value, upper))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("exe",type=Path);p.add_argument("command",type=Path)
    p.add_argument("--output",type=Path,required=True);args=p.parse_args()
    if args.output.resolve() in (args.exe.resolve(),args.command.resolve()):
        p.error("Output must not replace an input")
    raw=args.exe.read_bytes();commands=args.command.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=EXE_SHA or hashlib.sha256(commands).hexdigest()!=COMMAND_SHA:
        raise SystemExit("Unsupported fixture identity")
    pe=pefile.PE(data=raw)
    graph=bytes.fromhex("3bc77d048bc7eb063bc37e028bc38906")
    assert pe.get_data(0x38EDCB,len(graph))==graph
    assert pe.get_data(0x38ED41,5)==bytes.fromhex("bb9f860100")
    observed=[]
    for index,name in [(0,"Attack"),(63,"Holy"),(66,"Fire"),(78,"Demi"),(83,"Ultima"),
                       (115,"Nova"),(121,"Fire Fury"),(133,"Demi Fury"),(138,"Ultima Fury")]:
        row=commands[20+96*index:20+96*(index+1)]
        observed.append({"index":index,"name":name,"damage_flags":row[0x20],"channels":row[0x23],
                         "formula":row[0x28],"row_sha256":hashlib.sha256(row).hexdigest()})
    assert next(r for r in observed if r['index']==66)['damage_flags'] & 2
    assert next(r for r in observed if r['index']==121)['damage_flags'] & 2 == 0
    count=0
    def check(condition):
        nonlocal count
        count+=1
        if not condition:raise AssertionError(f"Cap policy case {count} failed")
    for equipment in (False,True):
        for flags in (0,2,0x40,0x42,0x80,0x82,0xC0,0xC2):
            original=native_cap(equipment,flags)
            for spell in (False,True):
                for component in ("hp","mp","ctb"):
                    for value in (-2000000,-100000,-9999,0,9999,10000,99999,100000,999999,1000000,2147483647):
                        off=proposed_clamp(value,equipment,flags,spell,component,False)
                        check(off==max(-original,min(value,original)))
                        on=proposed_clamp(value,equipment,flags,spell,component,True)
                        allowed=bool(spell and component=="hp" and original==99999 and value>original)
                        check((on!=off)==allowed)
                        check(-original<=on<=(999999 if allowed else original))
    check(proposed_clamp(900000,False,2,True,"hp",True)==9999)
    check(proposed_clamp(900000,True,2,True,"hp",True)==900000)
    check(proposed_clamp(2000000,True,2,True,"hp",True)==999999)
    check(proposed_clamp(900000,True,1,False,"hp",True)==99999)
    check(proposed_clamp(900000,True,0,True,"hp",True)==900000)  # Explicit Fury binding.
    check(proposed_clamp(900000,True,0x42,True,"hp",True)==9999)
    check(proposed_clamp(900000,False,0x82,True,"hp",True)==900000)
    report={"producer":"Jarvis-HOOK","date":"2026-09-27","level":"RT0-static-bytes-and-proposed-policy",
            "exe_sha256":EXE_SHA,"command_sha256":COMMAND_SHA,
            "cap_graph_rva":"0x38EDCB","upper_clamp_rva":"0x38EDD3","writeback_rva":"0x38EDD9",
            "cap_graph_sha256":hashlib.sha256(graph).hexdigest(),
            "command_observations":observed,"policy_assertions":count,"failures":0,
            "limitations":["Does not execute the complete native cap loop or a replacement Hook",
                           "Native selection/command flags inspected; spell classification remains a manifest contract",
                           "Damage display, live HP writeback, Nova/Nul composition and RT2 pending"]}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({"policy_assertions":count,"failures":0,"output":str(args.output)}))


if __name__=="__main__":main()
