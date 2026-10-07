. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
$ErrorActionPreference='Stop'
$env:FFXHOOKS_TRACE_WORKSHOP_GEAR='1'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\native-presentation-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$exe=Join-Path $obj 'NativePresentationRt1.exe'
$source=@('tests\NativePresentationRt1.cpp','hooks\ArcanaCore.cpp','hooks\ArcanaUiCore.cpp','hooks\ArcanaAcquisition.cpp','hooks\ArcanaNativeUi.cpp','hooks\EquipmentWorkshopRuntime.cpp','hooks\EquipmentWorkshopNativeUi.cpp','hooks\ElementHook.cpp','shared\Config.cpp','hooks\EquipmentWorkshopStore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp') | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib' -f $vcvars,$repo,$mh,($source -join ' '),$exe
Push-Location $obj
try {
 & $env:ComSpec /d /s /c $compileC
 if($LASTEXITCODE -ne 0){throw 'MinHook build failed'}
 & $env:ComSpec /d /s /c $cmd
 if($LASTEXITCODE -ne 0){throw 'Workshop runtime build failed'}
 $fixture=Join-Path $repo 'native-fixtures\FFX.exe'
 if((Get-FileHash $fixture -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Wrong private PE fixture'}
 $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
 foreach($scanMode in 0..5){
  & $exe $fixture (Join-Path $repo 'native-fixtures\ffx_000') ($data+'-'+$scanMode) (Join-Path $repo 'sin-fixtures\a_ability.bin') $scanMode
  if($LASTEXITCODE -ne 0){throw ('Workshop runtime RT1 failed: Scan mode '+$scanMode)}
 }
} finally {Pop-Location}
