. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
# Jarvis-HOOK: bounded isolated x86 runtime; no game session or deployment.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\arcana-spira-composition-rt1';New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$files=@('tests\ArcanaRuntimeRt1.cpp','hooks\EquipmentWorkshopRuntime.cpp','hooks\EquipmentWorkshopStore.cpp',
 'hooks\VanguardRuntime.cpp','hooks\F8FlagCatalog.cpp','hooks\ArcanaCore.cpp','hooks\ArcanaAcquisition.cpp','hooks\ArcanaStore.cpp','hooks\ArcanaNativeEffects.cpp','hooks\ArcanaRuntime.cpp','hooks\ArcanaCombatCore.cpp','hooks\ArcanaCombat.cpp',
 'hooks\SharedBattleRuntime.cpp','hooks\RonsoPoolCore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp',
 'hooks\RonsoPoolRuntime.cpp','hooks\NovaSuperDamageHook.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp','shared\Config.cpp')
if(Test-Path (Join-Path $here 'hooks\SpiraRuntime.cpp')){$files+='hooks\SpiraRuntime.cpp'}
$sources=$files | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$exe=Join-Path $obj 'ArcanaSpiraCompositionRt1.exe'
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_SPIRA_COMPOSITION /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib user32.lib' -f $vcvars,$repo,$mh,($sources -join ' '),$exe
$fixture=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Wrong private PE'}
Push-Location $obj
try{
 & $env:ComSpec /d /s /c $compileC;if($LASTEXITCODE -ne 0){throw 'MinHook compilation failed'}
 & $env:ComSpec /d /s /c $compile;if($LASTEXITCODE -ne 0){throw 'Spira compilation failed'}
 $failed=$false
 foreach($mode in @('spira-first','arcana-first','shared-only','v4')){
  $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $data | Out-Null
  & $exe $fixture (Join-Path $repo 'native-fixtures\ffx_000') $data $mode (Join-Path $repo 'sin-fixtures\a_ability.bin')
  Write-Output ('ARCANA_SPIRA_RESULT {0} exit={1}' -f $mode,$LASTEXITCODE)
  if($LASTEXITCODE -ne 0){$failed=$true}
 }
 if($failed){throw 'Arcana/Spira composition failed'}
}finally{Pop-Location}
