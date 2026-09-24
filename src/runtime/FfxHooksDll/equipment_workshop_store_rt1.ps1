$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '..\..\..')).Path
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $here 'obj\equipment-workshop-store-rt1'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$exe=Join-Path $obj 'EquipmentWorkshopStoreRt1.exe'
$cmd='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /I"{1}\research\equipment_workshop\include" "{2}\tests\EquipmentWorkshopStoreRt1.cpp" "{2}\hooks\EquipmentWorkshopStore.cpp" "{2}\hooks\RonsoPoolStore.cpp" "{2}\hooks\RonsoPoolSave.cpp" "{1}\research\equipment_workshop\src\workshop.cpp" /Fe"{3}" bcrypt.lib' -f $vcvars,$repo,$here,$exe
Push-Location $obj
try {
 & $env:ComSpec /d /s /c $cmd
 if($LASTEXITCODE -ne 0){throw 'Workshop store build failed'}
 $fixture=Join-Path $repo 'native-fixtures\ffx_000'
 $data=Join-Path $obj ('private-'+[guid]::NewGuid().ToString('N'))
 & $exe $fixture $data
 if($LASTEXITCODE -ne 0){throw 'Workshop store RT1 failed'}
} finally {Pop-Location}
