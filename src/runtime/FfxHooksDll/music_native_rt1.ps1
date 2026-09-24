param([Parameter(Mandatory=$true)][string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED') {
    throw 'Exact reviewed PE fixture required; do not launch the game'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools were not found' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$obj = Join-Path $PSScriptRoot 'obj\music-native-rt1'
$deps = Join-Path $PSScriptRoot 'vcpkg_installed\x86-windows-static'
New-Item -ItemType Directory -Force $obj | Out-Null
$source = Get-Content -Raw (Join-Path $PSScriptRoot 'tests\ArenaPositionNativeRt1.cpp')
$start = $source.IndexOf('static std::vector<unsigned char> Read(')
$end = $source.IndexOf('static bool Nested(', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Reviewed PE mapper fixture not found' }
[IO.File]::WriteAllText((Join-Path $obj 'MusicPeFixture.inc'), $source.Substring($start, $end-$start))
$test = Join-Path $PSScriptRoot 'tests\MusicNativeRt1.cpp'
$exe = Join-Path $obj 'MusicNativeRt1.exe'
Push-Location $obj
try {
    $command = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_HAVE_POLYHOOK /I"{4}" /external:I"{1}\include" /external:W0 "{2}" /Fe"{3}" /link /LIBPATH:"{1}\lib" PolyHook_2.lib Zydis.lib Zycore.lib asmjit.lib asmtk.lib kernel32.lib user32.lib' -f $vcvars,$deps,$test,$exe,$obj
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Native music harness build failed' }
    & $exe $ExecutablePath
    if ($LASTEXITCODE -ne 0) { throw 'Native music harness failed' }
} finally { Pop-Location }
