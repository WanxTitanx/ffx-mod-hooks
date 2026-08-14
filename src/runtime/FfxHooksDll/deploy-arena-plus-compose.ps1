# Deploy Arena+ Custom Mix Phase 2 RT2 stack:
#   ffx-hooks.dll (PolyHook) + ArenaMultiBossLab.exe + recipes + compose flags/config
#
# Usage:
#   .\deploy-arena-plus-compose.ps1
#   .\deploy-arena-plus-compose.ps1 -VanillaBtlRoot "D:\FFX Extracted\FFX\ffx_ps2\ffx\master\jppc\battle\btl"
#   .\deploy-arena-plus-compose.ps1 -GameRoot "C:\...\FINAL FANTASY FFX&FFX-2 HD Remaster" -SkipDllBuild
#
param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster",
    [string]$VanillaBtlRoot = "D:\FFX Extracted\FFX\ffx_ps2\ffx\master\jppc\battle\btl",
    [switch]$SkipDllBuild,
    [switch]$SkipLabBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$hooksDir = Join-Path $repoRoot "RuntimeTools\FfxHooksDll"
$labProj  = Join-Path $repoRoot "RuntimeTools\ArenaMultiBossLab\ArenaMultiBossLab.csproj"
$labPub   = Join-Path $repoRoot "work\arena_lab_publish"
$configSrc = Join-Path $hooksDir "config"

if (-not (Test-Path -LiteralPath $GameRoot)) {
    throw "GameRoot not found: $GameRoot. Pass -GameRoot with your Steam FFX install path."
}
if (-not (Test-Path -LiteralPath $VanillaBtlRoot)) {
    throw "VanillaBtlRoot not found: $VanillaBtlRoot. Pass -VanillaBtlRoot with your extracted battle\btl path."
}

$modules = Join-Path $GameRoot "modules"
$config  = Join-Path $modules "config"
New-Item -ItemType Directory -Force -Path $modules, $config | Out-Null

if (-not $SkipDllBuild) {
    Write-Host "Building ffx-hooks.dll (PolyHook Release)..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $hooksDir "build_hooks.ps1") `
        -WithPolyHook -Release -Deploy -LabDeploy -GameRoot $GameRoot
} else {
    $dll = Join-Path $hooksDir "bin\Release\ffx-hooks.dll"
    if (-not (Test-Path -LiteralPath $dll)) { throw "Missing $dll - run without -SkipDllBuild." }
    Copy-Item -LiteralPath $dll -Destination (Join-Path $modules "ffx-hooks.dll") -Force
    Write-Host "Copied existing DLL -> $modules\ffx-hooks.dll"
}

if (-not $SkipLabBuild) {
    Write-Host "Publishing ArenaMultiBossLab (full folder)..."
    dotnet publish $labProj -c Release -o $labPub
} elseif (-not (Test-Path (Join-Path $labPub "ArenaMultiBossLab.exe"))) {
    throw "Missing $labPub\ArenaMultiBossLab.exe - run without -SkipLabBuild."
}

$labDest = Join-Path $modules "tools\ArenaMultiBossLab"
if (Test-Path -LiteralPath $labDest) { Remove-Item -LiteralPath $labDest -Recurse -Force }
New-Item -ItemType Directory -Force -Path $labDest | Out-Null
Get-ChildItem -LiteralPath $labPub | Copy-Item -Destination $labDest -Recurse -Force
Write-Host "Lab publish folder -> $labDest"

$labExePath = Join-Path $labDest "ArenaMultiBossLab.exe"
if (-not (Test-Path -LiteralPath $labExePath)) {
    throw "Lab deploy failed: missing $labExePath"
}
$labCfg = Join-Path $config "arena_plus_compose_lab.txt"
Set-Content -LiteralPath $labCfg -Value $labExePath -Encoding ascii -NoNewline
Write-Host "Wrote lab exe path -> $labCfg"

# Remove broken single-file deploy from older scripts (exe without .dll).
$legacyExe = Join-Path $modules "ArenaMultiBossLab.exe"
if (Test-Path -LiteralPath $legacyExe) { Remove-Item -LiteralPath $legacyExe -Force }
$legacyRecipes = Join-Path $modules "recipes"
if (Test-Path -LiteralPath $legacyRecipes) { Remove-Item -LiteralPath $legacyRecipes -Recurse -Force }

$vanillaCfg = Join-Path $config "arena_plus_compose_vanilla_btl.txt"
Set-Content -LiteralPath $vanillaCfg -Value $VanillaBtlRoot -Encoding ascii -NoNewline
Write-Host "Wrote vanilla btl root -> $vanillaCfg"

foreach ($flag in @(
    "arena_plus_compose_f7.flag",
    "arena_plus_combo_battles.flag",
    "arena_plus_charge_gil.flag",
    "arena_plus.flag",
    "arena_plus_music.flag",
    "music.flag",
    "native_menu.flag",
    "arena_plus_direct_request.flag"
)) {
    $src = Join-Path $configSrc $flag
    $dst = Join-Path $config $flag
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $dst -Force
    } else {
        New-Item -ItemType File -Force -Path $dst | Out-Null
    }
    Write-Host "Flag: $dst"
}

Write-Host ""
Write-Host "=== Arena+ Custom Mix Phase 2 deployed ==="
Write-Host "GameRoot      : $GameRoot"
Write-Host "Vanilla btl   : $VanillaBtlRoot"
Write-Host "Mod btl (def) : $GameRoot\data\mods\ffx_ps2\ffx\master\jppc\battle\btl"
Write-Host "Log           : %TEMP%\ffx-hooks.log"
Write-Host ""
Write-Host "RT2: restart FFX -> F7 -> Arena+ -> Custom Mix x3/x4/x5"
Write-Host "     Pick bosses -> Build + Launch Mix -> confirm fight matches picks"
Write-Host "Rollback picker: remove arena_plus_compose_f7.flag or FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7=1"
