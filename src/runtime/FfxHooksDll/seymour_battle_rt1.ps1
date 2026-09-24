# Seymour battle-roster and shared InitScene isolated harness (x86, no game process).
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

$objDir = Join-Path $PSScriptRoot 'obj\seymour-rt1'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'tests\SeymourBattleRt1.cpp'),
    (Join-Path $PSScriptRoot 'hooks\SharedBattleRuntime.cpp'),
    (Join-Path $PSScriptRoot 'hooks\SeymourBattleCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.cpp')
)
$sourceArguments = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$exe = Join-Path $objDir 'SeymourBattleRt1.exe'
$pdb = Join-Path $objDir 'SeymourBattleRt1.pdb'

$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fd"{2}" /Fe"{3}"' -f `
    $vcvarsall, $sourceArguments, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "Seymour battle RT1 compilation failed with exit code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Seymour battle RT1 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'SEYMOUR BATTLE RT1: PASS'
