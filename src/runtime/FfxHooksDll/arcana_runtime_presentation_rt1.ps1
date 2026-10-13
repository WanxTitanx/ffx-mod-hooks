# Jarvis-HOOK: isolated native save/menu fixtures, without launching FFX.
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
& (Join-Path $PSScriptRoot 'arcana_rt1.ps1') `
    -GameExecutable (Join-Path $repo 'native-fixtures\FFX.exe') `
    -NativeSaveFixture (Join-Path $repo 'native-fixtures\ffx_000') `
    -AssetsRoot (Join-Path $repo 'arcana-art-fixtures') -Cases @('runtime','ui','assets')
if($LASTEXITCODE -ne 0){throw 'Arcana runtime/presentation fixtures failed'}
