# S.I.N. transition publication RT0/RT1 harness. It compiles only the portable
# value-publication core as x86 and runs bounded concurrency pressure.
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

$objDir = Join-Path $PSScriptRoot 'obj\sin-transition-publication-rt1'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$testSource = Join-Path $PSScriptRoot 'tests\SinTransitionPublicationRt1.cpp'
$coreSource = Join-Path $PSScriptRoot 'hooks\SinTransitionPublication.cpp'
$exe = Join-Path $objDir 'SinTransitionPublicationRt1.exe'
$pdb = Join-Path $objDir 'SinTransitionPublicationRt1.pdb'
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT "{1}" "{2}" /Fd"{3}" /Fe"{4}"' -f `
    $vcvarsall, $testSource, $coreSource, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "S.I.N. transition publication RT0/RT1 compilation failed with exit code $LASTEXITCODE" }

    $binary = [System.IO.File]::ReadAllBytes($exe)
    if ($binary.Length -lt 0x40) { throw 'S.I.N. transition publication output is not a valid PE image' }
    $peOffset = [BitConverter]::ToInt32($binary, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length) { throw 'S.I.N. transition publication PE header is out of bounds' }
    $machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
    if ($machine -ne 0x014C) { throw ('S.I.N. transition publication expected I386 machine 0x014C, got 0x{0:X4}' -f $machine) }

    & $exe
    if ($LASTEXITCODE -ne 0) { throw "S.I.N. transition publication RT0/RT1 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'S.I.N. transition publication portable x86 core: PASS (RT0 contract + RT1 concurrency)'
