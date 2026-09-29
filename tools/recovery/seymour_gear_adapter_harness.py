#!/usr/bin/env python3
"""Emit actual production name adapter with explicit test endpoints."""
from pathlib import Path
import argparse
import hashlib
import json
ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
def emit(output):
    if output.exists():raise FileExistsError(output)
    adapter=MOD/'hooks/SeymourGearPresentationHook.cpp'
    template=MOD/'tests/SeymourGearAdapterRt1.inl'
    source=adapter.read_text()
    for name in ('RecoveryNative.h','F7InLive.h','F8FlagCatalog.h','../shared/Config.h'):
        token='#include "'+name+'"'
        if source.count(token)!=1:raise ValueError('endpoint mismatch: '+name)
        source=source.replace(token,'// Explicit test endpoints declared above.')
    marker='// INSERT ACTUAL GEAR ADAPTER';body=template.read_text()
    if body.count(marker)!=1:raise ValueError('template marker mismatch')
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x') as f:f.write(body.replace(marker,source))
    sources=[adapter,template]+[MOD/'hooks'/p for p in ('SeymourGearPresentationCore.h','SeymourGearPresentationService.h','SeymourGearPresentationEvidence.h','SeymourGearPresentationHook.h','SeymourOverdriveControl.h')]
    report={'source_hashes':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
            'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
            'scope':'actual adapter; simulated installation/config; real Windows exceptions',
            'live_game_proven':False}
    with output.with_suffix('.manifest.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path)
    print(json.dumps(emit(p.parse_args().output),indent=2))
