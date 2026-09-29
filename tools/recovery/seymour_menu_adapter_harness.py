#!/usr/bin/env python3
"""Actual menu adapter with explicit simulated native and platform endpoints."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
from seymour_sort_adapter_harness import portable_finally
ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
CASES=('off invalid validate stop-before master-off profile signature pin publisher create publish stop-publish '
       'call mode-hidden foreign wrong-thread battle no-session off-after revoke-native revive-native load-native '
       'original-exception read-failure write-failure short-write post-write-revoke post-write-save reentry remove menu retire-fail '
       'disable-cleanup wrong-thread-cleanup changed-menu-cleanup '
       'new-save-cleanup new-save-pending-cleanup new-save-wrong-thread-cleanup').split()
def emit(output,portable=False):
    adapter=MOD/'hooks/SeymourMenuListHook.cpp'
    if not adapter.exists():raise AssertionError('native eight-character menu adapter is missing')
    if output.exists():raise FileExistsError(output)
    template=MOD/'tests/SeymourMenuListAdapterRt1.inl';source=adapter.read_text()
    for name in ('SeymourMenuListHook.h','RecoveryNative.h','SeymourSessionRuntime.h','F8FlagCatalog.h','../shared/Config.h'):
        token='#include "'+name+'"'
        if source.count(token)!=1:raise ValueError('endpoint changed: '+name)
        source=source.replace(token,'// Explicit RT1 endpoint declared above.')
    if portable:source=portable_finally(source)
    body=template.read_text();marker='// INSERT ACTUAL MENU ADAPTER'
    if body.count(marker)!=1:raise ValueError('template marker changed')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(body.replace(marker,source))
    files=[adapter,template,Path(__file__)]+[MOD/'hooks'/name for name in ('SeymourMenuListCore.h','SeymourMenuListService.h','SeymourOverdriveControl.h','SeymourSessionRuntime.h','MinHookBatchCoordinator.h')]
    output.with_suffix('.manifest.json').write_text(json.dumps({'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'portable_finally_translation':portable,'native_game_proven':False},indent=2)+'\n')
def run(output):
    output.mkdir(parents=True,exist_ok=False);source=output/'menu.cpp';emit(source,True)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(MOD/'hooks'),'-I'+str(MOD/'shared'),str(source),'-o',str(output/'menu')]
    built=subprocess.run(cmd,capture_output=True,timeout=60);(output/'build.log').write_bytes(built.stdout+built.stderr)
    if built.returncode:print(built.stderr.decode(errors='replace'));return built.returncode
    results=[];boundaries=0
    def execute(case):
        r=subprocess.run([str(output/'menu'),case],capture_output=True,timeout=15);(output/(case+'.log')).write_bytes(r.stdout+r.stderr)
        text=r.stdout.decode(errors='replace');m=re.search(r': (\d+)/(\d+) passed',text);passed,total=map(int,m.groups()) if m else (0,0)
        results.append({'case':case,'exit_code':r.returncode,'passed':passed,'total':total,'ok':r.returncode==0 and total>0 and passed==total})
        if r.returncode:print(text)
        return text
    for case in CASES:
        text=execute(case)
        if case=='call':
            m=re.search(r'MENU_READS=(\d+)',text);boundaries=int(m.group(1)) if m else 0
    if not 0<boundaries<=4096:raise AssertionError('missing bounded memory-access inventory')
    for mode in ('revoke','revive'):
        for i in range(1,boundaries+1):execute(f'{mode}-read-{i}')
    report={'cases_passed':sum(r['ok'] for r in results),'cases_total':len(results),'read_boundaries':boundaries,'results':results,'live_game_proven':False}
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n');print('MENU_ADAPTER',report['cases_passed'],report['cases_total'])
    return int(not all(r['ok'] for r in results))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);p.add_argument('--run',action='store_true');p.add_argument('--portable',action='store_true');a=p.parse_args()
    if a.run:raise SystemExit(run(a.output.resolve()))
    emit(a.output.resolve(),a.portable)
