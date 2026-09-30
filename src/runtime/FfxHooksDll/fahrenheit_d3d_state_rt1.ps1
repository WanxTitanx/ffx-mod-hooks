param([switch]$WithSwapChain)
# Swapchain tests require a desktop session; DXGI rejects Session 0 (SSH/services).
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'work\fahrenheit-d3d-state-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 is required.'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$exe=Join-Path $out 'fahrenheit-d3d-state-rt1.exe'
$rsp=Join-Path $out 'build.rsp'
$argsText='/nologo /std:c++17 /EHsc /MT /O2 /W4 "{0}\tests\FahrenheitD3DStateRt1.cpp" /Fo"{1}\\" /Fe:"{2}" /link d3d11.lib dxgi.lib user32.lib' -f $PSScriptRoot,$out,$exe
[IO.File]::WriteAllText($rsp,$argsText,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw "D3D state harness compile failed: $LASTEXITCODE"}
if($WithSwapChain){& $exe --swapchain}else{& $exe}
if($LASTEXITCODE){throw "D3D state harness failed: $LASTEXITCODE"}
