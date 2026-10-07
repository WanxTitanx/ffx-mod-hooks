param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
if(-not $FixtureRoot){$FixtureRoot=Join-Path $repo 'native-fixtures'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 tools required'}
$vcvars=Join-Path $vs.Trim() 'VC/Auxiliary/Build/vcvarsall.bat'
$obj=Join-Path $here 'obj/executable-startup-hash-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
Push-Location $obj
try {
    $source=Join-Path $here 'tests/ExecutableStartupHashRt1.cpp'
    $compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT "{1}" /Fe:ExecutableStartupHashRt1.exe bcrypt.lib' -f $vcvars,$source
    & $env:ComSpec /d /s /c $compile
    if($LASTEXITCODE -ne 0){throw 'Startup file hash test compilation failed'}
    $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $data | Out-Null
    & './ExecutableStartupHashRt1.exe' (Join-Path $FixtureRoot 'FFX.exe') $data
    if($LASTEXITCODE -ne 0){throw 'Startup file hash regression failed'}
} finally {Pop-Location}
