param([Parameter(Mandatory=$true)][string]$ExecutablePath,
      [Parameter(Mandatory=$true)][string]$CarrierPath,
      [Parameter(Mandatory=$true)][string]$ProfilesPath)
# Compatibility entrypoint: normal views now provide real eight-slot native arrays.
# The replacement harness includes the original PE mapper and MinHook activation test,
# then validates worker/formation/position consumers on the complete normal view.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'arena_program_native_rt1.ps1') -ExecutablePath $ExecutablePath -CarrierPath $CarrierPath -ProfilesPath $ProfilesPath
if ($LASTEXITCODE -ne 0) { throw 'Normal arena native validation failed' }
