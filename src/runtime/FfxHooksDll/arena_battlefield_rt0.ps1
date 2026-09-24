param([Parameter(Mandatory=$true)][string]$CarrierPath)
$ErrorActionPreference = 'Stop'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $CarrierPath).Hash -ne 'DDF8D89343195D3D014630C296A9583EE556EFA839918435802249FE148594D0') {
    throw 'Exact reviewed carrier fixture required'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj = Join-Path $PSScriptRoot 'obj\arena-battlefield-rt0'
New-Item -ItemType Directory -Path $obj -Force | Out-Null
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT "{1}\tests\ArenaBattlefieldRt0.cpp" "{1}\hooks\CustomMixUltraCore.cpp" /Fe:ArenaBattlefieldRt0.exe' -f $vcvars,$PSScriptRoot
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $compile
    if($LASTEXITCODE -ne 0){throw 'Arena scenery core compile failed'}
    & .\ArenaBattlefieldRt0.exe $CarrierPath
    if($LASTEXITCODE -ne 0){throw 'Arena scenery core checks failed'}
} finally {Pop-Location}
