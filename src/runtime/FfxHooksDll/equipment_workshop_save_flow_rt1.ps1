$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\equipment-workshop-save-flow-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$exe=Join-Path $obj 'EquipmentWorkshopSaveFlowRt1.exe'
$source=@('tests\EquipmentWorkshopSaveFlowRt1.cpp','hooks\EquipmentWorkshopRuntime.cpp','hooks\EquipmentWorkshopStore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp','hooks\RonsoPoolRuntime.cpp','hooks\RonsoPoolCore.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp') | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib' -f $vcvars,$repo,$mh,($source -join ' '),$exe
Push-Location $obj
try {
 & $env:ComSpec /d /s /c $compileC
 if($LASTEXITCODE -ne 0){throw 'MinHook build failed'}
 & $env:ComSpec /d /s /c $cmd
 if($LASTEXITCODE -ne 0){throw 'Workshop runtime build failed'}
 $fixture=Join-Path $repo 'native-fixtures\FFX.exe'
 if((Get-FileHash $fixture -Algorithm SHA256).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'){throw 'Wrong private PE fixture'}
 Copy-Item (Join-Path $repo 'native-fixtures\msvcr110.dll') (Join-Path $obj 'msvcr110.dll') -Force
 foreach($mode in @('off','on')){
  $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
  Write-Output ('WORKSHOP_SAVE_FLOW_RONSO '+$mode)
  & $exe $fixture (Join-Path $repo 'native-fixtures\ffx_000') $data $mode
  if($LASTEXITCODE -ne 0){throw ('Workshop native save flow failed: Ronso '+$mode)}
 }
} finally {Pop-Location}
