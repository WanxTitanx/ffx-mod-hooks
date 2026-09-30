param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster",
    [switch]$EnableLogOnly,
    [switch]$EnableApply,
    [switch]$SkipDll
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$modulesDir = Join-Path $GameRoot "modules"
$configDir = Join-Path $modulesDir "config"

if (-not (Test-Path $modulesDir)) {
    throw "modules not found: $modulesDir"
}
$proc = Get-Process FFX -ErrorAction SilentlyContinue
if ($proc) {
    throw "FFX is running (PID $($proc.Id)). Close the game first."
}

New-Item -ItemType Directory -Force -Path $configDir | Out-Null

if (-not $SkipDll) {
    $destDll = Join-Path $modulesDir "ffx-hooks.dll"
    $hooksRoot = (Resolve-Path (Join-Path $here "..\..")).Path
    $releaseDll = Join-Path $hooksRoot "bin\Release\ffx-hooks.dll"
    $bundledDll = Join-Path $here "ffx-hooks.dll"
    if (Test-Path $releaseDll) {
        $srcDll = $releaseDll
    } elseif (Test-Path $bundledDll) {
        $srcDll = $bundledDll
        Write-Host "WARN: using bundled deploy DLL (run build_hooks.ps1 -WithPolyHook -Release first)"
    } else {
        throw "No ffx-hooks.dll found. Build: RuntimeTools\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release"
    }
    if (Test-Path $destDll) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $backup = Join-Path $modulesDir "ffx-hooks.dll.backup-ronso-deploy-$stamp"
        Copy-Item -LiteralPath $destDll -Destination $backup -Force
        Write-Host "backup: $backup"
    }
    Copy-Item -LiteralPath $srcDll -Destination $destDll -Force
    $hash = (Get-FileHash -LiteralPath $destDll -Algorithm SHA256).Hash.Substring(0, 16)
    Write-Host "deployed dll: $destDll"
    Write-Host "source: $srcDll"
    Write-Host "sha256-prefix: $hash"
}

$flagLog = Join-Path $here "config\kimahri_ronso_mana.flag"
$flagApply = Join-Path $here "config\kimahri_ronso_mana_apply.flag"
$destLog = Join-Path $configDir "kimahri_ronso_mana.flag"
$destApply = Join-Path $configDir "kimahri_ronso_mana_apply.flag"

Copy-Item -LiteralPath $flagLog -Destination $destLog -Force
Write-Host "flag: $destLog"

if ($EnableApply) {
    Copy-Item -LiteralPath $flagApply -Destination $destApply -Force
    Write-Host "flag: $destApply (apply mode)"
} elseif ($EnableLogOnly) {
    if (Test-Path $destApply) {
        Remove-Item -LiteralPath $destApply -Force
        Write-Host "removed apply flag (log-only mode)"
    }
} else {
    Write-Host "flags copied to config; enable log-only or apply manually:"
    Write-Host "  log-only: ensure $destLog exists"
    Write-Host "  apply:    copy $flagApply -> $destApply"
}

Write-Host ""
Write-Host "Done. Launch FFX and check %TEMP%\ffx-hooks.log for RonsoMana install ok=1"
