param([Parameter(Mandatory=$true)][string]$TextFixtureRoot)
# Jarvis-HOOK: private text/font fixtures; the game entrypoint is never invoked.
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
& (Join-Path $PSScriptRoot 'text_languages_rt1.ps1') -ExecutablePath (Join-Path $repo 'native-fixtures/FFX.exe') -PackageDirectory (Join-Path $TextFixtureRoot 'pt-BR') -ReferenceDirectory (Join-Path $TextFixtureRoot 'reference')
if($LASTEXITCODE){throw 'Fahrenheit text/resource integration failed'}
