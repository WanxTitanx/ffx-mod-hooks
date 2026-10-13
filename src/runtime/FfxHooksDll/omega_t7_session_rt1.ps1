param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe';$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat';$obj=Join-Path $PSScriptRoot 'obj\omega-t7-session-rt1';New-Item -ItemType Directory -Force $obj | Out-Null
$exe=Join-Path $obj 'OmegaT7SessionRt1.exe'
$sources=@('tests\OmegaT7SessionRt1.cpp','hooks\OmegaT7Session.cpp','hooks\OmegaT7Store.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_TARGET_STEAM_20261001 {1} /Fe"{2}" bcrypt.lib user32.lib' -f $vcvars,($sources -join ' '),$exe
Push-Location $obj
try{& $env:ComSpec /d /s /c $compile;if($LASTEXITCODE){throw 'Omega session compile failed'}
    & $exe $FixtureRoot;if($LASTEXITCODE){throw 'Omega session validation failed'}
}finally{Pop-Location}
