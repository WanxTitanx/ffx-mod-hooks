$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools were not found' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$obj = Join-Path $PSScriptRoot 'obj\music-hook-rt1'
$deps = Join-Path $PSScriptRoot 'vcpkg_installed\x86-windows-static'
New-Item -ItemType Directory -Force $obj | Out-Null
$test = Join-Path $PSScriptRoot 'tests\MusicHookRt1.cpp'
$exe = Join-Path $obj 'MusicHookRt1.exe'
Push-Location $obj
try {
    $command = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_HAVE_POLYHOOK /external:I"{1}\include" /external:W0 "{2}" /Fe"{3}" /link /LIBPATH:"{1}\lib" PolyHook_2.lib Zydis.lib Zycore.lib asmjit.lib asmtk.lib kernel32.lib user32.lib' -f $vcvars,$deps,$test,$exe
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Music callback harness build failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Music callback harness failed' }
} finally { Pop-Location }
