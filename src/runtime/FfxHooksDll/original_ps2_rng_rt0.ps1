$ErrorActionPreference = 'Stop'
$test = Join-Path $PSScriptRoot 'tests/OriginalPs2RngCoreRt0.cpp'
$core = Join-Path $PSScriptRoot 'hooks/OriginalPs2RngCore.cpp'
$output = Join-Path $PSScriptRoot 'bin/OriginalPs2RngCoreRt0.exe'
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC installation not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars32.bat'
$work = Join-Path (Split-Path $output) 'original-ps2-rng-rt0'
New-Item -ItemType Directory -Force $work | Out-Null
$temp = Join-Path $work 'temp'
New-Item -ItemType Directory -Force $temp | Out-Null
Push-Location $work
try {
    & cmd /c "`"$vcvars`" >nul && set `"TEMP=$temp`" && set `"TMP=$temp`" && cl /nologo /std:c++17 /EHsc /W4 /WX `"$test`" `"$core`" /Fe:`"$output`" && `"$output`""
    if ($LASTEXITCODE) { throw "Original PS2 RNG RT0 failed: $LASTEXITCODE" }
} finally { Pop-Location }
