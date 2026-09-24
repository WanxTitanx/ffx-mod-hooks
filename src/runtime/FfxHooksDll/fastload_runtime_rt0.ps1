# Fastload RT0 contracts and RT1 native title lifecycle (isolated x86 process).
$ErrorActionPreference = 'Stop'

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $vswhere) {
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) { $vswhere = $vswhereCommand.Source }
}
if (-not $vswhere) { throw 'vswhere.exe was not found' }

$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) { throw 'Visual Studio C++ tools were not found' }
$vcvarsall = Join-Path $installationPath.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $vcvarsall)) { throw "vcvarsall.bat was not found: $vcvarsall" }

$objDir = Join-Path $PSScriptRoot 'obj\fastload-rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'tests\FastloadRuntimeRt0.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F8RuntimeCore.cpp')
)
$sourceArguments = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$exe = Join-Path $objDir 'FastloadRuntimeRt0.exe'
$pdb = Join-Path $objDir 'FastloadRuntimeRt0.pdb'
$fixture = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path 'native-fixtures\FFX.exe'
if (-not (Test-Path -LiteralPath $fixture) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $fixture).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED') { throw 'Exact supported FFX.exe fixture missing; do not launch the game' }

$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fd"{2}" /Fe"{3}"' -f `
    $vcvarsall, $sourceArguments, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "Fastload runtime RT0 compilation failed with exit code $LASTEXITCODE" }
    & $exe $fixture
    if ($LASTEXITCODE -ne 0) { throw "Fastload runtime RT0 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'FASTLOAD RUNTIME RT0 + NATIVE TITLE RT1: PASS'
