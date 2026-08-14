param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$hooksDll = Join-Path $repo "RuntimeTools\FfxHooksDll\deploy\ffx-hooks.dll"
if (-not (Test-Path $hooksDll)) {
    $hooksDll = Join-Path $GameRoot "modules\ffx-hooks.dll"
}

Write-Host "=== Ability SFX lab preflight (offline) ==="
Write-Host "hooks: $hooksDll"

if (-not (Test-Path $hooksDll)) {
    Write-Host "WARN: build hooks first: RuntimeTools\FfxHooksDll\build_hooks.ps1 -WithPolyHook"
}

Push-Location $repo
try {
    dotnet run -c Release --project FFXProjectEditor -- --magicdll-sound-corpus-wave6 2>$null
    if ($LASTEXITCODE -ne 0) { Write-Host "WARN: wave6 corpus skipped (magic root missing)" }

    & (Join-Path $PSScriptRoot "install_to_modules.ps1") -GameRoot $GameRoot
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "Preflight done. RT2: cast Fire/Firaga in battle; check %TEMP%\ffx-hooks.log for AbilitySfx lines"
