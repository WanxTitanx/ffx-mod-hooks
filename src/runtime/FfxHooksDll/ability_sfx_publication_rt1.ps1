param([string]$DependencyRoot="$PSScriptRoot/vcpkg_installed/x86-windows-static")
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'work/ability-sfx-publication-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 is required.'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$dep=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DependencyRoot)
$exe=Join-Path $out 'ability-sfx-publication-rt1.exe'
$rsp=Join-Path $out 'build.rsp'
$arguments='/nologo /std:c++17 /EHsc /MT /O2 /W4 /DFFXHOOKS_COEXISTENCE /I"{0}/include" "{1}/tests/AbilitySfxPublicationRt1.cpp" /Fo"{2}\\" /Fe:"{3}" /link /LIBPATH:"{0}/lib" PolyHook_2.lib Zydis.lib Zycore.lib asmjit.lib asmtk.lib kernel32.lib user32.lib' -f $dep,$PSScriptRoot,$out,$exe
[IO.File]::WriteAllText($rsp,$arguments,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw "Ability SFX publication harness compile failed: $LASTEXITCODE"}
& $exe
if($LASTEXITCODE){throw "Ability SFX publication harness failed: $LASTEXITCODE"}
