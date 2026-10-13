# Jarvis-HOOK: isolated real native help renderer and production Pump-entry regression.
param([Parameter(Mandatory=$true)][string]$ExecutablePath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'tests/executable_profile.ps1')
if((Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash -ne (Get-FfxTestExecutableHash)){throw 'Wrong private executable fixture'}
$here=$PSScriptRoot
$obj=Join-Path $here 'obj/native-menu-help-rt1'
New-Item -ItemType Directory -Force $obj | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $here 'dllmain.cpp')
$marker='static int __cdecl NativeMenu_PumpHook(unsigned int a1)'
$start=$source.IndexOf($marker)
$end=$source.IndexOf('NativeMenuPollHubCloseDrainAfterPump();',$start)
if($start -lt 0 -or $end -le $start){throw 'Actual production Pump entry was not found'}
$entry=$source.Substring($start,$end-$start).Replace('NativeMenu_PumpHook','NativeHelpTestPump')+"return r;`n}"
[IO.File]::WriteAllText((Join-Path $obj 'NativeHelpPumpEntry.inc'),$entry)
$owner=''
$start=$source.IndexOf('static bool NativeMenuOwnsHelpPresentation()')
if($start -ge 0){
 $body=$source.IndexOf('{',$start);$depth=1;$end=$body+1
 while($depth -gt 0 -and $end -lt $source.Length){if($source[$end] -eq '{'){$depth++};if($source[$end] -eq '}'){$depth--};$end++}
 if($depth -ne 0){throw 'Unbalanced production owner predicate'}
 $owner=$source.Substring($start,$end-$start)
}
[IO.File]::WriteAllText((Join-Path $obj 'NativeHelpOwner.inc'),$owner)
$mapper=Get-Content -Raw (Join-Path $here 'tests/ArenaPositionNativeRt1.cpp')
$start=$mapper.IndexOf('static std::vector<unsigned char> Read(');$end=$mapper.IndexOf('static bool Nested(',$start)
if($start -lt 0 -or $end -le $start){throw 'Reviewed private PE mapper was not found'}
[IO.File]::WriteAllText((Join-Path $obj 'NativeHelpPeFixture.inc'),$mapper.Substring($start,$end-$start))
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$deps=Join-Path $here 'vcpkg_installed/x86-windows-static'
$exe=Join-Path $obj 'NativeMenuHelpRt1.exe'
$arguments=@('/nologo','/EHsc','/std:c++17','/W4','/WX','/utf-8','/O2','/MT','/DFFXHOOKS_TESTING','/DFFXHOOKS_COEXISTENCE',
 ('/I"'+$obj+'"'),('/I"'+$deps+'/include"'),('"'+$here+'/tests/NativeMenuHelpRt1.cpp"'),
 ('"'+$here+'/hooks/F8RuntimeCore.cpp"'),('/Fe"'+$exe+'"'),'/link',('/LIBPATH:"'+$deps+'/lib"'),
 'PolyHook_2.lib','Zydis.lib','Zycore.lib','asmjit.lib','asmtk.lib','kernel32.lib','user32.lib')
$rsp=Join-Path $obj 'compile.rsp'
[IO.File]::WriteAllText($rsp,($arguments -join ' '),[Text.Encoding]::Unicode)
Push-Location $obj
try {
 & $env:ComSpec /d /c ('call "'+$vcvars+'" x86 >nul && cl @"'+$rsp+'"') *> (Join-Path $obj 'compile.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $obj 'compile.log');throw 'Help regression compilation failed'}
 & $exe $ExecutablePath
 if($LASTEXITCODE -ne 0){throw 'Native help regression failed'}
} finally {Pop-Location}
