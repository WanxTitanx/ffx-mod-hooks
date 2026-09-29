#!/usr/bin/env python3
"""Emit actual-source Windows RT1; platform endpoints remain explicitly simulated."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
def emit(output: Path):
    if output.exists():raise FileExistsError(output)
    adapter=(MOD/'hooks/SeymourCompatibilityHook.cpp').read_text()
    for line in ('#include "RecoveryNative.h"','#include "F7InLive.h"','#include "../shared/Config.h"'):
        if adapter.count(line)!=1:raise ValueError('native endpoint include changed: '+line)
        adapter=adapter.replace(line,'// RT1 simulated platform declaration is supplied above.')
    owner=(MOD/'hooks/SeymourBattleHook.cpp').read_text()
    start=owner.index('bool CaptureSeymourCompatibilityScope(')
    end=owner.index('SeymourBattleRuntimeSnapshot GetSeymourBattleRuntimeSnapshot()',start)
    scope=owner[start:end].strip()
    template=(MOD/'tests/SeymourCompatibilityAdapterRt1.inl').read_text()
    for marker in ('// INSERT REAL SCOPE','// INSERT REAL ADAPTER'):
        if template.count(marker)!=1:raise ValueError('template marker changed')
    generated=template.replace('// INSERT REAL SCOPE',scope).replace('// INSERT REAL ADAPTER',adapter)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x') as f:f.write(generated)
    report={'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (
        MOD/'hooks/SeymourCompatibilityHook.cpp',MOD/'hooks/SeymourBattleHook.cpp',
        MOD/'hooks/SeymourCompatibilityCore.h',MOD/'hooks/SeymourCompatibilityService.h',
        MOD/'tests/SeymourCompatibilityAdapterRt1.inl')},
        'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
        'scope':'actual adapter and scope function, simulated profile/patch/config/native endpoints',
        'native_install_or_game_proven':False}
    with output.with_suffix('.manifest.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    emit(p.parse_args().output)
