$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
& (Join-Path $PSScriptRoot 'sin_ai_native_rt1.ps1') -ExecutablePath (Join-Path $repo 'native-fixtures\FFX.exe') -PackPath (Join-Path $repo 'sin-fixtures\_sin-ai-v1.bin') -CommandsPath (Join-Path $repo 'sin-fixtures\monmagic2.bin') -MonstersPath (Join-Path $repo 'sin-fixtures\mon')
if($LASTEXITCODE -ne 0){throw 'S.I.N. package RT1 failed'}
