# F8 runtime governance RT0 harness (x86, isolated from the DLL project).
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

$objDir = Join-Path $PSScriptRoot 'obj\rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'tests\F8RuntimeRt0.cpp'),
    (Join-Path $PSScriptRoot 'shared\Config.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F8FlagCatalog.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F8RuntimeCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MaechenCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MaechenHook.cpp')
)
$sourceArguments = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$exe = Join-Path $objDir 'F8RuntimeRt0.exe'
$pdb = Join-Path $objDir 'F8RuntimeRt0.pdb'

$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fd"{2}" /Fe"{3}" kernel32.lib user32.lib' -f `
    $vcvarsall, $sourceArguments, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "F8 RT0 compilation failed with exit code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "F8 RT0 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'F8 RUNTIME RT0: PASS'
