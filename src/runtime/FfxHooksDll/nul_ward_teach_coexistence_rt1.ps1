$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'work/nul-ward-teach-coexistence-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 is required.'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$exe=Join-Path $out 'nul-ward-teach-coexistence-rt1.exe'
$rsp=Join-Path $out 'build.rsp'
$arguments='/nologo /std:c++17 /EHsc /MT /O2 /W3 "{0}/tests/NulWardTeachCoexistenceRt1.cpp" /Fo"{1}\\" /Fe:"{2}" /link kernel32.lib' -f $PSScriptRoot,$out,$exe
[IO.File]::WriteAllText($rsp,$arguments,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw "Legacy coexistence harness compile failed: $LASTEXITCODE"}
& $exe
if($LASTEXITCODE){throw "Legacy coexistence harness failed: $LASTEXITCODE"}
