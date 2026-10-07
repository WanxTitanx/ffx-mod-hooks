. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
# Jarvis-HOOK: isolated Vanguard producers; no live game.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\vanguard-action-runtime-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$exe=Join-Path $obj 'VanguardActionRuntimeRt1.exe'
$source=@('hooks\VanguardRuntime.cpp','hooks\SharedBattleRuntime.cpp','hooks\F8FlagCatalog.cpp','tests\VanguardActionRuntimeRt1.cpp','hooks\EquipmentWorkshopRuntime.cpp','shared\Config.cpp','hooks\EquipmentWorkshopStore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp') | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib user32.lib' -f $vcvars,$repo,$mh,($source -join ' '),$exe
Push-Location $obj
try {
 & $env:ComSpec /d /s /c $compileC
 if($LASTEXITCODE -ne 0){throw 'MinHook build failed'}
 & $env:ComSpec /d /s /c $cmd
 if($LASTEXITCODE -ne 0){throw 'Workshop runtime build failed'}
 $fixture=Join-Path $repo 'native-fixtures\FFX.exe'
 if((Get-FileHash $fixture -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Wrong private PE fixture'}
 $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
 & $exe $fixture
 if($LASTEXITCODE -ne 0){throw 'Workshop runtime RT1 failed'}
 & $exe $fixture buffs
 if($LASTEXITCODE -ne 0){throw 'Vanguard temporary buff runtime failed'}
 & $exe $fixture follow
 if($LASTEXITCODE -ne 0){throw 'Vanguard follow-up runtime failed'}
 & $exe $fixture follow-vampirism
 if($LASTEXITCODE -ne 0){throw 'Wakka follow-up/Vampirism composition RT1 failed'}
 & $exe $fixture threaten
 if($LASTEXITCODE -ne 0){throw 'Vanguard Threaten runtime failed'}
 & $exe $fixture energy
 if($LASTEXITCODE -ne 0){throw 'Vanguard energy action runtime failed'}
 & $exe $fixture shared-first
 if($LASTEXITCODE -ne 0){throw 'Vanguard shared action composition failed'}
} finally {Pop-Location}
