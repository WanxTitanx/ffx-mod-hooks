param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path

Write-Host "=== Nul Ward preflight (offline) ==="

Push-Location $repo
try {
    dotnet run -c Release --project FFXProjectEditor -- --nul-ward-static --kernel "$GameRoot\data\mods\ffx_ps2\ffx\master\new_uspc\battle\kernel\command.bin" --exe "$GameRoot\FFX.exe" --hooks-dll "$GameRoot\modules\ffx-hooks.dll"
    if ($LASTEXITCODE -ne 0) { throw "static RT2 failed" }

    dotnet run -c Release --project FFXProjectEditor -- --nul-ward-pack --deploy --kernel "$GameRoot\data\mods\ffx_ps2\ffx\master\new_uspc\battle\kernel\command.bin" --kernel-jp "$GameRoot\data\mods\ffx_ps2\ffx\master\jppc\battle\kernel\command.bin"
    if ($LASTEXITCODE -ne 0) { throw "pack deploy failed" }

    & (Join-Path $PSScriptRoot "install_to_modules.ps1") -EnableApply -GameRoot $GameRoot
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "Preflight PASS. One in-game RT2: cast 320/321, Holy/Dark hit, check %TEMP%\ffx-hooks.log"
