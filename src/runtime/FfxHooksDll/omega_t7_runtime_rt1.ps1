param([Parameter(Mandatory=$true)][string]$ExecutablePath,[Parameter(Mandatory=$true)][string]$KernelPath,[Parameter(Mandatory=$true)][string]$CommandsPath,[Parameter(Mandatory=$true)][string]$ManifestPath,[Parameter(Mandatory=$true)][string]$MonsterCommandsPath)
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutablePath).Hash -ne '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'){throw 'Exact Steam PE required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe';$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat';$obj=Join-Path $PSScriptRoot 'obj\omega-t7-runtime-rt1';New-Item -ItemType Directory -Force $obj | Out-Null
$exe=Join-Path $obj 'OmegaT7RuntimeRt1.exe'
$mh=Join-Path $PSScriptRoot 'third_party\minhook'
$sources=@('tests\OmegaT7RuntimeRt1.cpp','hooks\OmegaT7Runtime.cpp','hooks\F8RuntimeCore.cpp','hooks\F7DifficultyCore.cpp','hooks\SinRamScalingCore.cpp','hooks\MinHookBatchCoordinator.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_TARGET_STEAM_20261001 /DFFXHOOKS_HAVE_POLYHOOK /I"{3}\include" {1} hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{2}" user32.lib' -f $vcvars,($sources -join ' '),$exe,$mh
Push-Location $obj
try{
    $compileC='call "{0}" x86 >nul && cl /nologo /c /TC /O2 /MT /I"{1}\include" "{1}\src\hook.c" "{1}\src\buffer.c" "{1}\src\trampoline.c" "{1}\src\hde\hde32.c"' -f $vcvars,$mh
    & $env:ComSpec /d /s /c $compileC;if($LASTEXITCODE){throw 'MinHook native queue fixture compilation failed'}
    & $env:ComSpec /d /s /c $compile;if($LASTEXITCODE){throw 'Omega T7 runtime compile failed'}
    & $exe $ExecutablePath $KernelPath $CommandsPath $ManifestPath $MonsterCommandsPath;if($LASTEXITCODE){throw 'Omega T7 runtime validation failed'}
}finally{Pop-Location}
