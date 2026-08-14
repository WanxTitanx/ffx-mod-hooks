param(
    [string]$GameRoot = "D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster",
    [switch]$EnableLogOnly,
    [switch]$EnableApply,
    [switch]$EnableNativeSlots,
    [switch]$EnableP16,
    [switch]$EnableTeach,
    [switch]$EnableScanDark,
    [switch]$SkipDll,
    [switch]$KeepNovaFlag
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
    if (-not (Test-Path $releaseDll)) {
        throw "Build first: RuntimeTools\FfxHooksDll\build_hooks.ps1 -WithPolyHook -Release"
    }
    if (Test-Path $destDll) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $backup = Join-Path $modulesDir "ffx-hooks.dll.backup-nul-ward-$stamp"
        Copy-Item -LiteralPath $destDll -Destination $backup -Force
        Write-Host "backup: $backup"
    }
    Copy-Item -LiteralPath $releaseDll -Destination $destDll -Force
    $hash = (Get-FileHash -LiteralPath $destDll -Algorithm SHA256).Hash.Substring(0, 16)
    Write-Host "deployed dll: $destDll"
    Write-Host "sha256-prefix: $hash"
}

$destLog = Join-Path $configDir "nul_ward.flag"
$destApply = Join-Path $configDir "nul_ward_apply.flag"
$destLogSrc = Join-Path $here "config\nul_ward.flag"
$destApplySrc = Join-Path $here "config\nul_ward_apply.flag"

if ($EnableApply) {
    Copy-Item -LiteralPath $destLogSrc -Destination $destLog -Force
    Copy-Item -LiteralPath $destApplySrc -Destination $destApply -Force
    Write-Host "flags: nul_ward.flag + nul_ward_apply.flag (apply mode)"

    $novaFlag = Join-Path $configDir "nova_super_damage.flag"
    if ((Test-Path $novaFlag) -and -not $KeepNovaFlag) {
        $novaBak = "$novaFlag.disabled-for-nul-ward"
        Move-Item -LiteralPath $novaFlag -Destination $novaBak -Force
        Write-Host "WARN: moved nova_super_damage.flag aside (writeback patch conflict with NulWard)"
    } elseif ((Test-Path $novaFlag) -and $KeepNovaFlag) {
        Write-Host "WARN: nova_super_damage.flag kept; NulWard writeback may fail"
    }
} elseif ($EnableLogOnly) {
    Copy-Item -LiteralPath $destLogSrc -Destination $destLog -Force
    if (Test-Path $destApply) {
        Remove-Item -LiteralPath $destApply -Force
        Write-Host "removed nul_ward_apply.flag (log-only)"
    }
    Write-Host "flag: nul_ward.flag (log-only)"
} else {
    Copy-Item -LiteralPath $destLogSrc -Destination $destLog -Force
    Write-Host "flag: nul_ward.flag (add nul_ward_apply.flag manually for apply)"
}

$optionalFlags = @(
    @{ Name = "nul_ward_native_slots.flag"; Switch = $EnableNativeSlots },
    @{ Name = "nul_ward_p16.flag"; Switch = $EnableP16 },
    @{ Name = "nul_ward_teach.flag"; Switch = $EnableTeach },
    @{ Name = "nul_ward_teach_grant.flag"; Switch = $EnableTeach },
    @{ Name = "element_scan_dark.flag"; Switch = $EnableScanDark }
)
foreach ($f in $optionalFlags) {
    $src = Join-Path $here "config\$($f.Name)"
    $dst = Join-Path $configDir $f.Name
    if ($f.Switch -and (Test-Path $src)) {
        Copy-Item -LiteralPath $src -Destination $dst -Force
        Write-Host "optional flag: $($f.Name)"
    }
}

Write-Host ""
Write-Host "Done. Launch FFX and check TEMP\ffx-hooks.log for NulWard install ok=1 apply=1"
Write-Host "RT2 gate: RuntimeTools\NulWardLab\run_rt2_gate.ps1"
