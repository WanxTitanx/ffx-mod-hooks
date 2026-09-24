# Focused RT0 denylist for the two legacy F7 prototypes that can touch disk or launch tools.
$ErrorActionPreference = 'Stop'

$runtimeRoot = Split-Path -Parent $PSScriptRoot
$dllmainPath = Join-Path $runtimeRoot 'dllmain.cpp'
$sinHookPath = Join-Path $runtimeRoot 'hooks\SinCurseHook.cpp'
$f7InLivePath = Join-Path $runtimeRoot 'hooks\F7InLive.cpp'
$customMixRuntimePath = Join-Path $runtimeRoot 'hooks\CustomMixRuntime.cpp'
$customMixCorePath = Join-Path $runtimeRoot 'hooks\CustomMixUltraCore.cpp'
$nativeMenuPath = Join-Path (Split-Path -Parent $runtimeRoot) 'NativeMenuShell\NativeMenuShell.h'

$dllmain = [IO.File]::ReadAllText($dllmainPath)
$sinHook = [IO.File]::ReadAllText($sinHookPath)
$f7InLive = [IO.File]::ReadAllText($f7InLivePath)
$customMixRuntime = [IO.File]::ReadAllText($customMixRuntimePath)
$customMixCore = [IO.File]::ReadAllText($customMixCorePath)
$nativeMenu = [IO.File]::ReadAllText($nativeMenuPath)
$failures = 0

function Require-Text([string]$Text, [string]$Token, [string]$Claim) {
    if ($Text.Contains($Token)) {
        Write-Host "PASS: $Claim"
        return
    }
    $script:failures++
    Write-Host "FAIL: $Claim (missing '$Token')"
}

function Forbid-Text([string]$Text, [string]$Token, [string]$Claim) {
    if (-not $Text.Contains($Token)) {
        Write-Host "PASS: $Claim"
        return
    }
    $script:failures++
    Write-Host "FAIL: $Claim (found '$Token')"
}

foreach ($token in @(
    'ArenaMultiBossLab.exe',
    'ultra_manifest.json',
    'F7_Ultra.bin'
)) {
    Forbid-Text $dllmain $token "CustomMix Ultra runtime cannot retain $token"
}

foreach ($token in @(
    'CreateProcessA',
    'WaitForSingleObject',
    'SinScaleInject.exe',
    'GraphicFieldMapLoad_SinCurseHook',
    'GetEnvironmentVariableA',
    'FFXHOOKS_ENABLE_SIN_CURSE',
    'sin_curse.flag',
    'sin_f7_intensity.flag',
    '.bin'
)) {
    Forbid-Text $sinHook $token "legacy S.I.N. hook cannot retain source-reader/writer token $token"
}

foreach ($token in @(
    'SinCurse_ToggleOnOff',
    'SinCurse_WriteIntensity',
    'SinCurse_CycleIntensity',
    'MoveFileA(onPath',
    'CreateFileA(onPath',
    'config\\sin_curse.flag'
)) {
    Forbid-Text $dllmain $token "S.I.N. menu cannot retain $token"
}

Require-Text $dllmain 'ArenaPlusMenuKind::Ultra' 'Arena+ exposes only the reviewed Ultra native submenu'
Require-Text $dllmain 'CustomMixUltraExactCarrier' 'Ultra uses a closed exact-carrier launch authority'
Require-Text $customMixRuntime 'RunProductionBattle' 'Ultra battle execution stays in the reviewed RAM-only runtime'
Require-Text $customMixCore 'ExecuteTransaction' 'the reviewed core retains the sole transaction writer'
foreach ($token in @('MH_CreateHook','MH_EnableHook','MH_DisableHook','MH_RemoveHook','CreateProcess','CreateFile','WriteFile','ArenaMultiBossLab','0x1158')) {
    Forbid-Text $customMixRuntime $token "reviewed CustomMix runtime cannot retain unsafe token $token"
}
Require-Text $nativeMenu 'S.I.N. RAM' 'main F7 menu exposes the reviewed RAM-only S.I.N. surface'
Forbid-Text $nativeMenu 'S.I.N. - Unavailable' 'main F7 menu cannot retain the obsolete read-only S.I.N. label'
Require-Text $dllmain 'InstallSinCurseHook' 'legacy S.I.N. hook remains explicitly quarantined at startup'
Require-Text $dllmain 'S.I.N. RAM' 'S.I.N. submenu names the reviewed RAM-only implementation'
Require-Text $dllmain 'Natural only; fields 310/340; 9 catalog IDs' 'S.I.N. submenu exposes the exact bounded scope'
foreach ($token in @('INVALID','OFF','UNAVAILABLE','WAIT NATURAL','CURRENT NATURAL')) {
    Require-Text $f7InLive ('"' + $token + '"') "typed S.I.N. runtime retains truthful status $token"
}
$sinStart = $dllmain.IndexOf('// -- S.I.N. RAM submenu state --')
$sinEnd = if ($sinStart -ge 0) { $dllmain.IndexOf('static const char* kArenaPlusDarkNames', $sinStart) } else { -1 }
if ($sinStart -lt 0 -or $sinEnd -le $sinStart) {
    $failures++
    Write-Host 'FAIL: reviewed S.I.N. RAM menu block was not found'
} else {
    $sinMenu = $dllmain.Substring($sinStart, $sinEnd - $sinStart)
    foreach ($token in @('CreateProcess','GetEnvironmentVariable','sin_curse.flag','sin_f7_intensity.flag','.bin','MH_CreateHook')) {
        Forbid-Text $sinMenu $token "reviewed S.I.N. RAM menu cannot retain legacy surface $token"
    }
}
Require-Text $dllmain 'static const int ARENA_PLUS_PRESET_COMBO_COUNT = 5;' 'standard Arena+ x3/x4/x5 preset count is preserved'
Require-Text $dllmain 'static const int ARENA_PLUS_CUSTOM_MIX_COMBO_COUNT = 3;' 'standard Custom Mix count is preserved'
Require-Text $dllmain 'g_arenaPlusMixRequiredSlots = static_cast<uint8_t>(row + 3)' 'standard Mix routes use the bounded RAM editor'
Require-Text $dllmain 'ArenaPlus_LaunchComboBattleFromPump(combo)' 'standard combo launch routing is preserved'

if ($failures -ne 0) {
    throw "F7 unsafe containment RT0 failed: $failures contract(s)"
}

Write-Host 'F7 UNSAFE CONTAINMENT RT0: PASS'
