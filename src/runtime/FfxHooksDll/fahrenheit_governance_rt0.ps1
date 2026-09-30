# Jarvis-HOOK: real menu draft transactions and cooperative admission, no game.
$ErrorActionPreference='Stop'
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$out=Join-Path $PSScriptRoot 'work/fahrenheit-governance-rt0'
New-Item -ItemType Directory -Force $out | Out-Null
$cases=@{
  F7ConfigEditorRt0=@('hooks/F7ConfigState.cpp','hooks/F7DifficultyCore.cpp','hooks/SinRamConfigCore.cpp','hooks/SinRamScalingCore.cpp','hooks/MinHookBatchCoordinator.cpp')
  FahrenheitInputRt0=@()
  FahrenheitServicesRt0=@()
  FahrenheitCoexistenceRt0=@()
}
foreach($name in ($cases.Keys | Sort-Object)){
  $sources=@('tests/'+$name+'.cpp')+$cases[$name]
  $quoted=($sources|ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}) -join ' '
  $exe=Join-Path $out ($name+'.exe');$rsp=Join-Path $out ($name+'.rsp')
  [IO.File]::WriteAllText($rsp,('/nologo /std:c++17 /EHsc /MT /O2 /W4 /WX /utf-8 '+$quoted+' /Fo"'+$out+'/" /Fe:"'+$exe+'" /link kernel32.lib user32.lib'),[Text.Encoding]::Unicode)
  & cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
  if($LASTEXITCODE){throw "Governance build failed: $name"}
  & $exe
  if($LASTEXITCODE){throw "Governance test failed: $name"}
}
