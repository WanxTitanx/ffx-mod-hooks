# Jarvis-HOOK: private value/core and real Windows file-transaction tests.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 is required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\aeon-ascension-store-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
Push-Location $obj
try {
    $core=Join-Path $obj 'AeonAscensionCoreRt0.exe'
    $cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /I"{1}\research\equipment_workshop\include" "{2}\tests\AeonAscensionCoreRt0.cpp" "{1}\research\equipment_workshop\src\workshop.cpp" /Fe"{3}"' -f $vcvars,$repo,$here,$core
    & $env:ComSpec /d /s /c $cmd
    if($LASTEXITCODE -ne 0){throw 'Ascension core compilation failed'}
    & $core
    if($LASTEXITCODE -ne 0){throw 'Ascension core regression failed'}
    $exe=Join-Path $obj 'AeonAscensionStoreRt1.exe'
    $cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /I"{1}\research\equipment_workshop\include" "{2}\tests\AeonAscensionStoreRt1.cpp" "{2}\hooks\EquipmentWorkshopStore.cpp" "{2}\hooks\RonsoPoolStore.cpp" "{2}\hooks\RonsoPoolSave.cpp" "{1}\research\equipment_workshop\src\workshop.cpp" /Fe"{3}" bcrypt.lib' -f $vcvars,$repo,$here,$exe
    & $env:ComSpec /d /s /c $cmd
    if($LASTEXITCODE -ne 0){throw 'Ascension store compilation failed'}
    $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
    & $exe (Join-Path $repo 'native-fixtures\ffx_000') $data
    if($LASTEXITCODE -ne 0){throw 'Ascension store regression failed'}
} finally {Pop-Location}
