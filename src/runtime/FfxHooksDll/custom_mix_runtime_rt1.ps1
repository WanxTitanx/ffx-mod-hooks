# CustomMix Ultra production-runtime RT0/RT1 harness (x86, no game process).
$ErrorActionPreference = 'Stop'

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $vswhere) { throw 'vswhere.exe was not found' }
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) { throw 'Visual Studio C++ tools were not found' }
$vcvarsall = Join-Path $installationPath.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $vcvarsall)) { throw "vcvarsall.bat was not found: $vcvarsall" }

$objDir = Join-Path $PSScriptRoot 'obj\custommix-runtime-rt1'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'tests\CustomMixRuntimeRt1.cpp'),
    (Join-Path $PSScriptRoot 'hooks\CustomMixRuntime.cpp'),
    (Join-Path $PSScriptRoot 'hooks\ArenaBattleProgram.cpp'),
    (Join-Path $PSScriptRoot 'hooks\CustomMixUltraCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\CustomMixWindowsAdapter.cpp'),
    (Join-Path $PSScriptRoot 'hooks\SharedBattleRuntime.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F7UiCore.cpp')
)
$sourceArguments = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$exe = Join-Path $objDir 'CustomMixRuntimeRt1.exe'
$pdb = Join-Path $objDir 'CustomMixRuntimeRt1.pdb'
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fd"{2}" /Fe"{3}" kernel32.lib user32.lib bcrypt.lib' -f `
    $vcvarsall, $sourceArguments, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "CustomMix runtime RT0/RT1 compilation failed with exit code $LASTEXITCODE" }

    $binary = [System.IO.File]::ReadAllBytes($exe)
    if ($binary.Length -lt 0x40) { throw 'CustomMix runtime output is not a valid PE image' }
    $peOffset = [BitConverter]::ToInt32($binary, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length) { throw 'CustomMix runtime PE header is out of bounds' }
    $machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
    if ($machine -ne 0x014C) { throw ('CustomMix runtime expected I386 machine 0x014C, got 0x{0:X4}' -f $machine) }

    & $exe
    if ($LASTEXITCODE -ne 0) { throw "CustomMix runtime executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host ('CustomMix Ultra production runtime x86: PASS (RT0 contracts + RT1 composition; COFF-I386 0x{0:X4})' -f $machine)
