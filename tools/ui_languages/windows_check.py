#!/usr/bin/env python3
"""Jarvis-HOOK: source-verified disposable Windows checks; no deployment."""
import argparse, base64, hashlib, json, subprocess, sys, uuid, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from run_mod_runtime_checks import snapshot, powershell_literal

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', action='append', default=[], choices=[
        'ui_language_rt1.ps1', 'f8_runtime_rt0.ps1', 'f7_ui_rt0.ps1', 'equipment_workshop_menu_rt1.ps1'])
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--font-fixture',type=Path)
    args = parser.parse_args()
    if not args.test and not args.build: parser.error('Select tests or a build')
    lane = 'ui-language-' + uuid.uuid4().hex[:12]
    out = ROOT / '.superpowers/ui-languages' / lane
    out.mkdir(parents=True)
    archive = out / 'source.zip'
    entries = snapshot(archive, None)
    remote = 'C:/VMTasks/' + lane
    receipt = dict(remote_lane=remote, source_manifest=entries, tests=args.test,
                   build=args.build, deployed=False, completed=False)
    def save(): (out / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    save()
    print('EVIDENCE', out, flush=True)
    subprocess.run(['scp','-o','BatchMode=yes','-o','ConnectTimeout=8',str(archive),
        'windows11-dev-next:'+remote+'.zip'], check=True, timeout=120)
    fixture_hash=''
    if args.font_fixture:
        private=out/'font-fixture.zip';members=[]
        with zipfile.ZipFile(private,'w',zipfile.ZIP_DEFLATED) as package:
            for name in ['native-fonts.vbf','us-0.rgba','truncated.vbf','bad-extent.vbf','bad-block.vbf']:
                data=(args.font_fixture/name).read_bytes()
                if len(data)>32*1024*1024:raise ValueError('Private font fixture exceeds its bound')
                package.writestr(name,data);members.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
            package.writestr('fixture-manifest.json',json.dumps(members))
        fixture_hash=hashlib.sha256(private.read_bytes()).hexdigest()
        receipt['private_font_fixture']=dict(sha256=fixture_hash,members=members);save()
        subprocess.run(['scp','-q',str(private),'windows11-dev-next:'+remote+'-fonts.zip'],check=True,timeout=120)
    script = """
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$lane=__LANE__
if(Test-Path $lane){throw 'Lane already exists'}
if((Get-PSDrive C).Free -lt 2GB){throw 'Insufficient space'}
if(Get-Process cl,link,MSBuild,ninja -ErrorAction SilentlyContinue){throw 'Other compiler is active'}
if((Get-FileHash ($lane+'.zip')).Hash.ToLowerInvariant() -ne __HASH__){throw 'Archive mismatch'}
Expand-Archive ($lane+'.zip') $lane
$manifest=Get-Content ($lane+'/source-manifest.json') -Raw | ConvertFrom-Json
foreach($e in $manifest){if((Get-FileHash (Join-Path $lane $e.path)).Hash.ToLowerInvariant() -ne $e.sha256){throw 'Source mismatch'}}
$here=$lane+'/src/runtime/FfxHooksDll'
if(__FONTS__){
 if((Get-FileHash ($lane+'-fonts.zip')).Hash.ToLowerInvariant() -ne __FONT_HASH__){throw 'Private font ZIP mismatch'}
 Expand-Archive -LiteralPath ($lane+'-fonts.zip') -DestinationPath ($lane+'/native-fonts')
 $fonts=Get-Content -Raw ($lane+'/native-fonts/fixture-manifest.json') | ConvertFrom-Json
 foreach($f in $fonts){$p=Join-Path ($lane+'/native-fonts') $f.path
  if((Get-Item $p).Length -ne $f.bytes -or (Get-FileHash $p).Hash.ToLowerInvariant() -ne $f.sha256){throw 'Private font source mismatch'}}
}
foreach($test in @(__TESTS__)){
 $extra=@();if(__FONTS__ -and $test -eq 'ui_language_rt1.ps1'){$extra=@('-NativeFontFixture',($lane+'/native-fonts'))}
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ($here+'/'+$test) @extra *> ($lane+'/'+$test+'.log')
 $code=$LASTEXITCODE
 Get-Content ($lane+'/'+$test+'.log') -Tail 20
 if($code -ne 0){exit $code}
}
if(__BUILD__){
 New-Item -ItemType Junction -Path ($here+'/vcpkg_installed') -Target 'C:/vcpkg/installed' | Out-Null
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ($here+'/build_hooks.ps1') -WithPolyHook -Release *> ($lane+'/build.log')
 $code=$LASTEXITCODE
 Get-Content ($lane+'/build.log') -Tail 35
 if($code -ne 0){exit $code}
 Get-FileHash ($here+'/bin/Release/ffx-hooks.dll') | Format-List
}
Write-Output 'UI_WINDOWS_PASS'
"""
    values = {'__LANE__':powershell_literal(remote),
              '__FONTS__':'$true' if args.font_fixture else '$false',
              '__FONT_HASH__':powershell_literal(fixture_hash),
              '__HASH__':powershell_literal(hashlib.sha256(archive.read_bytes()).hexdigest()),
              '__TESTS__':','.join(map(powershell_literal,args.test)),
              '__BUILD__':'$true' if args.build else '$false'}
    for key,value in values.items(): script=script.replace(key,value)
    (out/'run.ps1').write_text(script)
    encoded = base64.b64encode(script.encode('utf-16le')).decode()
    with (out/'windows.log').open('wb') as log:
        result = subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',
            'windows11-dev-next','powershell.exe','-NoProfile','-EncodedCommand',encoded],
            stdout=log,stderr=subprocess.STDOUT,timeout=1200)
    receipt.update(completed=True,exit_code=result.returncode)
    save()
    print((out/'windows.log').read_text(errors='replace')[-14000:],flush=True)
    print('EXIT',result.returncode,'EVIDENCE',out,flush=True)
    return result.returncode

if __name__=='__main__': raise SystemExit(main())
