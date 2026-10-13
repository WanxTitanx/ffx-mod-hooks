param([Parameter(Mandatory=$true)][string]$ExecutablePath)
$ErrorActionPreference='Stop'
if((Get-FileHash $ExecutablePath).Hash -ne '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'){throw 'Exact Steam PE required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe';$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat';$obj=Join-Path $PSScriptRoot 'obj\workshop-main-menu-rt1';New-Item -ItemType Directory -Force $obj | Out-Null
$exe=Join-Path $obj 'EquipmentWorkshopMainMenuRt1.exe'
$mh=Join-Path $PSScriptRoot 'third_party\minhook'
$mapper=Get-Content -Raw (Join-Path $PSScriptRoot 'tests/ArenaPositionNativeRt1.cpp')
$start=$mapper.IndexOf('static std::vector<unsigned char> Read(');$end=$mapper.IndexOf('static bool Nested(',$start)
if($start -lt 0 -or $end -le $start){throw 'Private PE mapper missing'}
[IO.File]::WriteAllText((Join-Path $obj 'WorkshopMainPeFixture.inc'),$mapper.Substring($start,$end-$start))
$sources=@('tests\EquipmentWorkshopMainMenuRt1.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_TARGET_STEAM_20261001 /DFFXHOOKS_HAVE_POLYHOOK /I"{3}\include" /I"{4}" {1} hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{2}" user32.lib' -f $vcvars,($sources -join ' '),$exe,$mh,$obj
Push-Location $obj
try{
    $compileC='call "{0}" x86 >nul && cl /nologo /c /TC /O2 /MT /I"{1}\include" "{1}\src\hook.c" "{1}\src\buffer.c" "{1}\src\trampoline.c" "{1}\src\hde\hde32.c"' -f $vcvars,$mh
    & $env:ComSpec /d /s /c $compileC;if($LASTEXITCODE){throw 'MinHook native queue fixture compilation failed'}
    & $env:ComSpec /d /s /c $compile;if($LASTEXITCODE){throw 'Workshop main menu compile failed'}
    & $exe $ExecutablePath;if($LASTEXITCODE){throw 'Workshop main menu validation failed'}
}finally{Pop-Location}
