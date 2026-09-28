# Jarvis-HOOK: compile the complete candidate, never deploy it or start the game.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build_hooks.ps1') -WithPolyHook -Release
$candidate = Join-Path $PSScriptRoot 'bin\Release\ffx-hooks.dll'
if (-not (Test-Path -LiteralPath $candidate)) { throw 'Candidate DLL was not produced' }
$hash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()
$bytes = (Get-Item -LiteralPath $candidate).Length
Write-Output ('MOD007_RELEASE_CANDIDATE bytes=' + $bytes + ' sha256=' + $hash)
