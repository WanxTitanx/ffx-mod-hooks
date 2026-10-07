# Jarvis-HOOK: isolated PE admission regression; never loads the game or installs a DLL.
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC x86 tools not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$out = Join-Path $PSScriptRoot 'obj/executable-startup-rt0'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$test = Join-Path $PSScriptRoot 'tests/ExecutableStartupGateRt0.cpp'
$core = Join-Path $PSScriptRoot 'hooks/F8RuntimeCore.cpp'
Push-Location $out
try {
    $compile = 'call "{0}" x86 >nul && cl.exe /nologo /EHsc /std:c++17 /WX /utf-8 /MT "{1}" "{2}" /Fe:ExecutableStartupGateRt0.exe' -f $vcvars, $test, $core
    & $env:ComSpec /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Startup admission compile failed' }
    & './ExecutableStartupGateRt0.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Startup admission regression failed' }
    $hashTest = Join-Path $PSScriptRoot 'tests/ExecutableStartupHashRt0.cpp'
    $hashCompile = 'call "{0}" x86 >nul && cl.exe /nologo /EHsc /std:c++17 /WX /utf-8 /MT "{1}" /Fe:ExecutableStartupHashRt0.exe' -f $vcvars, $hashTest
    & $env:ComSpec /c $hashCompile
    if ($LASTEXITCODE -ne 0) { throw 'Startup hash regression compile failed' }
    & './ExecutableStartupHashRt0.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Startup hash regression failed' }
} finally { Pop-Location }
