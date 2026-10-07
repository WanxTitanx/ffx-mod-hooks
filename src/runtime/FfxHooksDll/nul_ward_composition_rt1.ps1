. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
# Jarvis-HOOK: isolated native bank/producer/cap admission, no game session.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 is required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\nul-ward-composition-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$mh=Join-Path $here 'third_party\minhook'
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+(Join-Path "$mh\src" $_)+'"'}
$compileC='call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvars,$mh,($c -join ' ')
$sources=@('tests\ElementalRuntimeRt1.cpp','hooks\ElementalRuntime.cpp','hooks\NulWardHook.cpp','hooks\SharedBattleRuntime.cpp','hooks\EquipmentWorkshopRuntime.cpp',
    'hooks\EquipmentWorkshopStore.cpp','hooks\RonsoPoolCore.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp',
    'hooks\RonsoPoolRuntime.cpp','hooks\NovaSuperDamageHook.cpp','hooks\F8RuntimeCore.cpp',
    'hooks\MinHookBatchCoordinator.cpp','shared\Config.cpp') | ForEach-Object {'"'+(Join-Path $here $_)+'"'}
$exe=Join-Path $obj 'ElementalRuntimeRt1.exe'
$compile='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_NUL_COMPOSITION /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{1}\research\equipment_workshop\include" /I"{2}\include" /external:I"{5}\include" /external:W0 {3} "{1}\research\equipment_workshop\src\workshop.cpp" "{1}\research\equipment_workshop\src\lifecycle.cpp" hook.obj buffer.obj trampoline.obj hde32.obj /Fe"{4}" bcrypt.lib user32.lib /link /LIBPATH:"{5}\lib" PolyHook_2.lib Zydis.lib Zycore.lib asmjit.lib asmtk.lib' -f $vcvars,$repo,$mh,($sources -join ' '),$exe,(Join-Path $here 'vcpkg_installed\x86-windows-static')
$fixture=Join-Path $repo 'native-fixtures\FFX.exe'
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Wrong private PE'}
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $compileC
    if($LASTEXITCODE -ne 0){throw 'MinHook compilation failed'}
    & $env:ComSpec /d /s /c $compile
    if($LASTEXITCODE -ne 0){throw 'Elemental runtime compilation failed'}
    $failed=$false
    foreach($mode in @('nul-first','elemental-first')){
        $case=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
        & $exe $fixture $case $mode
        Write-Output ('NUL_MODE_RESULT {0} exit={1}' -f $mode,$LASTEXITCODE)
        if($LASTEXITCODE -ne 0){$failed=$true}
    }
    if($failed){throw 'NulWard composition failed'}
} finally {Pop-Location}
