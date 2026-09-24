# Arena+ compose restore RT0 harness (real temporary files, no game process).
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

$objDir = Join-Path $PSScriptRoot 'obj\arena-compose-restore-rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$testSource = Join-Path $PSScriptRoot 'tests\ArenaComposeRestoreRt0.cpp'
$productionSource = Join-Path $PSScriptRoot 'hooks\ArenaComposeRestore.cpp'
$exe = Join-Path $objDir 'ArenaComposeRestoreRt0.exe'
$pdb = Join-Path $objDir 'ArenaComposeRestoreRt0.pdb'

$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT "{1}" "{2}" /Fd"{3}" /Fe"{4}" kernel32.lib bcrypt.lib' -f `
    $vcvarsall, $testSource, $productionSource, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "Arena compose restore RT0 compilation failed with exit code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Arena compose restore RT0 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'ARENA COMPOSE RESTORE RT0: PASS'
