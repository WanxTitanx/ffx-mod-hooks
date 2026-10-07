# Jarvis-HOOK: execute current input callback source with isolated input/storage.
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$out=Join-Path $here 'obj/f7-audio-rt1'
New-Item -ItemType Directory -Force $out | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $here 'dllmain.cpp')
function Extract([string]$signature,[string]$name){
    $start=$source.LastIndexOf($signature);if($start -lt 0){throw "Missing callback: $signature"}
    $brace=$source.IndexOf('{',$start);$depth=1;$end=$brace+1
    while($depth -gt 0 -and $end -lt $source.Length){if($source[$end] -eq '{'){$depth++};if($source[$end] -eq '}'){$depth--};$end++}
    if($depth){throw "Unbalanced callback: $signature"}
    Set-Content -LiteralPath (Join-Path $out $name) -Value $source.Substring($start,$end-$start) -Encoding utf8
}
Extract 'static int __cdecl F7Sub_InputCb(int obj)' 'F7InputAudio.inc'
Extract 'static bool F7_SaveConfigWithFeedback(' 'F7SaveFeedback.inc'
Extract 'static void F7_AdjustValue(int delta, int sel)' 'F7AdjustAudio.inc'
Extract 'static void F7MainMenuMouseTick(int obj)' 'HubMouseAudio.inc'
Extract 'enum class ArenaPlusMenuKind : int' 'ArenaKindAudio.inc'
Extract 'static int __cdecl ArenaPlus_InputCb(int obj)' 'ArenaInputAudio.inc'
Extract 'static void ArenaPlus_DispatchMenuConfirm(int row)' 'ArenaDispatchAudio.inc'
Extract 'static int __cdecl SinCurse_InputCb(int obj)' 'SinInputAudio.inc'
$sinInput=Join-Path $out 'SinInputAudio.inc'
# Keep the source input callback and real catalog helpers; only seed-validation
# outcomes are injected by the existing isolated audio fixture.
[IO.File]::WriteAllText($sinInput,([IO.File]::ReadAllText($sinInput)).Replace('FfxHooks::SinSpread::ParseSeed','SinSeedForTests'))
$source=Get-Content -Raw -LiteralPath (Join-Path $here '../NativeMenuShell/NativeMenuShell.h')
Extract 'static int __cdecl OurListInputCb(int obj)' 'HubInputAudio.inc'
Extract 'static inline bool ConfirmationAwaitsAllocation(ActionId action)' 'HubAllocationPolicy.inc'
Extract 'static inline void PlayMenuOpenResult(bool opened)' 'HubAllocationResult.inc'
Extract 'static inline void DispatchConfirm(int row)' 'HubDispatchAudio.inc'
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC/Auxiliary/Build/vcvarsall.bat'
$exe=Join-Path $out 'F7AudioInputRt1.exe'
$argsText='/nologo /std:c++17 /EHsc /MT /O2 /W4 /utf-8 /I"'+$out+'" "'+$here+'/tests/F7AudioInputRt1.cpp" "'+$here+'/hooks/F7UiCore.cpp" /Fo"'+$out+'/" /Fe:"'+$exe+'" /link user32.lib'
$rsp=Join-Path $out 'build.rsp';[IO.File]::WriteAllText($rsp,$argsText,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw 'F7 audio host build failed'}
& $exe
if($LASTEXITCODE){throw 'F7 audio regression failed'}
$policy=Join-Path $out 'MenuFeedbackRt0.exe'
$argsText='/nologo /std:c++17 /EHsc /MT /O2 /W4 /WX /utf-8 "'+$here+'/tests/MenuFeedbackRt0.cpp" /Fo"'+$out+'/" /Fe:"'+$policy+'"'
[IO.File]::WriteAllText($rsp,$argsText,[Text.Encoding]::Unicode)
& cmd /d /c ('"'+$vcvars+'" x86 >nul 2>&1 && cl @"'+$rsp+'"')
if($LASTEXITCODE){throw 'Menu feedback policy build failed'}
& $policy
if($LASTEXITCODE){throw 'Menu feedback policy regression failed'}
