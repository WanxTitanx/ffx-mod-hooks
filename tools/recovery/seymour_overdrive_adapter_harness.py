#!/usr/bin/env python3
"""Emit exact production Overdrive adapter with explicit simulated endpoints."""
from pathlib import Path
import argparse
import hashlib
import json
ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
def emit(output):
    if output.exists():raise FileExistsError(output)
    adapter=MOD/'hooks/SeymourOverdriveHook.cpp'
    template=MOD/'tests/SeymourOverdriveAdapterRt1.inl'
    source=adapter.read_text()
    for name in ('RecoveryNative.h','F7InLive.h','../shared/Config.h'):
        line='#include "'+name+'"'
        if source.count(line)!=1:raise ValueError('endpoint include mismatch')
        source=source.replace(line,'// Explicit simulated endpoint declarations above.')
    body=template.read_text();marker='// INSERT ACTUAL OVERDRIVE ADAPTER'
    if body.count(marker)!=1:raise ValueError('template marker mismatch')
    generated=body.replace(marker,source)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x') as f:f.write(generated)
    sources=[adapter,template]+[MOD/'hooks'/p for p in ('SeymourOverdrivePlan.h','SeymourOverdriveControl.h','SeymourOverdriveEvidence.generated.h','SeymourCompatibilityCore.h')]
    report={'source_hashes':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
            'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
            'scope':'exact adapter bodies, simulated native/platform endpoints; generated native clones are NOT executed',
            'runtime_game_proven':False}
    with output.with_suffix('.manifest.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    print(json.dumps(emit(p.parse_args().output),indent=2))
