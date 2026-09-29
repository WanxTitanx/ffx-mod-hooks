#!/usr/bin/env python3
"""Actual sorting adapter; native/OS endpoints are explicit simulated dependencies.

Portable runs translate only try/finally syntax to C++ scope cleanup. Windows
emission retains SEH. Neither mode proves the real game's helper implementations.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
CASES=('off invalid validate stop-before stop-during profile signature helper-signature publish '
       'owner type within counters corrupt-counter corrupt-within outside-within row-drift '
       'session-drift stop-inside off-on field wrong-thread workshop-busy duplicates '
       'invalid-slots ungrouped exception swap-exception reentrant menu retire-fail').split()


def closing(text,at):
    depth=1;i=at+1
    while i<len(text):
        if text.startswith('//',i):
            end=text.find('\n',i);i=len(text) if end<0 else end+1;continue
        if text.startswith('/*',i):
            end=text.find('*/',i+2)
            if end<0:raise ValueError('unterminated comment')
            i=end+2;continue
        if text[i] in ('"',"'"):
            quote=text[i];i+=1
            while i<len(text) and text[i]!=quote:i+=2 if text[i]=='\\' else 1
        elif text[i]=='{':depth+=1
        elif text[i]=='}':
            depth-=1
            if not depth:return i
        i+=1
    raise ValueError('unclosed brace')


def portable_finally(source):
    while '__try' in source:
        start=source.rfind('__try');opening=source.index('{',start);end=closing(source,opening)
        tail=end+1
        while source[tail].isspace():tail+=1
        if not source.startswith('__finally',tail):raise ValueError('unsupported SEH construct')
        cleanup=source.index('{',tail);last=closing(source,cleanup)
        source=source[:start]+'{ auto cleanup = Test::OnExit([&]() noexcept {'+source[cleanup+1:last]+'});'+source[opening+1:end]+'}'+source[last+1:]
    return source


def emit(output,portable=False):
    if output.exists():raise FileExistsError(output)
    adapter=MOD/'hooks/SeymourGearSortHook.cpp';template=MOD/'tests/SeymourSortAdapterRt1.inl'
    source=adapter.read_text()
    for name in ('SeymourGearSortHook.h','RecoveryNative.h','RonsoPoolRuntime.h','SeymourSessionRuntime.h',
                 'EquipmentWorkshopRuntime.h','F8FlagCatalog.h','../shared/Config.h'):
        token='#include "'+name+'"'
        if source.count(token)!=1:raise ValueError('endpoint include changed: '+name)
        source=source.replace(token,'// Explicit test endpoint declarations above.')
    substitutions={}
    for old,new in {
        'reinterpret_cast<int(__cdecl*)()>(g_base+CountRva)':'Test::NativeCount',
        'reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(g_base+SwapRva)':'Test::NativeSwap',
        'reinterpret_cast<void(__cdecl*)(void*,int,unsigned)>(g_base+WithinRva)':'Test::NativeWithin',
        'reinterpret_cast<void(__cdecl*)()>(g_base+RefreshRva)':'Test::NativeRefresh',
    }.items():
        count=source.count(old)
        if new!='Test::NativeRefresh' and count!=1:raise ValueError('endpoint changed: '+new)
        substitutions[new]=count;source=source.replace(old,new)
    if portable:source=portable_finally(source)
    body=template.read_text();marker='// INSERT ACTUAL SORT ADAPTER'
    if body.count(marker)!=1:raise ValueError('template marker changed')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(body.replace(marker,source))
    files=[adapter,template,Path(__file__),MOD/'hooks/SeymourGearSortCore.h',MOD/'hooks/SeymourOverdriveControl.h']
    within=MOD/'hooks/SeymourGearWithinCore.h'
    if within.exists():files.append(within)
    output.with_suffix('.manifest.json').write_text(json.dumps({
        'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        'portable_finally_translation':portable,'native_endpoint_substitutions':substitutions,
        'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'live_game_proven':False},indent=2)+'\n')


def run(output):
    output.mkdir(parents=True,exist_ok=False);source=output/'sort.cpp';emit(source,True)
    built=subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-I'+str(MOD/'hooks'),str(source),'-o',str(output/'sort')],capture_output=True,timeout=60)
    (output/'build.log').write_bytes(built.stdout+built.stderr)
    if built.returncode:print(built.stderr.decode(errors='replace'));return built.returncode
    results=[]
    def execute(case,verbose=True):
        result=subprocess.run([str(output/'sort'),case],capture_output=True,timeout=10)
        (output/(case+'.log')).write_bytes(result.stdout+result.stderr)
        text=result.stdout.decode(errors='replace')
        checks=re.search(r': (\d+)/(\d+) passed',text)
        passed,total=map(int,checks.groups()) if checks else (0,0)
        record={'case':case,'exit_code':result.returncode,'passed':passed,'total':total,
                'ok':result.returncode==0 and total>0 and passed==total}
        results.append(record)
        if verbose:print(text.strip(),flush=True)
        return text
    within_text=''
    for case in CASES:
        text=execute(case)
        if case=='within':within_text=text
    match=re.search(r'BATTLE_READS=(\d+)',within_text)
    boundaries=int(match.group(1)) if match else 0
    if not 0<boundaries<=4096:raise ValueError('missing or unbounded memory-boundary inventory')
    # A fresh process resets all process-lifetime state for every injected edge.
    # The healthy run inventories boundaries; base-case failures are never hidden.
    for mode in ('revoke','revive'):
        for point in range(1,boundaries+1):execute(f'{mode}-read-{point}',False)
    summary={'base_cases':len(CASES),'read_boundaries':boundaries,
             'revocation_cases':boundaries*2,'cases_passed':sum(r['ok'] for r in results),
             'cases_total':len(results),'results':results,'live_game_proven':False}
    (output/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('SORT_ADAPTER_CASES',summary['cases_passed'],'/',summary['cases_total'],flush=True)
    return int(not all(r['ok'] for r in results))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--run',action='store_true');p.add_argument('--portable',action='store_true');a=p.parse_args()
    if a.run:raise SystemExit(run(a.output.resolve()))
    emit(a.output.resolve(),a.portable)
