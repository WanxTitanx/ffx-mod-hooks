param([Parameter(Mandatory=$true)][string]$ExecutablePath,[Parameter(Mandatory=$true)][string]$FixtureGame)
$ErrorActionPreference='Stop'
if((Get-FileHash -LiteralPath $ExecutablePath).Hash -ne '0537b2a1047f3266e73495cd4e35f63f0777f4231d417699f979954686da686d'){throw 'Exact Steam executable fixture required'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x86 required'}
$vcvars=Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj=Join-Path $PSScriptRoot 'obj\omega-ultima-rt1'
New-Item -ItemType Directory -Force $obj | Out-Null
$sources=@('tests\OmegaUltimaRuntimeRt1.cpp','hooks\OmegaUltimaRuntime.cpp','hooks\F8RuntimeCore.cpp','shared\Config.cpp') | ForEach-Object {'"'+(Join-Path $PSScriptRoot $_)+'"'}
$exe=Join-Path $obj 'OmegaUltimaRuntimeRt1.exe'
$command='call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_TARGET_STEAM_20261001 {1} /Fe"{2}" bcrypt.lib user32.lib' -f $vcvars,($sources -join ' '),$exe
Push-Location $obj
try {
    & $env:ComSpec /d /s /c $command
    if($LASTEXITCODE -ne 0){throw 'Omega Ultima RT1 compilation failed'}
    $failed=@()
    foreach($mode in @('normal','asset-fail','ownership','exception','exception-ownership','stop-roll','stop-original','name-drift','bytes-drift','ticket-drift','flag-drift')){
        & $exe $ExecutablePath $FixtureGame $mode
        if($LASTEXITCODE -ne 0){$failed+=$mode}
    }
    if($failed.Count){throw ('Omega Ultima RT1 failed: '+($failed -join ', '))}
} finally {Pop-Location}
