#!/usr/bin/env python3
"""Run isolated x86 actual-source adapter tests. No game, deploy or policy change."""
from pathlib import Path
import argparse
import base64
import hashlib
import io
import json
import re
import secrets
import subprocess
import sys
import tarfile
from seymour_session_adapter_harness import CASES as SESSION
from seymour_menu_adapter_harness import CASES as MENU
from seymour_sort_adapter_harness import CASES as SORT

ROOT=Path(__file__).resolve().parents[2]
MOD=ROOT/'src/runtime/FfxHooksDll'
GEAR=('off invalid validate profile signature create create-rollback publish publish-rollback '
      'stop-before stop-during master-off master-missing other unknown off-after master-revoke '
      'revoke off-on write-rejected exception menu names retire-fail').split()
OD=('off invalid validate stop-before profile signature byte-mismatch pin alloc seal create1 create5 '
    'publish rollback counter events wrong-pointer null-pointer field zero-hp zero-damage inactive '
    'wrong-thread exception native-exception menu off-after reentrant revoke off-on stop-inside '
    'replace-table lose-owner turn retire-fail').split()

def run(host,output,adapters=('gear','overdrive')):
    recipes={
        'gear':('seymour_gear_adapter_harness.py',GEAR),
        'overdrive':('seymour_overdrive_adapter_harness.py',OD),
        'session':('seymour_session_adapter_harness.py',SESSION),
        'menu':('seymour_menu_adapter_harness.py',MENU),
        'sort':('seymour_sort_adapter_harness.py',SORT),
    }
    if not adapters or len(set(adapters))!=len(adapters) or any(name not in recipes for name in adapters):
        raise ValueError('select unique known adapters')
    output.mkdir(parents=True,exist_ok=False)
    remote='C:/VMTasks/seymour-adapters-'+secrets.token_hex(5)
    def ps(script):
        encoded=base64.b64encode(script.encode('utf-16le')).decode()
        return subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',host,
            'powershell','-NoProfile','-NonInteractive','-EncodedCommand',encoded],capture_output=True,timeout=300)
    def saved(name,result):
        (output/name).write_bytes(result.stdout+result.stderr)
        return result.returncode
    # Both inspectors and compilation are bounded to a fresh private directory.
    # An empty named Get-Process query leaves PowerShell's $? false. Enumerate
    # once, fail closed on query errors, and return success explicitly when idle.
    preflight=("$ErrorActionPreference='Stop';$p=@(Get-Process -ErrorAction Stop|"
               "Where-Object {$_.ProcessName -in @('cl','link','MSBuild')});"
               "if($p){$p|Select-Object Id,ProcessName;exit 75};")
    inspect=ps(preflight+'exit 0')
    if saved('inspect.log',inspect):return inspect.returncode
    payload={}
    for label in adapters:
        script=recipes[label][0]
        emitted=output/(label+'.cpp')
        result=subprocess.run([sys.executable,str(ROOT/'tools/recovery'/script),'--output',str(emitted)],capture_output=True,timeout=30)
        if saved(label+'-emission.log',result):return result.returncode
        payload[label+'.cpp']=emitted.read_bytes()
    for folder in ('hooks','shared'):
        for file in sorted((MOD/folder).rglob('*.h')):
            if file.is_symlink():raise ValueError('symlink in source headers')
            payload[str(file.relative_to(MOD))]=file.read_bytes()
    if 'session' in adapters:
        payload['hooks/RonsoPoolSave.cpp']=(MOD/'hooks/RonsoPoolSave.cpp').read_bytes()
    lines=['@echo off','call "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 >environment.log 2>&1',
           'if errorlevel 1 exit /b %errorlevel%']
    for label in adapters:
        extra=' hooks/RonsoPoolSave.cpp' if label=='session' else ''
        lines += [f'cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /O2 /Ihooks /Ishared {label}.cpp{extra} /Fe:{label}.exe >{label}-build.log 2>&1',
                  'if errorlevel 1 exit /b %errorlevel%']
    lines+=['exit /b 0']
    payload['build.cmd']=('\r\n'.join(lines)+'\r\n').encode()
    manifest={name:hashlib.sha256(data).hexdigest() for name,data in payload.items()}
    (output/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    payload['source-manifest.json']=json.dumps(manifest).encode()
    archive=io.BytesIO()
    with tarfile.open(fileobj=archive,mode='w') as tar:
        for name,data in payload.items():
            info=tarfile.TarInfo(name);info.size=len(data);tar.addfile(info,io.BytesIO(data))
    prepared=ps(f"$ErrorActionPreference='Stop';if(Test-Path '{remote}'){{throw 'task exists'}};New-Item -ItemType Directory '{remote}'|Out-Null")
    if saved('prepare.log',prepared):return prepared.returncode
    (output/'task.json').write_text(json.dumps({'directory':remote,'host':host,'game':False},indent=2)+'\n')
    extracted=subprocess.run(['ssh','-o','BatchMode=yes',host,f'tar -xf - -C "{remote}"'],input=archive.getvalue(),capture_output=True,timeout=60)
    if saved('extract.log',extracted):return extracted.returncode
    checked=ps(f"$ErrorActionPreference='Stop';Set-Location '{remote}';$m=Get-Content source-manifest.json -Raw|ConvertFrom-Json;foreach($p in $m.PSObject.Properties){{if((Get-FileHash -LiteralPath $p.Name -Algorithm SHA256).Hash -ne $p.Value){{throw 'source hash mismatch'}}}};Write-Output ('HASHES='+$m.PSObject.Properties.Count)")
    if saved('hashes.log',checked):return checked.returncode
    built=ps(preflight+f"Set-Location '{remote}';& $env:ComSpec /d /c build.cmd;exit $LASTEXITCODE")
    saved('build.log',built)
    for label in adapters:
        log=ps(f"Get-Content -LiteralPath '{remote}/{label}-build.log' -ErrorAction SilentlyContinue")
        saved(label+'-build.log',log)
    if built.returncode:
        (output/'summary.json').write_text(json.dumps({'compiled':False,'exit_code':built.returncode,'runtime_game_proven':False},indent=2)+'\n')
        print('ADAPTER_BUILD_EXIT',built.returncode,flush=True);return built.returncode
    results=[]
    for label in adapters:
        cases=recipes[label][1]
        for case in cases:
            result=ps(f"& '{remote}/{label}.exe' '{case}';exit $LASTEXITCODE")
            saved(label+'-'+case+'.log',result)
            match=re.search(r': (\d+)/(\d+) passed',result.stdout.decode(errors='replace'))
            passed,total=map(int,match.groups()) if match else (0,0)
            item={'adapter':label,'case':case,'exit_code':result.returncode,'passed':passed,'total':total,
                  'ok':result.returncode==0 and total>0 and passed==total}
            results.append(item)
            print(label,case,result.returncode,f'{passed}/{total}',flush=True)
    report={'compiled':True,'cases_passed':sum(r['ok'] for r in results),'cases_total':len(results),
            'results':results,'scope':'actual adapters with simulated endpoints in isolated Windows x86 executables',
            'runtime_game_proven':False,'source_manifest_sha256':hashlib.sha256((output/'source-manifest.json').read_bytes()).hexdigest()}
    (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    return 0 if report['cases_passed']==report['cases_total'] else 1

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',default='windows11-dev-next');p.add_argument('--output',required=True,type=Path)
    p.add_argument('--adapters',nargs='+',choices=('gear','overdrive','session','menu','sort'),default=['gear','overdrive'])
    a=p.parse_args();raise SystemExit(run(a.host,a.output.resolve(),a.adapters))
