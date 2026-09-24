param([Parameter(Mandatory=$true)][string]$ExecutablePath)
$ErrorActionPreference='Stop'
if((Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'){throw 'Exact reviewed executable fixture required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ tools were not found'}
$vcvars=Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\fmv-speed-native-rt1'
New-Item -ItemType Directory -Force $obj|Out-Null
$mapper=Get-Content -Raw (Join-Path $PSScriptRoot 'tests\ArenaPositionNativeRt1.cpp')
$begin=$mapper.IndexOf('static std::vector<unsigned char> Read(');$end=$mapper.IndexOf('static bool Nested(', $begin)
if($begin -lt 0 -or $end -le $begin){throw 'Reviewed PE mapper not found'}
[IO.File]::WriteAllText((Join-Path $obj 'MusicPeFixture.inc'),$mapper.Substring($begin,$end-$begin))
$sources=@('tests\FmvSpeedNativeRt1.cpp','hooks\FmvSpeedHook.cpp','hooks\F8RuntimeCore.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$exe=Join-Path $obj 'FmvSpeedNativeRt1.exe'
Push-Location $obj
try {
    $command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /I"{3}" {1} /Fe"{2}" kernel32.lib user32.lib' -f $vcvars,($sources -join ' '),$exe,$obj
    & $env:ComSpec /d /c $command
    if($LASTEXITCODE -ne 0){throw 'Native language harness build failed'}
    & $exe $ExecutablePath
    if($LASTEXITCODE -ne 0){throw 'Native language harness failed'}
} finally {Pop-Location}
