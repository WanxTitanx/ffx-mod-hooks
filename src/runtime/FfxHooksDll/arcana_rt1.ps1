param(
    [Parameter(Mandatory=$true)][string]$GameExecutable,
    [Parameter(Mandatory=$true)][string]$NativeSaveFixture,
    [Parameter(Mandatory=$true)][string]$AssetsRoot,
    [string]$CandidateDll,
    [string]$OutputDirectory=(Join-Path $PSScriptRoot 'obj\arcana-rt1'),
    [ValidateSet('ui','runtime','combat','assets','save')][string[]]$Cases=@('ui','runtime','combat','assets','save')
)
# Jarvis-HOOK: private PE/CRT fixtures and WARP only. Never starts or deploys FFX.
$ErrorActionPreference='Stop'
$GameExecutable=(Resolve-Path -LiteralPath $GameExecutable).Path
$NativeSaveFixture=(Resolve-Path -LiteralPath $NativeSaveFixture).Path
$AssetsRoot=(Resolve-Path -LiteralPath $AssetsRoot).Path
if((Get-FileHash -LiteralPath $GameExecutable -Algorithm SHA256).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED') {throw 'Unsupported private PE fixture'}
if($Cases -contains 'save') {
    $crt=Join-Path ([IO.Path]::GetDirectoryName($GameExecutable)) 'msvcr110.dll'
    if(-not (Test-Path -LiteralPath $crt) -or (Get-FileHash -LiteralPath $crt -Algorithm SHA256).Hash -ne 'B30160E759115E24425B9BCDF606EF6EBCE4657487525EDE7F1AC40B90FF7E49') {throw 'Place the supported game CRT beside the private PE fixture'}
}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC x86 tools required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$mh=Join-Path $PSScriptRoot 'third_party\minhook'
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$OutputDirectory=(Resolve-Path $OutputDirectory).Path
$sources=@(
 'hooks\ArcanaCore.cpp','hooks\ArcanaAcquisition.cpp','hooks\ArcanaStore.cpp',
 'hooks\ArcanaNativeEffects.cpp','hooks\ArcanaRuntime.cpp','hooks\ArcanaCombatCore.cpp','hooks\ArcanaCombat.cpp',
 'hooks\ArcanaUiCore.cpp','hooks\ArcanaNativeUi.cpp','hooks\ArcanaAssets.cpp',
 'hooks\EquipmentWorkshopRuntime.cpp','hooks\EquipmentWorkshopNativeUi.cpp','hooks\EquipmentWorkshopStore.cpp',
 'hooks\RonsoPoolCore.cpp','hooks\RonsoPoolRuntime.cpp','hooks\RonsoPoolStore.cpp','hooks\RonsoPoolSave.cpp',
 'hooks\F8RuntimeCore.cpp','shared\Config.cpp','hooks\MinHookBatchCoordinator.cpp'
) | ForEach-Object {Join-Path $PSScriptRoot $_}
$sources+=@( (Join-Path $repo 'research\equipment_workshop\src\workshop.cpp'),(Join-Path $repo 'research\equipment_workshop\src\lifecycle.cpp') )
$sourceArgs=($sources | ForEach-Object {'"'+$_+'"'}) -join ' '
$c=@('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object {'"'+$mh+'\src\'+$_+'"'}
$flags='/nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK'
$includes='/I"{0}\include" /I"{1}\research\equipment_workshop\include"' -f $mh,$repo
$libraries='bcrypt.lib d3d11.lib d3dcompiler.lib windowscodecs.lib ole32.lib'
function Compile([string]$command) {
    & $env:ComSpec /d /s /c ('call "{0}" x86 >nul && {1}' -f $vcvars,$command)
    if($LASTEXITCODE -ne 0){throw ('MSVC failed: '+$LASTEXITCODE)}
}
function RunFixture([string]$name,[string[]]$arguments) {
    & (Join-Path $OutputDirectory ($name+'.exe')) @arguments
    if($LASTEXITCODE -ne 0){throw ($name+' failed: '+$LASTEXITCODE)}
}
Push-Location $OutputDirectory
try {
    Compile ('cl /nologo /TC /O2 /MT /W3 /I"{0}\include" /c {1}' -f $mh,($c -join ' '))
    Compile ('cl {0} {1} /c {2}' -f $flags,$includes,$sourceArgs)
    $objects=@('hook.obj','buffer.obj','trampoline.obj','hde32.obj')+@($sources | ForEach-Object {[IO.Path]::GetFileNameWithoutExtension($_)+'.obj'})
    $objectArgs=($objects | ForEach-Object {'"'+$_+'"'}) -join ' '
    $names=@{ui='ArcanaNativeUiRt1';runtime='ArcanaRuntimeRt1';combat='ArcanaCombatRt1';assets='ArcanaAssetsRt1';save='RonsoPoolIoRt1'}
    foreach($key in $Cases) {
        $name=$names[$key]
        Compile ('cl {0} {1} "{2}\tests\{3}.cpp" {4} /Fe"{3}.exe" {5}' -f $flags,$includes,$PSScriptRoot,$name,$objectArgs,$libraries)
    }
    if($Cases -contains 'ui'){
        RunFixture 'ArcanaNativeUiRt1' @($GameExecutable)
        RunFixture 'ArcanaNativeUiRt1' @($GameExecutable,'workshop')
    }
    if($Cases -contains 'combat'){RunFixture 'ArcanaCombatRt1' @($GameExecutable)}
    if($Cases -contains 'assets'){RunFixture 'ArcanaAssetsRt1' @($AssetsRoot)}
    if($Cases -contains 'runtime'){foreach($mode in @('development','normal','workshop','legacy','v2','v3','v4')) {
        $data=Join-Path $OutputDirectory ('session-'+[guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($data) | Out-Null
        RunFixture 'ArcanaRuntimeRt1' @($GameExecutable,$NativeSaveFixture,$data,$mode)
    }}
    if($Cases -contains 'save'){
        $data=Join-Path $OutputDirectory ('save-'+[guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($data) | Out-Null
      foreach($mode in @('on','off')) {
        RunFixture 'RonsoPoolIoRt1' @($GameExecutable,$NativeSaveFixture,$data,$mode)
    }}
    if($CandidateDll){
        $candidate=(Resolve-Path -LiteralPath $CandidateDll).Path
        Compile ('cl {0} "{1}\tests\ArcanaDllSmokeRt1.cpp" /Fe:ArcanaDllSmokeRt1.exe' -f $flags,$PSScriptRoot)
        $data=Join-Path $OutputDirectory ('dll-smoke-'+[guid]::NewGuid().ToString('N'))
        RunFixture 'ArcanaDllSmokeRt1' @($candidate,$data,$GameExecutable)
    }
    Write-Output 'PASS Arcana RT1 fixtures; live gameplay and visual acceptance remain RT2'
} finally {Pop-Location}
