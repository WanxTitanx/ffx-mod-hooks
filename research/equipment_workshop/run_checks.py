#!/usr/bin/env python3
"""Build/check the Workshop without touching a game installation."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

ROOT=Path(__file__).resolve().parent
BUILD=ROOT/'build'

def run(command, **kwargs):
    p=subprocess.run(command,cwd=ROOT,text=True,errors="replace",stdout=subprocess.PIPE,stderr=subprocess.STDOUT,**kwargs)
    if p.returncode:raise RuntimeError(f'{command[0]} failed ({p.returncode})\n{p.stdout}')
    return p.stdout

def local(args):
    BUILD.mkdir(exist_ok=True)
    common=['-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','src/workshop.cpp','src/effects.cpp','src/lifecycle.cpp']
    run(['c++',*common,'tests/test_workshop.cpp','-o',str(BUILD/'core_tests')])
    print(run([str(BUILD/'core_tests')]),end='')
    run(['c++',*common,'-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','tests/test_workshop.cpp','-o',str(BUILD/'core_sanitized')])
    print(run([str(BUILD/'core_sanitized')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')),end='')
    run(['c++',*common,'-shared','-fPIC','-o',str(BUILD/'libworkshop.so')])
    environment=dict(os.environ)
    if args.save:environment['WORKSHOP_SAVE_FIXTURE']=str(args.save.resolve())
    print(run(['python3','-m','unittest','discover','-s','tests','-p','test_*.py','-v'],env=environment),end='')
    run(['i686-w64-mingw32-g++',*common,'-static','-O2','tests/native_effects.cpp','-ladvapi32','-o',str(BUILD/'native_effects.exe')])

def ps(host,code):
    encoded=base64.b64encode(code.encode('utf-16le')).decode()
    return run(['ssh','-o','BatchMode=yes','-o','LogLevel=ERROR',host,'pwsh','-NoProfile','-OutputFormat','Text','-EncodedCommand',encoded],timeout=55)

def windows(args):
    if not args.pe or not args.kernel:raise ValueError('--pe and --kernel required')
    folder='C:/VMTasks/workshop-'+uuid.uuid4().hex
    host=args.windows
    quote=lambda text:"'"+str(text).replace("'","''")+"'"
    ps(host,'New-Item -ItemType Directory -Path '+quote(folder)+' | Out-Null')
    inputs={'workshop.h':ROOT/'include/workshop.h','effects.h':ROOT/'include/effects.h','lifecycle.h':ROOT/'include/lifecycle.h',
            'workshop.cpp':ROOT/'src/workshop.cpp','effects.cpp':ROOT/'src/effects.cpp','lifecycle.cpp':ROOT/'src/lifecycle.cpp',
            'test_workshop.cpp':ROOT/'tests/test_workshop.cpp','native_effects.cpp':ROOT/'tests/native_effects.cpp',
            'image.bin':args.pe,'a_ability.bin':args.kernel}
    if args.snapshot:inputs['snapshot.bin']=args.snapshot
    hashes={}
    try:
        for name,path in inputs.items():
            hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
            run(['scp','-q',str(path),host+':'+folder+'/'+name])
            remote=ps(host,'(Get-FileHash -Algorithm SHA256 -LiteralPath '+quote(folder+'/'+name)+').Hash').strip().lower()
            if remote!=hashes[name]:raise RuntimeError('Transfer changed '+name)
        batch='''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 2
cd /d %~dp0
cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /I. workshop.cpp effects.cpp lifecycle.cpp test_workshop.cpp /Fe:core.exe
if errorlevel 1 exit /b 3
core.exe
if errorlevel 1 exit /b 4
cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /I. workshop.cpp effects.cpp lifecycle.cpp native_effects.cpp advapi32.lib /Fe:native.exe
if errorlevel 1 exit /b 5
native.exe image.bin a_ability.bin
exit /b %errorlevel%
'''
        if args.snapshot:batch=batch.replace('native.exe image.bin a_ability.bin','native.exe image.bin a_ability.bin snapshot.bin')
        (BUILD/'build.cmd').write_bytes(batch.replace('\n','\r\n').encode('ascii'))
        run(['scp','-q',str(BUILD/'build.cmd'),host+':'+folder+'/build.cmd'])
        output=ps(host,'& '+quote(folder+'/build.cmd')+'; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }')
        (BUILD/'windows.log').write_text(output)
        print(output)
        run(['scp','-q',host+':'+folder+'/native.exe',str(BUILD/'native_msvc.exe')])
        (BUILD/'windows-inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
    finally:
        ps(host,'Remove-Item -LiteralPath '+quote(folder)+' -Recurse -Force; if(Test-Path '+quote(folder)+'){exit 7}; "WORKSHOP_TEMP_REMOVED"')

def proton(args):
    if not args.pe or not args.kernel or not args.wine:raise ValueError('--pe, --kernel and --wine required')
    env={k:v for k,v in os.environ.items() if not k.startswith('FFXHOOKS_')}
    env.update(WINEPREFIX=str(args.proton_prefix.resolve()),WINEDEBUG='-all',FFXHOOKS_VALIDATE_ONLY='1')
    exe=BUILD/('native_msvc.exe' if (BUILD/'native_msvc.exe').exists() else 'native_effects.exe')
    extra=[str(args.snapshot)] if args.snapshot else []
    output=run(['xvfb-run','-a',str(args.wine),str(exe),str(args.pe),str(args.kernel),*extra],env=env,timeout=55)
    (BUILD/'proton.log').write_text(output);print(output,end='')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--save',type=Path);parser.add_argument('--pe',type=Path);parser.add_argument('--kernel',type=Path)
    parser.add_argument('--windows',help='Explicit SSH alias of the isolated Windows host')
    parser.add_argument('--snapshot',type=Path,help='Optional validated v1 host state with an Auto-Protect fifth ability')
    parser.add_argument('--proton-prefix',type=Path);parser.add_argument('--wine',type=Path)
    parser.add_argument('--skip-local',action='store_true')
    args=parser.parse_args()
    if not args.skip_local:local(args)
    if args.windows:windows(args)
    if args.proton_prefix:proton(args)
