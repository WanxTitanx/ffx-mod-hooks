. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
# Jarvis-HOOK: private x86 native-frame replay; never launches FFX.exe.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\monster-rewards-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$core=Join-Path $obj 'MonsterRewardsCoreRt0.exe'
$compileCore='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /WX /utf-8 /MT "{1}\tests\MonsterRewardsCoreRt0.cpp" /Fe"{2}"' -f $vcvars,$here,$core
$sources=@('tests\MonsterRewardsRuntimeRt1.cpp','hooks\MonsterRewardsRuntime.cpp','hooks\F8RuntimeCore.cpp','hooks\F8FlagCatalog.cpp','hooks\MinHookBatchCoordinator.cpp','shared\Config.cpp') | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$exe=Join-Path $obj 'MonsterRewardsRuntimeRt1.exe'
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\include" {2} hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{3}" user32.lib' -f $vcvars,$mh,($sources -join ' '),$exe
$fixture=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Unsupported private executable fixture'}
Push-Location $obj
try{
    & $env:ComSpec /c $compileC;if($LASTEXITCODE -ne 0){throw 'MinHook compilation failed'}
    & $env:ComSpec /c $compileCore;if($LASTEXITCODE -ne 0){throw 'Reward arithmetic compilation failed'}
    & $core;if($LASTEXITCODE -ne 0){throw 'Reward arithmetic checks failed'}
    & $env:ComSpec /c $compile;if($LASTEXITCODE -ne 0){throw 'Reward native harness compilation failed'}
    foreach($mode in @('off','invalid','enabled')){
        $private=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
        & $exe $fixture $mode $private;if($LASTEXITCODE -ne 0){throw "Reward native harness failed: $mode"}
    }
}finally{Pop-Location}
