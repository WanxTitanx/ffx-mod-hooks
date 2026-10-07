param([Parameter(Mandatory=$true)][string]$FixtureDirectory,
      [Parameter(Mandatory=$true)][string]$ExecutablePath)
$ErrorActionPreference='Stop'
if((Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash.ToLowerInvariant() -ne
   '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'){throw 'Reviewed Steam host executable required'}
$ids=@(790,791,802,803,807,815,829,837,845,852,864)
$fixtures=$ids|ForEach-Object {Join-Path $FixtureDirectory ('magic_{0:D4}.dll' -f $_)}
foreach($file in $fixtures){if(!(Test-Path -LiteralPath $file)){throw "Missing owned fixture: $file"}}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC installation not found'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj/sin-throw-visual-native-rt1'
$temp=Join-Path $obj 'temp'
New-Item -ItemType Directory -Force $temp|Out-Null
$mapper=Get-Content -Raw (Join-Path $PSScriptRoot 'tests/ArenaPositionNativeRt1.cpp')
$begin=$mapper.IndexOf('static std::vector<unsigned char> Read(');$end=$mapper.IndexOf('static bool Nested(',$begin)
if($begin -lt 0 -or $end -le $begin){throw 'Reviewed private PE mapper not found'}
$body=$mapper.Substring($begin,$end-$begin)
if(!$body.Contains('nt->OptionalHeader.SizeOfImage < 0x00D2C264u')){throw 'Mapper size guard changed'}
# This harness maps the reviewed visual family, not the much larger FFX.exe.
$body=$body.Replace('nt->OptionalHeader.SizeOfImage < 0x00D2C264u','nt->OptionalHeader.SizeOfImage != 0x001D9000u')
[IO.File]::WriteAllText((Join-Path $obj 'MusicPeFixture.inc'),$body)
$source=Join-Path $PSScriptRoot 'tests/SinThrowVisualNativeRt1.cpp'
$exe=Join-Path $obj 'SinThrowVisualNativeRt1.exe'
Push-Location $obj
try {
    $command='call "{0}" x86 >nul && set "TEMP={4}" && set "TMP={4}" && cl /nologo /EHsc /std:c++17 /W4 /WX /MT /I"{1}" "{2}" /Fe"{3}"' -f $vcvars,$obj,$source,$exe,$temp
    & $env:ComSpec /c $command
    if($LASTEXITCODE){throw 'Native visual harness compile failed'}
    & $exe $ExecutablePath @fixtures
    if($LASTEXITCODE){throw 'Native visual harness failed'}
} finally {Pop-Location}
