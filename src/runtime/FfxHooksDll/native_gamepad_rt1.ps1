$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'Visual Studio C++ tools were not found'}
$vcvars=Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\native-gamepad-rt1'
New-Item -ItemType Directory -Force $obj|Out-Null
$sources=@('tests\NativeGamepadWindowsRt1.cpp','hooks\NativeGamepadHook.cpp','hooks\F8RuntimeCore.cpp','shared\Config.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$exe=Join-Path $obj 'NativeGamepadWindowsRt1.exe'
Push-Location $obj
try {
    $command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fe"{2}" kernel32.lib user32.lib' -f $vcvars,($sources -join ' '),$exe
    & $env:ComSpec /d /c $command
    if($LASTEXITCODE -ne 0){throw 'Native gamepad harness build failed'}
    & $exe
    if($LASTEXITCODE -ne 0){throw 'Native gamepad harness failed'}
} finally {Pop-Location}
