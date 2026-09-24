$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build_hooks.ps1') -WithPolyHook -Release
if($LASTEXITCODE -ne 0){throw 'Main Workshop DLL build failed'}
