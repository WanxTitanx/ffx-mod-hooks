# Jarvis-HOOK: one native action owner, both subscription orders and exceptions.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 is required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\shared-action-runtime-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$sources=@('tests\SharedActionRuntimeRt1.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp','shared\Config.cpp') |
    ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$exe=Join-Path $obj 'SharedActionRuntimeRt1.exe'
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\include" {2} hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{3}" user32.lib' -f $vcvars,$mh,($sources -join ' '),$exe
$fixture=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'){throw 'Wrong private PE'}
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $compileC
    if($LASTEXITCODE -ne 0){throw 'MinHook compilation failed'}
    & $env:ComSpec /d /s /c $compile
    if($LASTEXITCODE -ne 0){throw 'Shared action compilation failed'}
    foreach($mode in @('legacy-first','observer-first')){
        & $exe $fixture $mode
        if($LASTEXITCODE -ne 0){throw ('Shared action runtime failed: '+$mode)}
    }
} finally {Pop-Location}
