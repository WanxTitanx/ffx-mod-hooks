param([Parameter(Mandatory=$true)][string]$ExecutablePath,[Parameter(Mandatory=$true)][string]$ManifestPath)
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutablePath).Hash -ne '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'){throw 'Exact Steam PE required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\omega-t7-effects-rt1';New-Item -ItemType Directory -Force $obj | Out-Null
$exe=Join-Path $obj 'OmegaT7EffectsRt1.exe'
$sources=@('tests\OmegaT7EffectsRt1.cpp','hooks\F8RuntimeCore.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_TARGET_STEAM_20261001 {1} /Fe"{2}" user32.lib' -f $vcvars,($sources -join ' '),$exe
Push-Location $obj
try {& $env:ComSpec /d /s /c $compile;if($LASTEXITCODE){throw 'Omega T7 effects build failed'}
    & $exe $ExecutablePath $ManifestPath;if($LASTEXITCODE){throw 'Omega T7 native effects failed'}
}finally {Pop-Location}
