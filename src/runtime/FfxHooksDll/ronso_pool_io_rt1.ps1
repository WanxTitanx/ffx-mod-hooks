$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio C++ tools missing' }
$vcvars = Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj = Join-Path $here 'obj\ronso-pool-io-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh = Join-Path $here 'third_party\minhook'
$cSources = @('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object { '"{0}"' -f (Join-Path "$mh\src" $_) }
$compileC = 'call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($cSources -join ' ')
$exe = Join-Path $obj 'RonsoPoolIoRt1.exe'
$fixture = Join-Path (Resolve-Path (Join-Path $here '..\..\..')).Path 'native-fixtures\FFX.exe'
if (-not (Test-Path -LiteralPath $fixture) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $fixture).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED') { throw 'Exact supported FFX.exe fixture missing; do not launch the game' }
$crtFixture = Join-Path (Split-Path $fixture) 'msvcr110.dll'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $crtFixture).Hash -ne 'B30160E759115E24425B9BCDF606EF6EBCE4657487525EDE7F1AC40B90FF7E49') { throw 'Exact game CRT fixture required' }
Copy-Item -LiteralPath $crtFixture -Destination (Join-Path $obj 'msvcr110.dll') -Force
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\include" "{2}\tests\RonsoPoolIoRt1.cpp" "{2}\hooks\MinHookBatchCoordinator.cpp" "{2}\hooks\F8RuntimeCore.cpp" "{2}\hooks\RonsoPoolCore.cpp" "{2}\hooks\RonsoPoolSave.cpp" "{2}\hooks\RonsoPoolStore.cpp" "{2}\hooks\RonsoPoolRuntime.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{3}" bcrypt.lib' -f $vcvars,$mh,$here,$exe
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $compileC
    if ($LASTEXITCODE -ne 0) { throw 'MinHook fixture compilation failed' }
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Nova RT1 compilation failed' }
    $save = Join-Path (Split-Path $fixture) 'ffx_000'
    $root = Join-Path $obj 'owned-test-data'
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    & $exe $fixture $save $root on
    if ($LASTEXITCODE -ne 0) { throw 'Ronso ON IO fixture failed' }
    & $exe $fixture $save $root off
    if ($LASTEXITCODE -ne 0) { throw 'Nova RT1 failed' }
} finally { Pop-Location }
Write-Host 'RONSO POOL IO RT1: PASS'
