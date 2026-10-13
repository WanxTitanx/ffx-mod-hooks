param([Parameter(Mandatory=$true)][string]$CommandPath)
. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\shared-command-rt1';New-Item -ItemType Directory -Force $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$fixture=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash $fixture).Hash -ne (Get-FfxTestExecutableHash)){throw 'Wrong private PE fixture'}
Push-Location $obj
try {
    $compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
    & $env:ComSpec /c $compileC;if($LASTEXITCODE){throw 'MinHook build failed'}
    $sources=@('tests\SharedCommandGridVanguardRt1.cpp','hooks\VanguardRuntime.cpp','hooks\SharedBattleRuntime.cpp',
      'hooks\GridTeachHook.cpp','hooks\KimahriLancetDualGrantHook.cpp','hooks\F8FlagCatalog.cpp',
      'hooks\EquipmentWorkshopRuntime.cpp','shared\Config.cpp','hooks\EquipmentWorkshopStore.cpp',
      'hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp') |
      ForEach-Object {'"'+(Join-Path $here $_)+'"'}
    $exe=Join-Path $obj 'SharedCommandGridVanguardRt1.exe'
    $compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib user32.lib' -f $vcvars,$repo,$mh,($sources -join ' '),$exe
    & $env:ComSpec /c $compile;if($LASTEXITCODE){throw 'Shared command composition compile failed'}
    $failed=$false
    foreach($mode in @('grid-first','vanguard-first')){
        & $exe $fixture $mode $CommandPath
        if($LASTEXITCODE){$failed=$true}
    }
    if($failed){throw 'Shared GridTeach/Vanguard regression failed'}
}finally{Pop-Location}
