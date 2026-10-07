. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
# Jarvis-HOOK: isolated Vanguard producers; no live game.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\vanguard-overdrive-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$exe=Join-Path $obj 'VanguardOverdriveRt1.exe'
$source=@('hooks\VanguardRuntime.cpp','hooks\F8FlagCatalog.cpp','tests\VanguardOverdriveRt1.cpp','hooks\EquipmentWorkshopRuntime.cpp','shared\Config.cpp','hooks\EquipmentWorkshopStore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp','hooks\RonsoPoolCore.cpp','hooks\RonsoPoolRuntime.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp') | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
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
 $crt=Join-Path (Split-Path $fixture) 'msvcr110.dll'
 if((Get-FileHash $crt -Algorithm SHA256).Hash -ne 'B30160E759115E24425B9BCDF606EF6EBCE4657487525EDE7F1AC40B90FF7E49'){throw 'Wrong private CRT'}
 Copy-Item -LiteralPath $crt -Destination (Join-Path $obj 'msvcr110.dll')
 New-Item -ItemType Directory -Path $data | Out-Null
 & $exe $fixture $data
 if($LASTEXITCODE -ne 0){throw 'Workshop runtime RT1 failed'}
 & $exe $fixture ($data+'-equipment') equipment
 if($LASTEXITCODE -ne 0){throw 'Equipped command runtime failed'}
} finally {Pop-Location}
