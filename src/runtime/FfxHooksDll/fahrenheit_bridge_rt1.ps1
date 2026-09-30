$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'work\fahrenheit-bridge-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 is required.'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$exe=Join-Path $out 'fahrenheit-bridge-rt1.exe'
$rsp=Join-Path $out 'build.rsp'
$argsText='/nologo /std:c++17 /EHsc /MT /O2 /W4 /Fo"{1}\\" /Fe:"{2}" "{0}\tests\FahrenheitBridgeRt1.cpp"' -f $PSScriptRoot,$out,$exe
$implementation=Join-Path $PSScriptRoot 'hooks\FahrenheitBridge.cpp'
if(Test-Path -LiteralPath $implementation){$argsText+=' "'+$implementation+'"'}
[IO.File]::WriteAllText($rsp,$argsText,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw "Bridge harness compile failed: $LASTEXITCODE"}
& $exe
if($LASTEXITCODE){throw "Bridge harness failed: $LASTEXITCODE"}
