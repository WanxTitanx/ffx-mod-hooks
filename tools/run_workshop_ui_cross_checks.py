#!/usr/bin/env python3
"""Compile isolated Win32 UI/settings harnesses with MinGW and run under Proton.

This is an alternate RT1 toolchain, not an MSVC DLL build or a live game test.
All outputs stay in the explicitly supplied evidence directory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def block(source: str, needle: str, last: bool = False) -> str:
    start = source.rfind(needle) if last else source.find(needle)
    if start < 0:
        raise ValueError('Missing production adapter: '+needle)
    body = source.index('{',start)
    depth,end = 1,body+1
    while depth and end < len(source):
        if source[end] == '{':depth += 1
        elif source[end] == '}':depth -= 1
        end += 1
    if depth:
        raise ValueError('Unbalanced adapter block')
    return source[start:end]

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--prefix',type=Path,required=True)
    parser.add_argument('--wine',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    prefix=args.prefix.resolve()
    if not prefix.is_dir() or not prefix.is_relative_to(ROOT/'.superpowers'):
        raise ValueError('Only a pre-existing project-isolated Proton prefix is permitted')
    here=ROOT/'src/runtime/FfxHooksDll';source=(here/'dllmain.cpp').read_text()
    # Windows header casing is insensitive; MinGW on Linux is not.
    (out/'Xinput.h').write_text('#include <xinput.h>\n')
    (out/'WorkshopCloseTransition.inc').write_text(block(source,'static void F7CloseTransition(',True))
    (out/'WorkshopListMouse.inc').write_text(block(source,'static F7MouseInputResult F7ListMouseTick(',True))
    pump=source[source.index('static int __cdecl NativeMenu_PumpHook('):]
    needles=('if(InterlockedCompareExchange(&EquipmentMenu::wantOpen,0,0)',
             'const LONG f7CloseSource = InterlockedExchange(&g_f7CloseSourcePending, -1);')
    parts=sorted((pump.index(needle),block(pump,needle)) for needle in needles)
    (out/'WorkshopOpenCloseOrder.inc').write_text('\n'.join(text for _,text in parts))
    runtime=(here/'hooks/EquipmentWorkshopRuntime.cpp').read_text()
    (out/'WorkshopTransaction.inc').write_text('\n'.join(block(runtime,needle) for needle in (
        'workshop::Error ReadEconomy(', 'workshop::Error Preview(', 'bool Commit(')))
    env={k:v for k,v in os.environ.items() if not k.startswith('FFXHOOKS_')}
    env.update(WINEPREFIX=str(prefix),WINEDEBUG='-all',FFXHOOKS_VALIDATE_ONLY='1')
    results=[]
    for name,additional in [('EquipmentWorkshopSettingsRt0',[]),('EquipmentWorkshopMenuRt1',[here/'hooks/F7UiCore.cpp']),('EquipmentWorkshopTransactionRt1',[])]:
        executable=out/(name+'.exe')
        sources=[here/'tests'/(name+'.cpp'),here/'shared/Config.cpp',ROOT/'research/equipment_workshop/src/workshop.cpp',*additional]
        command=['i686-w64-mingw32-g++','-std=c++17','-O2','-static','-DFFXHOOKS_TESTING',
                 '-I'+str(out),'-I'+str(ROOT/'research/equipment_workshop/include'),
                 *map(str,sources),'-luser32','-o',str(executable)]
        build=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,errors='replace')
        (out/(name+'.build.log')).write_text(build.stdout+build.stderr)
        if build.returncode:
            print(build.stdout+build.stderr);return build.returncode
        run=subprocess.run(['xvfb-run','-a',str(args.wine),str(executable)],cwd=out,env=env,
                           capture_output=True,text=True,errors='replace',timeout=90)
        text=run.stdout+run.stderr;(out/(name+'.proton.log')).write_text(text)
        ok=run.returncode==0 and 'FAIL' not in text
        results.append(dict(name=name,passed=ok,exit=run.returncode,
                            sha256=hashlib.sha256(executable.read_bytes()).hexdigest()))
        (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(text,flush=True)
        if not ok:return 1
    return 0

if __name__=='__main__':raise SystemExit(main())
