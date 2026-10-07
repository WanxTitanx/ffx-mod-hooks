$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'tests/SinProducerRt0.cpp'
$output = Join-Path $PSScriptRoot 'bin/SinProducerRt0.exe'
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC installation not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars32.bat'
& cmd /c "`"$vcvars`" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX `"$source`" /Fe:`"$output`" /Fo:`"$output.obj`" && `"$output`""
if ($LASTEXITCODE) { throw "SIN producer RT0 failed: $LASTEXITCODE" }
