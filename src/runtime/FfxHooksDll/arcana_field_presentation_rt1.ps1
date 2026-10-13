. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
# Jarvis-HOOK: private PE admission/native field fixtures only, never runs FFX.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\arcana-field-presentation-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$files=@('tests\ArcanaFieldPresentationRt1.cpp','hooks\ArcanaCore.cpp','hooks\ArcanaPresentation.cpp','hooks\ArcanaFieldDialogCore.cpp','hooks\ArcanaFieldPresentation.cpp','hooks\CaptureCascadeCore.cpp','hooks\F8RuntimeCore.cpp','hooks\MinHookBatchCoordinator.cpp')
$sources=$files | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$exe=Join-Path $obj 'ArcanaFieldPresentationRt1.exe'
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\include" {2} hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{3}" user32.lib bcrypt.lib' -f $vcvars,$mh,($sources -join ' '),$exe
$game=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Unsupported private PE'}
Push-Location $obj
try {
    & $env:ComSpec /c $compileC
    if($LASTEXITCODE){throw 'MinHook compilation failed'}
    & $env:ComSpec /c $compile
    if($LASTEXITCODE){throw 'Arcana field presentation compilation failed'}
    $metrics=Join-Path $repo 'native-fixtures\font-base.ftc'
    if(Test-Path -LiteralPath $metrics){& $exe $game $metrics}else{& $exe $game}
    if($LASTEXITCODE){throw 'Arcana field presentation admission failed'}
} finally {Pop-Location}
