#!/usr/bin/env python3
"""Compile actual caller expressions against the real shared-load API header."""
from pathlib import Path
import argparse
import json
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'

def run(output):
    output.mkdir(parents=True,exist_ok=False)
    source=(MOD/'dllmain.cpp').read_text(encoding='utf-8-sig')
    calls=re.findall(r'FfxHooks::(?:Remove|Stop|Request)NativeSaveLoadEvents(?:Stop)?\s*\(\s*\)\s*;',source)
    if len(calls)!=2:raise ValueError('expected one normal retirement and one nonblocking stop call')
    generated=output/'load_api.cpp'
    generated.write_text('#include "GridTeachHook.h"\nvoid ActualLoadApiCalls(){\n'+ '\n'.join(calls)+'\n}\n')
    command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsyntax-only','-I'+str(MOD/'hooks'),str(generated)]
    result=subprocess.run(command,capture_output=True,timeout=30)
    (output/'compiler.log').write_bytes(result.stdout+result.stderr)
    (output/'result.json').write_text(json.dumps({'calls':calls,'exit_code':result.returncode,
        'scope':'actual caller expressions and actual header; compile-only, not full DLL'},indent=2)+'\n')
    print('NativeSaveLoadApi compile exit',result.returncode,flush=True)
    print((result.stdout+result.stderr).decode(errors='replace'),end='')
    return result.returncode

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path)
    raise SystemExit(run(p.parse_args().output.resolve()))
