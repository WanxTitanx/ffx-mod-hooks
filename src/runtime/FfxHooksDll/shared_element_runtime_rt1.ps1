# Jarvis-HOOK: shared affinity and actual Vanguard composition, private PE only.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 tools are required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\shared-element-runtime-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$fixture=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'){throw 'Wrong private PE fixture'}
$common=@('hooks\VanguardRuntime.cpp','hooks\SharedBattleRuntime.cpp','hooks\F8FlagCatalog.cpp','hooks\EquipmentWorkshopRuntime.cpp','shared\Config.cpp','hooks\EquipmentWorkshopStore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp')
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $compileC
    if($LASTEXITCODE -ne 0){throw 'MinHook build failed'}
    foreach($test in @('SharedElementRuntimeRt1','SharedElementVanguardRt1')) {
        $source=@($common+('tests\'+$test+'.cpp')) | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
        $exe=Join-Path $obj ($test+'.exe')
        $cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib user32.lib' -f $vcvars,$repo,$mh,($source -join ' '),$exe
        & $env:ComSpec /d /s /c $cmd
        if($LASTEXITCODE -ne 0){throw ($test+' build failed')}
        $modes=if($test -eq 'SharedElementRuntimeRt1'){@('0','1')}else{@('vanguard-first','element-first')}
        foreach($mode in $modes){
            & $exe $fixture $mode
            if($LASTEXITCODE -ne 0){throw ($test+' failed: '+$mode)}
        }
    }
} finally {Pop-Location}
