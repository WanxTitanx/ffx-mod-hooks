$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$repo=(Resolve-Path (Join-Path $here '../../..')).Path
$fixture=Join-Path $repo 'native-fixtures/FFX.exe'
if((Get-FileHash $fixture).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'){throw 'Exact private FFX fixture required; never start its entrypoint'}
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$out=Join-Path $here 'work/fahrenheit-managed-io-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$exe=Join-Path $out 'fahrenheit-managed-io-rt1.exe'
$sources=@('tests/FahrenheitManagedIoRt1.cpp','hooks/RonsoPoolRuntime.cpp','hooks/RonsoPoolCore.cpp','hooks/RonsoPoolSave.cpp','hooks/RonsoPoolStore.cpp','hooks/F8RuntimeCore.cpp')
$quoted=($sources|ForEach-Object {'"'+(Join-Path $here $_)+'"'}) -join ' '
$argsText='/nologo /std:c++17 /EHsc /MT /O2 /W4 /utf-8 '+$quoted+' /Fo"'+$out+'/" /Fe:"'+$exe+'" /link bcrypt.lib'
$rsp=Join-Path $out 'build.rsp'
[IO.File]::WriteAllText($rsp,$argsText,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw 'Managed I/O harness compile failed'}
foreach($mode in @('off','on','pending-exit')){
    $owned=Join-Path $out ('owned-'+[guid]::NewGuid().ToString('N'))
    & $exe $fixture (Join-Path $repo 'native-fixtures/ffx_000') $owned $mode
    if($LASTEXITCODE){throw "Managed I/O harness failed: $mode"}
    if(Test-Path (Join-Path $owned 'unexpected-exit-finalization.txt')){throw 'Pending transaction ran its observer during CRT teardown'}
}
