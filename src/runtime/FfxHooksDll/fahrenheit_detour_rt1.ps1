param(
    [Parameter(Mandatory=$true)][string]$DependencyRoot,
    [string]$ProviderDirectory="$PSScriptRoot\work\fahrenheit-minhook-rt1"
)
$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'work\fahrenheit-detour-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 is required.'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$dep=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DependencyRoot)
$exe=Join-Path $out 'fahrenheit-detour-rt1.exe'
$rsp=Join-Path $out 'build.rsp'
$argsText='/nologo /std:c++17 /EHsc /MT /O2 /W4 /DFFXHOOKS_COEXISTENCE /I"{0}\include" "{1}\tests\FahrenheitDetourRt1.cpp" /Fo"{2}\\" /Fe:"{3}" /link /LIBPATH:"{0}\lib" PolyHook_2.lib Zydis.lib Zycore.lib asmjit.lib asmtk.lib kernel32.lib user32.lib' -f $dep,$PSScriptRoot,$out,$exe
[IO.File]::WriteAllText($rsp,$argsText,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw "Detour harness compile failed: $LASTEXITCODE"}
& $exe "$ProviderDirectory\guarded.dll" "$ProviderDirectory\reference.dll"
if($LASTEXITCODE){throw "Detour harness failed: $LASTEXITCODE"}
