param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [Parameter(Mandatory=$true)][string]$FixtureDirectory,
    [Parameter(Mandatory=$true)][string]$ManifestPath,
    [Parameter(Mandatory=$true)][string]$DarkFormationDirectory,
    [string]$OutputDirectory=(Join-Path $PSScriptRoot 'obj\omega-last-seal-rt1')
)
# Jarvis-HOOK: isolated PE/native routines only; never starts or installs FFX.
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutablePath).Hash -ne '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'){throw 'Exact Steam PE required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 tools required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$source=$PSScriptRoot;$mh=Join-Path $source 'third_party\minhook'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path $OutputDirectory).Path
$flags='/nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TARGET_STEAM_20261001 /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"'+$source+'" /I"'+$mh+'\include"'
function Compile([string[]]$Names,[string]$Target){
    $sources=@($Names + @('hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp')) | ForEach-Object {'"'+(Join-Path $source $_)+'"'}
    $cmd='call "{0}" x86 >nul && cl {1} {2} hook.obj buffer.obj trampoline.obj hde32.obj /Fe:"{3}" user32.lib' -f $vcvars,$flags,($sources -join ' '),$Target
    & $env:ComSpec /d /s /c $cmd
    if($LASTEXITCODE){throw "Compilation failed: $Target"}
}
Push-Location $OutputDirectory
try {
    $cc='call "{0}" x86 >nul && cl /nologo /c /TC /MT /O2 /I"{1}\include" "{1}\src\hook.c" "{1}\src\buffer.c" "{1}\src\trampoline.c" "{1}\src\hde\hde32.c"' -f $vcvars,$mh
    & $env:ComSpec /d /s /c $cc;if($LASTEXITCODE){throw 'MinHook compilation failed'}
    Compile @('tests\OmegaLastSealRuntimeRt1.cpp','hooks\OmegaLastSealRuntime.cpp') 'OmegaLastSealRuntimeRt1.exe'
    & '.\OmegaLastSealRuntimeRt1.exe' $ExecutablePath $FixtureDirectory $ManifestPath
    if($LASTEXITCODE){throw 'Last Seal controller regression failed'}
    Compile @('tests\ArcanaChallengeRuntimeRt1.cpp','hooks\ArcanaChallengeRuntime.cpp','hooks\ArcanaCore.cpp') 'ArcanaChallengeRuntimeRt1.exe'
    & '.\ArcanaChallengeRuntimeRt1.exe' $ExecutablePath $DarkFormationDirectory
    if($LASTEXITCODE){throw 'Original Dark challenge regression failed'}
    Compile @('tests\OmegaLastSealNoticeRt1.cpp','hooks\ArcanaCore.cpp','hooks\ArcanaFieldDialogCore.cpp','hooks\ArcanaPresentation.cpp') 'OmegaLastSealNoticeRt1.exe'
    & '.\OmegaLastSealNoticeRt1.exe' $ExecutablePath
    if($LASTEXITCODE){throw 'Last Seal notice regression failed'}
    Write-Output 'PASS Last Seal native controller, challenge receipts and battle notices; live RT2 is separate'
} finally {Pop-Location}
