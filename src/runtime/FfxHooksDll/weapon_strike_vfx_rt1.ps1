param([string]$FixtureRoot)
. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
if(-not $FixtureRoot){$FixtureRoot=Join-Path (Resolve-Path (Join-Path $here '..\..\..')).Path 'native-fixtures'}
$pe=Join-Path $FixtureRoot 'FFX.exe'
$effect=Join-Path $FixtureRoot 'et_battle.bin'
if((Get-FileHash $pe -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Unsupported private PE'}
if((Get-FileHash $effect -Algorithm SHA256).Hash -ne '1F4EBC789A7815C6A9162BC921B30ECD58A8F262733D47082D77B718769656E2'){throw 'Unsupported private battle effect'}
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\weapon-strike-vfx-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c')|ForEach-Object{'"'+(Join-Path $mh ('src\'+$_))+'"'}
$compileC='call "{0}" x86 >nul && cl.exe /nologo /TC /MT /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$files=@('tests\WeaponStrikeVfxRt1.cpp','hooks\WeaponStrikeVfxRuntime.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp')|ForEach-Object{'"'+(Join-Path $here $_)+'"'}
$exe=Join-Path $obj 'WeaponStrikeVfxRt1.exe'
$compile='call "{0}" x86 >nul && cl.exe /nologo /EHsc /std:c++17 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\..\..\..\research\equipment_workshop\include" /I"{2}\include" {3} hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib user32.lib' -f $vcvars,$here,$mh,($files -join ' '),$exe
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $compileC;if($LASTEXITCODE -ne 0){throw 'MinHook compilation failed'}
    & $env:ComSpec /d /s /c $compile;if($LASTEXITCODE -ne 0){throw 'Weapon VFX compilation failed'}
    foreach($case in @('off','validate','signature','foreign','oom','readonly','caller','thread','vanilla','epoch','active')){
        & $exe $pe $effect $case
        if($LASTEXITCODE -ne 0){throw "Weapon VFX RT1 failed: $case"}
    }
} finally {Pop-Location}
