# S.I.N. RAM config RT0 harness. This compiles only the portable parser/serializer core.
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

$objDir = Join-Path $PSScriptRoot 'obj\sin-ram-config-core-rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$testSource = Join-Path $PSScriptRoot 'tests\SinRamConfigCoreRt0.cpp'
$coreSource = Join-Path $PSScriptRoot 'hooks\SinRamConfigCore.cpp'
$difficultySource = Join-Path $PSScriptRoot 'hooks\F7DifficultyCore.cpp'
$scalingSource = Join-Path $PSScriptRoot 'hooks\SinRamScalingCore.cpp'
$coordinatorSource = Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.cpp'
$coreHeader = Join-Path $PSScriptRoot 'hooks\SinRamConfigCore.h'
$exe = Join-Path $objDir 'SinRamConfigCoreRt0.exe'
$pdb = Join-Path $objDir 'SinRamConfigCoreRt0.pdb'
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT "{1}" "{2}" "{3}" "{4}" "{5}" /Fd"{6}" /Fe"{7}"' -f `
    $vcvarsall, $testSource, $coreSource, $difficultySource, $scalingSource, `
    $coordinatorSource, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "S.I.N. RAM config RT0 compilation failed with exit code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "S.I.N. RAM config RT0 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$portableSources = Get-Content -Raw -LiteralPath $coreHeader, $coreSource
$forbiddenPatterns = @(
    '#\s*include\s*[<"](?:string|vector|memory|filesystem|windows\.h)',
    '\b(?:malloc|calloc|realloc|free)\s*\(',
    '\b(?:new|delete)\b',
    '\b(?:fopen|CreateFile|ReadFile|WriteFile|MoveFile|GetEnvironmentVariable|getenv)\b',
    '\b(?:MH_|LoadLibrary|GetProcAddress|VirtualQuery|FFX\.exe|IDA)\b',
    '\b(?:strstr|strchr)\s*\('
)
foreach ($pattern in $forbiddenPatterns) {
    if ($portableSources -match $pattern) {
        throw "Portable S.I.N. config source denylist matched: $pattern"
    }
}

Write-Host 'S.I.N. RAM portable config core: PASS'
