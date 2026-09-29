#!/usr/bin/env python3
"""Build/run the real Win32 companion store in a fresh VMTasks directory only."""
from pathlib import Path
import argparse
import base64
import hashlib
import io
import json
import secrets
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'

def run(host,output):
    output.mkdir(parents=True,exist_ok=False)
    remote='C:/VMTasks/sphere-store-'+secrets.token_hex(5)
    def ps(script,name):
        encoded=base64.b64encode(script.encode('utf-16le')).decode()
        result=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',host,
            'powershell','-NoProfile','-NonInteractive','-EncodedCommand',encoded],capture_output=True,timeout=300)
        (output/name).write_bytes(result.stdout+result.stderr)
        return result
    preflight=("$ErrorActionPreference='Stop';$busy=@(Get-Process -ErrorAction Stop|"
               "Where-Object {$_.ProcessName -in @('cl','link','MSBuild')});if($busy){exit 75};")
    check=ps(preflight+'exit 0','preflight.log')
    if check.returncode:return check.returncode
    names=['tests/SphereGridProgress8StoreRt1.cpp','tests/SphereGridProgress8StoreFaultRt1.cpp',
           'hooks/SphereGridProgress8Core.h','hooks/SphereGridProgressCore.h']
    for name in ['hooks/SphereGridProgress8Store.h','hooks/SphereGridProgress8Store.cpp']:
        if (MOD/name).exists():names.append(name)
    payload={name:(MOD/name).read_bytes() for name in names}
    extra=' hooks/SphereGridProgress8Store.cpp' if 'hooks/SphereGridProgress8Store.cpp' in payload else ''
    lines=['@echo off',
        'call "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 >environment.log 2>&1',
        'if errorlevel 1 exit /b %errorlevel%',
        'cl /nologo /EHsc /std:c++17 /utf-8 /W4 /WX /MT /O2 tests/SphereGridProgress8StoreRt1.cpp'+extra+' /Fe:store-test.exe >build.log 2>&1',
        'if errorlevel 1 exit /b %errorlevel%',
        'cl /nologo /EHsc /std:c++17 /utf-8 /W4 /WX /MT /O2 tests/SphereGridProgress8StoreFaultRt1.cpp /Fe:store-faults.exe >fault-build.log 2>&1',
        'if errorlevel 1 exit /b %errorlevel%',
        'set RESULT=0',
        'store-test.exe "%CD%\\disk" >execution.log 2>&1','if errorlevel 1 set RESULT=1',
        'store-faults.exe "%CD%\\fault-disk" >fault-execution.log 2>&1','if errorlevel 1 set RESULT=1',
        'exit /b %RESULT%']
    payload['build.cmd']=('\r\n'.join(lines)+'\r\n').encode()
    manifest={name:hashlib.sha256(data).hexdigest() for name,data in payload.items()}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    payload['manifest.json']=json.dumps(manifest).encode()
    archive=io.BytesIO()
    with tarfile.open(fileobj=archive,mode='w') as tar:
        for name,data in payload.items():
            info=tarfile.TarInfo(name);info.size=len(data);tar.addfile(info,io.BytesIO(data))
    prepared=ps(f"$ErrorActionPreference='Stop';if(Test-Path '{remote}'){{throw 'task exists'}};New-Item -ItemType Directory '{remote}'|Out-Null;exit 0",'prepare.log')
    if prepared.returncode:return prepared.returncode
    (output/'task.json').write_text(json.dumps({'host':host,'directory':remote,'game_access':False},indent=2)+'\n')
    copied=subprocess.run(['ssh','-o','BatchMode=yes',host,f'tar -xf - -C "{remote}"'],input=archive.getvalue(),capture_output=True,timeout=60)
    (output/'copy.log').write_bytes(copied.stdout+copied.stderr)
    if copied.returncode:return copied.returncode
    checked=ps(f"$ErrorActionPreference='Stop';Set-Location '{remote}';$m=Get-Content manifest.json -Raw|ConvertFrom-Json;foreach($p in $m.PSObject.Properties){{if((Get-FileHash -LiteralPath $p.Name -Algorithm SHA256).Hash -ne $p.Value){{throw 'source hash mismatch'}}}};Write-Output ('HASHES='+@($m.PSObject.Properties).Count);exit 0",'hashes.log')
    if checked.returncode:return checked.returncode
    result=ps(preflight+f"Set-Location '{remote}';& $env:ComSpec /d /c build.cmd;exit $LASTEXITCODE",'result.log')
    for name in ['build.log','fault-build.log','execution.log','fault-execution.log']:
        log=ps(f"Get-Content -LiteralPath '{remote}/{name}' -ErrorAction SilentlyContinue",name)
        print(log.stdout.decode(errors='replace').strip(),flush=True)
    (output/'summary.json').write_text(json.dumps({'exit_code':result.returncode,'remote':remote,'sources':manifest,
        'scope':'real Win32 filesystem, isolated x86 executable; not native Grid8 or installed save integration'},indent=2)+'\n')
    return result.returncode

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',default='windows11-dev-next');parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();raise SystemExit(run(args.host,args.output.resolve()))
