param(
    [Parameter(Mandatory=$true)][string]$ReferenceMinHook,
    [string]$OutputDirectory = "$PSScriptRoot\work\fahrenheit-minhook-rt1"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$reference = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReferenceMinHook)
if ($reference -eq [IO.Path]::GetFullPath("$root\third_party\minhook")) {
    throw 'Use an independent, unmodified reference provider.'
}
New-Item -ItemType Directory -Force $out | Out-Null
$programFiles = [Environment]::GetFolderPath('ProgramFilesX86')
$vswhere = Join-Path $programFiles 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual C++ x86 compiler not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$exports = @'
EXPORTS
MH_Initialize=_MH_Initialize@0
MH_Uninitialize=_MH_Uninitialize@0
MH_ApplyQueued=_MH_ApplyQueued@0
MH_CreateHook=_MH_CreateHook@12
MH_EnableHook=_MH_EnableHook@4
MH_DisableHook=_MH_DisableHook@4
MH_RemoveHook=_MH_RemoveHook@4
MH_QueueEnableHook=_MH_QueueEnableHook@4
'@
$def = Join-Path $out 'provider.def'
[IO.File]::WriteAllText($def, $exports, [Text.Encoding]::ASCII)
foreach ($entry in @(@('guarded', "$root\third_party\minhook"), @('reference', $reference))) {
    $label, $vendor = $entry
    $providerDef = $def
    if ($label -eq 'guarded') {
        $providerDef = Join-Path $out 'guarded-provider.def'
        [IO.File]::WriteAllText($providerDef, $exports + "`nMH_BindSharedProvider=_MH_BindSharedProvider@4`n", [Text.Encoding]::ASCII)
    }
    $objects = Join-Path $out $label
    New-Item -ItemType Directory -Force $objects | Out-Null
    $sources = @('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object { '"' + (Join-Path "$vendor\src" $_) + '"' }
    $arguments = '/nologo /LD /O2 /MT /W3 ' + ($sources -join ' ') +
        (' /Fo"{0}\\" /Fe:"{1}\{2}.dll" /link /DEF:"{3}"' -f $objects, $out, $label, $providerDef)
    $rsp = Join-Path $objects 'build.rsp'
    [IO.File]::WriteAllText($rsp, $arguments, [Text.Encoding]::Unicode)
    $buildCommand = '"' + $vcvars + '" x86 >nul 2>&1 && cl @"' + $rsp + '"'
    & cmd /d /c $buildCommand
    if ($LASTEXITCODE -ne 0) { throw "$label provider build failed ($LASTEXITCODE)." }
}
$harness = Join-Path $out 'fahrenheit-minhook-rt1.exe'
$rsp = Join-Path $out 'harness.rsp'
$arguments = '/nologo /EHsc /std:c++17 /Od /MT /W4 "{0}\tests\FahrenheitMinHookRt1.cpp" /Fo"{1}\\" /Fe:"{2}"' -f $root, $out, $harness
[IO.File]::WriteAllText($rsp, $arguments, [Text.Encoding]::Unicode)
$buildCommand = '"' + $vcvars + '" x86 >nul 2>&1 && cl @"' + $rsp + '"'
& cmd /d /c $buildCommand
if ($LASTEXITCODE -ne 0) { throw "Harness build failed ($LASTEXITCODE)." }
$failed = @()
foreach ($scenario in @('normal','foreign-first','create-enable','queued-conflict','remove-foreign','hotpatch-conflict','trampoline-conflict','shared-provider')) {
    & $harness $scenario "$out\guarded.dll" "$out\reference.dll"
    if ($LASTEXITCODE -ne 0) { $failed += $scenario }
}
if ($failed.Count) { throw ('Failed scenarios: ' + ($failed -join ', ')) }
Write-Host 'Fahrenheit dual-provider MinHook RT1: all scenarios passed.'
