param([Parameter(Mandatory=$true)][string]$ExecutablePath,
      [Parameter(Mandatory=$true)][string]$CommandsPath,
      [Parameter(Mandatory=$true)][string]$ManifestPath,
      [Parameter(Mandatory=$true)][string]$DispelPath)
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutablePath -Algorithm SHA256).Hash -ne '0537B2A1047F3266E73495CD4E35F63F0777F4231D417699F979954686DA686D'){throw 'Exact Steam fixture required'}
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\inside-sin-rt1'
New-Item -ItemType Directory -Force $obj | Out-Null
$sources=@('hooks\InsideSinRuntime.cpp','hooks\F8RuntimeCore.cpp','tests\InsideSinRuntimeRt1.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$exe=Join-Path $obj 'InsideSinRuntimeRt1.exe'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_TARGET_STEAM_20261001 /I"{1}\research\equipment_workshop\include" {2} /Fe"{3}" user32.lib' -f $vcvars,$repo,($sources -join ' '),$exe
Push-Location $obj
try {
 & $env:ComSpec /d /s /c $cmd
 if($LASTEXITCODE -ne 0){throw 'Inside Sin RT1 compilation failed'}
 & $exe $ExecutablePath $CommandsPath $ManifestPath $DispelPath
 if($LASTEXITCODE -ne 0){throw 'Inside Sin isolated native validation failed'}
} finally {Pop-Location}
