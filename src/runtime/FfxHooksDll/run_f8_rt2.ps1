[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet(
        'permanent_sensor','seymour_battle_roster','speed_hack','entire_party_earns_ap',
        'invincible_party','invincible_enemies','always_overdrive','always_critical',
        'damage_99999','always_rare_drop','ap_100x','gil_100x',
        'arena_plus_compose_f7','dialog_skip'
    )]
    [string]$Case,
    [Parameter(Mandatory)]
    [ValidateSet('Preflight','Verify')]
    [string]$Phase,
    [Parameter(Mandatory)][switch]$DisposableSaveConfirmed,
    [Parameter(Mandatory)][switch]$EditorClosedConfirmed,
    [switch]$ObservedApplied,
    [switch]$ObservedRestored,
    [switch]$RestoreConfigSnapshot,
    [string]$SeymourEvidencePath,
    [string]$EvidenceDirectory,
    [string]$EvidenceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$modulePath = Join-Path $PSScriptRoot 'run_f8_rt2_lib.psm1'
if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "F8 RT2 protocol helper is missing: $modulePath"
}

Import-Module -Name $modulePath -Force -ErrorAction Stop
try {
    $invoke = @{
        Case = $Case
        Phase = $Phase
        DisposableSaveConfirmed = $DisposableSaveConfirmed
        EditorClosedConfirmed = $EditorClosedConfirmed
        ObservedApplied = $ObservedApplied
        ObservedRestored = $ObservedRestored
        RestoreConfigSnapshot = $RestoreConfigSnapshot
        SeymourEvidencePath = $SeymourEvidencePath
        EvidenceDirectory = $EvidenceDirectory
        EvidenceRoot = $EvidenceRoot
        ScriptRoot = $PSScriptRoot
    }
    $result = Invoke-F8Rt2Protocol @invoke

    Write-Host "F8 manual protocol phase: $($result.Phase)"
    Write-Host "Case: $($result.Case)"
    Write-Host "Evidence directory: $($result.EvidenceDirectory)"
    Write-Host "Snapshot: $($result.SnapshotPath)"
    foreach ($resolution in @($result.Resolutions)) {
        Write-Host ("Resolver {0} ({1}) = {2} via {3}" -f `
            $resolution.Case,
            $resolution.Canonical,
            $(if ($resolution.Value) { 'ON' } else { 'OFF' }),
            $resolution.Source)
    }

    if ($Phase -ceq 'Preflight') {
        Write-Host 'Manual steps for the selected case only:'
        foreach ($step in @($result.ManualSteps)) { Write-Host "  - $step" }
        Write-Host 'Preflight complete. The script did not launch/stop FFX or the editor, deploy/copy a DLL, sleep for boot, or mutate the INI.'
        Write-Host 'This is a manual protocol boundary, not RT2 or Production evidence.'
    } else {
        Write-Host "Verify evidence: $($result.VerifyEvidenceDirectory)"
        Write-Host "Vanilla-restoration verdict (hashed/snapshotted scope only): $($result.FinalVerdict)"
        Write-Host 'Disposable-save confirmation is human attestation, not technical proof of whole-game state.'
    }

    Write-Output $result
} finally {
    Remove-Module -Name 'run_f8_rt2_lib' -Force -ErrorAction SilentlyContinue
}
