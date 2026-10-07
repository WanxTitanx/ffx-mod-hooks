param([Parameter(Mandatory=$true)][string]$ExecutablePath)
$ErrorActionPreference='Stop'
$hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash.ToLowerInvariant()
if($hash -notin @('78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced','0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d')){throw 'Exact reviewed executable fixture required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC installation not found'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj/original-ps2-rng-native-math-rt1'
New-Item -ItemType Directory -Force $obj|Out-Null
$temp=Join-Path $obj 'temp'
New-Item -ItemType Directory -Force $temp|Out-Null
$mapper=Get-Content -Raw (Join-Path $PSScriptRoot 'tests/ArenaPositionNativeRt1.cpp')
$begin=$mapper.IndexOf('static std::vector<unsigned char> Read(');$end=$mapper.IndexOf('static bool Nested(',$begin)
if($begin -lt 0 -or $end -le $begin){throw 'Reviewed PE mapper not found'}
[IO.File]::WriteAllText((Join-Path $obj 'MusicPeFixture.inc'),$mapper.Substring($begin,$end-$begin))
$sources=@('tests/OriginalPs2RngNativeMathRt1.cpp','hooks/OriginalPs2RngCore.cpp')|ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$exe=Join-Path $obj 'OriginalPs2RngNativeMathRt1.exe'
Push-Location $obj
try {
    $command='call "{0}" x86 >nul && set "TEMP={4}" && set "TMP={4}" && cl /nologo /EHsc /std:c++17 /W4 /WX /MT /I"{1}" {2} /Fe"{3}"' -f $vcvars,$obj,($sources -join ' '),$exe,$temp
    & $env:ComSpec /c $command
    if($LASTEXITCODE){throw 'Native RNG math compile failed'}
    & $exe $ExecutablePath
    if($LASTEXITCODE){throw 'Native RNG math harness failed'}
} finally {Pop-Location}
