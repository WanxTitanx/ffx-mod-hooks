#!/usr/bin/env python3
"""Pin literal-only minigame rules to exact private EV01 code; never edits game assets."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event-root", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    sources = [("mcfr0100", 241, 1), ("mcfr0200", 242, 1), ("nagi0000", 223, 2), ("lmyt0000", 290, 2)]
    patches = []
    def add(script, pc, before, after, purpose):
        patches.append((script, pc, before, after, purpose))
    for script, timed30, timed40, hud40, hud30 in ((0, 0x3A5F, 0x3A7E, 0x7876, 0x78E9), (1, 0xA3D, 0xA5C, 0x2F13, 0x2F86)):
        add(script, timed30, 900, 1800, "Butterfly timeout: 30 to 60 seconds")
        add(script, timed40, 1200, 2400, "Butterfly timeout: 40 to 80 seconds")
        add(script, hud40+6, 39, 79, "Butterfly countdown accumulator: 79.9")
        add(script, hud40+42, 40, 80, "Butterfly initial display: 80 seconds")
        add(script, hud30+6, 29, 59, "Butterfly countdown accumulator: 59.9")
        add(script, hud30+42, 30, 60, "Butterfly initial display: 60 seconds")
    for pc, before, after, label in (
        (0x1EF35,12,19,"Wobbly target seconds"),(0x1EF3B,8,2,"Wobbly target tenths"),
        (0x1F208,16,25,"Dodger target seconds"),(0x1F20E,8,2,"Dodger target tenths"),
        (0x1F4CE,18,27,"Hyper Dodger target seconds"),(0x1F4D4,5,8,"Hyper Dodger target tenths")):
        add(2,pc,before,after,label)
    for balloon,bird in ((0x20C36,0x20C3E),(0x21D0D,0x21D15),(0x22DE4,0x22DEC),(0x23EBB,0x23EC3)):
        add(2,balloon,3,5,"Player balloon bonus: five seconds")
        add(2,bird,3,1,"Player bird penalty: one second")
    for pc,speed in ((0xEF1,90),(0x1435,40),(0x143E,60),(0x16E2,50),(0x16EB,60),
                     (0x1887,40),(0x1890,70),(0x1AB0,70),(0x1AB9,70),(0x1C13,50),
                     (0x1C1C,90),(0x1DB8,65),(0x1DC1,90)):
        add(3,pc,speed,speed*3//4,"Remiem rival movement speed: 75 percent")
    patches.sort()
    if len(patches)!=39 or len({(p[0],p[1]) for p in patches})!=len(patches):
        raise ValueError("Duplicate or incomplete rule catalog")
    lines=["// Generated from private original EV01 scripts by build_catalog.py.","#pragma once",
           "namespace FfxHooks::MinigameAssist::Data {","inline constexpr std::array<Script,kScriptCount> scripts{{"]
    reports=[]
    for index,(field,room,option) in enumerate(sources):
        path=next(args.event_root.glob("*/*/"+field+".ebp"));data=path.read_bytes()
        if data[:4]!=b"EV01" or struct.unpack_from("<I",data,4)[0]!=0x40:raise ValueError("Wrong EV01 container")
        blob=data[0x40:];code_bytes=struct.unpack_from("<I",blob,0)[0];code_offset=struct.unpack_from("<I",blob,0x30)[0]
        code=blob[code_offset:code_offset+code_bytes]
        if len(code)!=code_bytes or not 0<code_bytes<=256*1024:raise ValueError("Invalid code bounds")
        own=[p for p in patches if p[0]==index]
        boundaries=set();pc=0
        while pc<len(code):boundaries.add(pc);pc+=3 if code[pc]&0x80 else 1
        if pc!=len(code):raise ValueError("Incomplete ATEL instruction")
        for _,pc,before,after,_ in own:
            if pc not in boundaries or code[pc:pc+3]!=struct.pack("<BH",0xAE,before) or not 0<after<32768:
                raise ValueError(f"Mismatched signed immediate {field}:{pc:X}")
        digest=hashlib.sha256(code).digest()
        lines.append(' {"%s",%d,%d,0x%Xu,%d,{%s}},' % (field,room,option,code_offset,code_bytes,','.join('0x%02x'%b for b in digest)))
        reports.append({"field":field,"room":room,"source":str(path),"ebp_sha256":hashlib.sha256(data).hexdigest(),
                        "code_offset":code_offset,"code_bytes":code_bytes,"code_sha256":digest.hex(),
                        "rules":[{"pc":hex(pc),"from":a,"to":b,"purpose":label} for _,pc,a,b,label in own]})
    lines += ["}};","inline constexpr std::array<Rule,kRuleCount> rules{{"]
    lines += [' {%d,0x%Xu,%d,%d,%s},' % (i,pc,a,b,json.dumps(label)) for i,pc,a,b,label in patches]
    lines += ["}};","}",""]
    (ROOT/'src/runtime/FfxHooksDll/hooks/MinigameAssistCatalog.generated.h').write_text('\n'.join(lines))
    args.evidence.mkdir(parents=True,exist_ok=True)
    (args.evidence/'catalog-evidence.json').write_text(json.dumps(reports,indent=2)+'\n')
    print("PASS: 39 exact signed-immediate rules across four original field scripts")


if __name__=="__main__":main()
