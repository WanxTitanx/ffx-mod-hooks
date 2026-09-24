$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ tools were not found'}
$vcvars=Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\arena-browser-rt0'
New-Item -ItemType Directory -Force $obj|Out-Null
$source=Get-Content -Raw (Join-Path $PSScriptRoot 'dllmain.cpp')
$begin=$source.IndexOf('static void ArenaPlus_BuildUltraPreview()')
$end=$source.IndexOf('static uint32_t ArenaPlus_MixEntryCost()', $begin)
if($begin -lt 0 -or $end -le $begin){throw 'Production preview function not found'}
[IO.File]::WriteAllText((Join-Path $obj 'ArenaPreviewUnderTest.inc'),$source.Substring($begin,$end-$begin))
$test=Join-Path $PSScriptRoot 'tests\ArenaBrowserRt0.cpp'
$core=Join-Path $PSScriptRoot 'hooks\CustomMixUltraCore.cpp'
$exe=Join-Path $obj 'ArenaBrowserRt0.exe'
Push-Location $obj
try {
    $command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /I"{1}" "{2}" "{3}" /Fe"{4}" kernel32.lib user32.lib' -f $vcvars,$obj,$test,$core,$exe
    & $env:ComSpec /d /s /c $command
    if($LASTEXITCODE -ne 0){throw 'Arena browser compilation failed'}
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Arena browser checks failed'}
}finally{Pop-Location}
