param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$modules = Join-Path $GameRoot "modules"
$configDir = Join-Path $modules "config"
$labConfig = Join-Path $PSScriptRoot "config"

if (-not (Test-Path $modules)) { throw "modules folder not found: $modules" }

$srcDll = Join-Path $repo "RuntimeTools\FfxHooksDll\deploy\ffx-hooks.dll"
if (-not (Test-Path $srcDll)) {
    $srcDll = Join-Path $repo "RuntimeTools\FfxHooksDll\ffx-hooks.dll"
}
if (Test-Path $srcDll) {
    Copy-Item $srcDll (Join-Path $modules "ffx-hooks.dll") -Force
    Write-Host "Copied ffx-hooks.dll -> modules"
} else {
    Write-Host "WARN: ffx-hooks.dll not built; using existing modules copy"
}

New-Item -ItemType Directory -Force -Path $configDir | Out-Null
Copy-Item (Join-Path $labConfig "ability_sfx.flag") (Join-Path $configDir "ability_sfx.flag") -Force
Write-Host "Installed config\ability_sfx.flag (read-only log)"

Write-Host "Done. Launch FFX; log: $env:TEMP\ffx-hooks.log"
