param([Parameter(Mandatory=$true)][string]$ExecutablePath,[switch]$LegacyExecutable)
$ErrorActionPreference='Stop'
$expected=if($LegacyExecutable){'78ce34397da5e6f49b72c2aebadedaf4cd3f6720e1949d46a1b8ed67d3db5ced'}else{'0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'}
if((Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash.ToLowerInvariant() -ne $expected){throw 'Exact selected executable required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC installation not found'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$profile=if($LegacyExecutable){'legacy'}else{'steam'}
$obj=Join-Path $PSScriptRoot ('obj/original-ps2-rng-runtime-rt1-'+$profile)
$temp=Join-Path $obj 'temp';New-Item -ItemType Directory -Force $temp|Out-Null
$mapper=Get-Content -Raw (Join-Path $PSScriptRoot 'tests/ArenaPositionNativeRt1.cpp')
$begin=$mapper.IndexOf('static std::vector<unsigned char> Read(');$end=$mapper.IndexOf('static bool Nested(',$begin)
if($begin -lt 0 -or $end -le $begin){throw 'Reviewed PE mapper not found'}
[IO.File]::WriteAllText((Join-Path $obj 'MusicPeFixture.inc'),$mapper.Substring($begin,$end-$begin))
$sources=@('tests/OriginalPs2RngRuntimeRt1.cpp','hooks/OriginalPs2RngRuntime.cpp','hooks/OriginalPs2RngInputs.cpp','hooks/OriginalPs2RngCore.cpp','hooks/F8RuntimeCore.cpp','hooks/MinHookBatchCoordinator.cpp')|ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$c=@('buffer.c','hook.c','trampoline.c','hde/hde32.c')|ForEach-Object {'"'+(Join-Path $PSScriptRoot ('third_party/minhook/src/'+$_))+'"'}
$define=if($LegacyExecutable){''}else{' /DFFXHOOKS_TARGET_STEAM_20261001'}
$exe=Join-Path $obj 'OriginalPs2RngRuntimeRt1.exe'
Push-Location $obj
try {
    & $env:ComSpec /c ('call "{0}" x86 >nul && set "TEMP={1}" && set "TMP={1}" && cl /nologo /MT /W3 /TC /c {2}' -f $vcvars,$temp,($c -join ' '))
    if($LASTEXITCODE){throw 'MinHook private harness compile failed'}
    & $env:ComSpec /c ('call "{0}" x86 >nul && set "TEMP={1}" && set "TMP={1}" && cl /nologo /MT /W4 /WX /EHsc /std:c++17 /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK{2} /I"{3}" /I"{6}" {4} buffer.obj hook.obj trampoline.obj hde32.obj /Fe"{5}"' -f $vcvars,$temp,$define,$obj,($sources -join ' '),$exe,(Join-Path $PSScriptRoot 'third_party/minhook/include'))
    if($LASTEXITCODE){throw 'Native RNG runtime harness compile failed'}
    foreach($case in @('off','validate','signature','table','late','stop-before','success','no-reset','wrong-thread','clock-failure','counter-failure','foreign-counter','unknown-caller','stop-committed','rebind-reset','stop-starting','hook-conflict','reset-phase','argument-wrap')){
        & $exe $ExecutablePath $case
        if($LASTEXITCODE){throw "Native RNG runtime case failed: $case"}
    }
    $gateSources=@('tests/OriginalPs2RngShippingRt1.cpp','hooks/OriginalPs2RngRuntime.cpp','hooks/OriginalPs2RngInputs.cpp','hooks/OriginalPs2RngCore.cpp','hooks/F8RuntimeCore.cpp','hooks/MinHookBatchCoordinator.cpp')|ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
    $gateExe=Join-Path $obj 'OriginalPs2RngShippingRt1.exe'
    & $env:ComSpec /c ('call "{0}" x86 >nul && set "TEMP={1}" && set "TMP={1}" && cl /nologo /MT /W4 /WX /EHsc /std:c++17 /DFFXHOOKS_HAVE_POLYHOOK{2} /I"{3}" /I"{6}" {4} buffer.obj hook.obj trampoline.obj hde32.obj /Fe"{5}"' -f $vcvars,$temp,$define,$obj,($gateSources -join ' '),$gateExe,(Join-Path $PSScriptRoot 'third_party/minhook/include'))
    if($LASTEXITCODE){throw 'Shipping gate harness compile failed'}
    & $gateExe enabled
    if($LASTEXITCODE){throw 'Shipping unsupported-profile gate did not fail closed'}
    & $gateExe
    if($LASTEXITCODE){throw 'Shipping OFF gate did not fail closed'}
} finally {Pop-Location}
