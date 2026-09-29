#!/usr/bin/env python3
"""Build/test this worktree in its own Windows VM task directory.

No game launch, deployment, main-branch changes, global tool installation or VM
lifecycle operations. The VM and dependency paths are supplied explicitly.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import subprocess
import tarfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
TASK=r'C:\VMTasks\hooks-recovery-20260925'

def ps_quote(value:str)->str:return "'"+value.replace("'","''")+"'"
def powershell(host:str,script:str,check:bool=True)->subprocess.CompletedProcess:
    encoded=base64.b64encode(script.encode('utf-16le')).decode('ascii')
    return subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',host,
        'powershell','-NoProfile','-NonInteractive','-EncodedCommand',encoded],check=check)

def package()->Path:
    destination=ROOT/'.superpowers/recovery';destination.mkdir(parents=True,exist_ok=True)
    archive=destination/'source.tar';manifest=[]
    roots=['src/runtime/FfxHooksDll','src/runtime/FfxDinput8Probe',
           'src/runtime/NativeMenuShell','src/runtime/BattlePhotoMode',
           'contracts','research/equipment_workshop']
    excluded={'bin','obj','vcpkg_installed','.git','__pycache__','deploy','.superpowers'}
    with tarfile.open(archive,'w') as tar:
        for folder in roots:
            for source in sorted((ROOT/folder).rglob('*')):
                relative=source.relative_to(ROOT)
                if not source.is_file() or source.is_symlink() or any(x in excluded for x in relative.parts):continue
                if source.suffix.lower() in {'.dll','.exe','.pdb','.obj','.lib','.exp','.pch','.pyc'}:continue
                if source.stat().st_size>4*1024*1024:continue
                tar.add(source,arcname=str(relative),recursive=False)
                manifest.append({'path':str(relative),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
    (destination/'source-manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,'files':len(manifest)},indent=2))
    return archive

def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['inspect','prepare','build','collect'])
    parser.add_argument('--host',default='windows11-dev-next')
    parser.add_argument('--dependencies',help='verified x86-windows-static dependency directory on the VM')
    args=parser.parse_args()
    task=ps_quote(TASK)
    if args.action=='inspect':
        powershell(args.host,r'''
$ErrorActionPreference='Stop'
Write-Output 'RECOVERY_BUILD_PROCESSES'
Get-Process cl,link,MSBuild -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,Path | Format-Table -AutoSize
Write-Output 'RECOVERY_TASK_DIRECTORIES'
Get-ChildItem C:\VMTasks -Directory | Select-Object -ExpandProperty Name
Write-Output 'RECOVERY_COMPILER'
& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Write-Output 'RECOVERY_DEPENDENCY_CANDIDATES'
Get-ChildItem C:\VMTasks -Filter PolyHook_2.lib -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 12 -ExpandProperty FullName
''')
    elif args.action=='prepare':
        archive=package()
        powershell(args.host,f"$ErrorActionPreference='Stop'; New-Item -ItemType Directory -Force {task} | Out-Null")
        subprocess.run(['scp',str(archive),args.host+':'+TASK.replace('\\','/')+'/source.tar'],check=True)
        powershell(args.host,f"$ErrorActionPreference='Stop'; Set-Location {task}; tar -xf source.tar; if($LASTEXITCODE -ne 0){{exit $LASTEXITCODE}}; Write-Output 'RECOVERY_SOURCE_EXTRACTED'")
    elif args.action=='build':
        if not args.dependencies:parser.error('--dependencies is required after inspecting the VM')
        dependencies=ps_quote(args.dependencies)
        script=f'''
$ErrorActionPreference='Stop'
$task={task}
$dependencies={dependencies}
if(!(Test-Path (Join-Path $dependencies 'lib\\PolyHook_2.lib'))){{throw 'Unverified dependency directory'}}
$busy=Get-Process cl,link,MSBuild -ErrorAction SilentlyContinue
if($busy){{$busy | Select-Object Id,ProcessName | Format-Table; Write-Output 'RECOVERY_BUILD_BUSY'; exit 75}}
$project=Join-Path $task 'src\\runtime\\FfxHooksDll'
$mount=Join-Path $project 'vcpkg_installed\\x86-windows-static'
if(!(Test-Path $mount)){{
 New-Item -ItemType Directory -Force (Split-Path $mount) | Out-Null
 New-Item -ItemType Junction -Path $mount -Target $dependencies | Out-Null
}}
$log=Join-Path $task 'recovery-build.log'
try {{
 & (Join-Path $project 'build_hooks.ps1') -WithPolyHook -Release *> $log
 $result=$LASTEXITCODE
}} catch {{ $_ | Out-String | Add-Content $log; $result=1 }}
Get-Content $log -Tail 100
if($result -ne 0){{exit $result}}
$dll=Join-Path $project 'bin\\Release\\ffx-hooks.dll'
if(!(Test-Path $dll)){{throw 'Build produced no DLL'}}
Get-FileHash $dll -Algorithm SHA256 | Format-List
Write-Output 'RECOVERY_BUILD_OK'
'''
        powershell(args.host,script)
    else:
        destination=ROOT/'.superpowers/recovery';destination.mkdir(parents=True,exist_ok=True)
        subprocess.run(['scp',args.host+':'+TASK.replace('\\','/')+'/recovery-build.log',str(destination/'recovery-build.log')],check=True)

if __name__=='__main__':main()
