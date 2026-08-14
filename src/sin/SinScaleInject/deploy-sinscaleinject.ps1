# Deploy SinScaleInject as a clean standalone tool under the game modules\tools tree.
#
# Usage:
#   .\deploy-sinscaleinject.ps1
#   .\deploy-sinscaleinject.ps1 -GameRoot "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
#   .\deploy-sinscaleinject.ps1 -SkipPublish

param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster",
    [switch]$SkipPublish
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$proj = Join-Path $repoRoot "RuntimeTools\SinScaleInject\SinScaleInject.csproj"
$publishDir = Join-Path $repoRoot "work\_sinscaleinject_publish"
$modulesDir = Join-Path $GameRoot "modules"
$toolsDir = Join-Path $modulesDir "tools"
$deployDir = Join-Path $toolsDir "SinScaleInject"
$deployExe = Join-Path $deployDir "SinScaleInject.exe"

if (-not (Test-Path -LiteralPath $GameRoot)) {
    throw "GameRoot not found: $GameRoot"
}
if (-not (Test-Path -LiteralPath $modulesDir)) {
    throw "Game modules directory not found: $modulesDir"
}
if (-not (Test-Path -LiteralPath $proj)) {
    throw "Project not found: $proj"
}

$ffxProc = Get-Process FFX -ErrorAction SilentlyContinue
if ($ffxProc) {
    throw "FFX is running (PID $($ffxProc.Id)). Close the game before deploying SinScaleInject."
}

if (-not $SkipPublish) {
    if (Test-Path -LiteralPath $publishDir) {
        Remove-Item -LiteralPath $publishDir -Recurse -Force
    }

    Write-Host "Publishing SinScaleInject (Release)..." -ForegroundColor Cyan
    dotnet publish $proj -c Release -o $publishDir
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet publish failed (exit $LASTEXITCODE)"
    }
} elseif (-not (Test-Path -LiteralPath (Join-Path $publishDir "SinScaleInject.exe"))) {
    throw "Missing published tool at $publishDir. Run without -SkipPublish first."
}

$publishedExe = Join-Path $publishDir "SinScaleInject.exe"
if (-not (Test-Path -LiteralPath $publishedExe)) {
    throw "Publish output missing executable: $publishedExe"
}

New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

if (Test-Path -LiteralPath $deployDir) {
    $resolvedDeployDir = (Resolve-Path -LiteralPath $deployDir).Path
    $expectedDeployDir = [System.IO.Path]::GetFullPath($deployDir)
    if ($resolvedDeployDir -ne $expectedDeployDir) {
        throw "Resolved deploy dir mismatch. Expected $expectedDeployDir but got $resolvedDeployDir"
    }
    Remove-Item -LiteralPath $deployDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $deployDir | Out-Null
Get-ChildItem -LiteralPath $publishDir | Copy-Item -Destination $deployDir -Recurse -Force

if (-not (Test-Path -LiteralPath $deployExe)) {
    throw "Deploy failed: missing $deployExe"
}

# Deploy data (clean bins + rosters) so the tool is self-contained inside the game dir.
$dataDir = Join-Path $deployDir "data"
$cleanSrc = Join-Path $repoRoot "mods\Spira Reforge\sin-clean-bins"
$rostersSrc = Join-Path $repoRoot "mods\Spira Reforge\arena\spira-sin-area-rosters"

if (Test-Path -LiteralPath $cleanSrc) {
    $cleanDst = Join-Path $dataDir "sin-clean-bins"
    New-Item -ItemType Directory -Force -Path $cleanDst | Out-Null
    Copy-Item -Path (Join-Path $cleanSrc "*") -Destination $cleanDst -Recurse -Force
    Write-Host "Copied sin-clean-bins -> $cleanDst" -ForegroundColor Cyan
} else {
    Write-Host "WARN: sin-clean-bins not found at $cleanSrc — generate with --save-clean first" -ForegroundColor Yellow
}

if (Test-Path -LiteralPath $rostersSrc) {
    $rostersDst = Join-Path $dataDir "rosters"
    New-Item -ItemType Directory -Force -Path $rostersDst | Out-Null
    Get-ChildItem -LiteralPath $rostersSrc -Filter *.csv | Copy-Item -Destination $rostersDst -Force
    Write-Host "Copied rosters -> $rostersDst" -ForegroundColor Cyan
} else {
    Write-Host "WARN: rosters not found at $rostersSrc" -ForegroundColor Yellow
}

Write-Host "SinScaleInject deployed clean -> $deployDir" -ForegroundColor Green
Write-Host "Contents:" -ForegroundColor Cyan
Get-ChildItem -LiteralPath $deployDir | Select-Object Name, Length
