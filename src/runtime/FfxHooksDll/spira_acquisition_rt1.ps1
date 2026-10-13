param([Parameter(Mandatory=$true)][string]$ExecutablePath,[Parameter(Mandatory=$true)][string]$KaizouPath,[Parameter(Mandatory=$true)][string]$MonsterRoot)
. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutablePath).Hash -ne (Get-FfxTestExecutableHash)){throw 'Exact PE required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe';$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat';$obj=Join-Path $PSScriptRoot 'obj\spira-acquisition-rt1';New-Item -ItemType Directory -Force $obj | Out-Null
$source=Get-Content (Join-Path $PSScriptRoot 'hooks\CaptureCascadeBattle.cpp') -Raw
$begin=$source.IndexOf('bool Asset(');$end=$source.IndexOf('struct Stream',$begin)
if($begin -lt 0 -or $end -lt $begin){throw 'Actual clone gate not found'}
$source.Substring($begin,$end-$begin) | Set-Content "$obj\CascadeAsset.inc" -Encoding UTF8
$exe=Join-Path $obj 'SpiraAcquisitionRt1.exe'
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /I"{1}" "{2}\tests\SpiraAcquisitionRt1.cpp" "{2}\hooks\CaptureCascadeBattleCore.cpp" "{2}\hooks\CaptureCascadeCore.cpp" /Fe"{3}" user32.lib' -f $vcvars,$obj,$PSScriptRoot,$exe
Push-Location $obj
try{& $env:ComSpec /d /s /c $compile;if($LASTEXITCODE){throw 'Acquisition native compile failed'}
    & $exe $ExecutablePath $KaizouPath $MonsterRoot;if($LASTEXITCODE){throw 'Acquisition native checks failed'}
}finally{Pop-Location}
