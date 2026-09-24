param([Parameter(Mandatory=$true)][string]$ExecutablePath, [Parameter(Mandatory=$true)][string]$CarrierPath, [Parameter(Mandatory=$true)][string]$ProfilesPath)
# CustomMix Ultra production-runtime RT0/RT1 harness (x86, no game process).
$ErrorActionPreference = 'Stop'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $ExecutablePath).Hash -ne '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED' -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $CarrierPath).Hash -ne 'DDF8D89343195D3D014630C296A9583EE556EFA839918435802249FE148594D0') {
    throw 'Exact reviewed PE/carrier fixtures required; do not launch the game'
}

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $vswhere) { throw 'vswhere.exe was not found' }
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) { throw 'Visual Studio C++ tools were not found' }
$vcvarsall = Join-Path $installationPath.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $vcvarsall)) { throw "vcvarsall.bat was not found: $vcvarsall" }

$objDir = Join-Path $PSScriptRoot 'obj\arena-program-native-rt1'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'tests\ArenaProgramNativeRt1.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.cpp'),
    (Join-Path $PSScriptRoot 'hooks\CustomMixRuntime.cpp'),
    (Join-Path $PSScriptRoot 'hooks\ArenaBattleProgram.cpp'),
    (Join-Path $PSScriptRoot 'hooks\CustomMixUltraCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\CustomMixWindowsAdapter.cpp'),
    (Join-Path $PSScriptRoot 'hooks\SharedBattleRuntime.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F7UiCore.cpp')
)
$sourceArguments = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$exe = Join-Path $objDir 'ArenaProgramNativeRt1.exe'
$pdb = Join-Path $objDir 'ArenaProgramNativeRt1.pdb'
$mh = Join-Path $PSScriptRoot 'third_party\minhook'
$cSources = @('hook.c','buffer.c','trampoline.c','hde\hde32.c') | ForEach-Object { '"{0}"' -f (Join-Path "$mh\src" $_) }
$compileC = 'call "{0}" x86 >nul && cl /nologo /TC /O2 /MT /W3 /I"{1}\include" /c {2}' -f $vcvarsall,$mh,($cSources -join ' ')
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING /DFFXHOOKS_HAVE_POLYHOOK /I"{4}\include" {1} hook.obj buffer.obj trampoline.obj hde32.obj /Fd"{2}" /Fe"{3}" kernel32.lib user32.lib bcrypt.lib' -f `
    $vcvarsall, $sourceArguments, $pdb, $exe, $mh

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compileC
    if ($LASTEXITCODE -ne 0) { throw 'MinHook fixture compilation failed' }
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "CustomMix runtime RT0/RT1 compilation failed with exit code $LASTEXITCODE" }

    $binary = [System.IO.File]::ReadAllBytes($exe)
    if ($binary.Length -lt 0x40) { throw 'CustomMix runtime output is not a valid PE image' }
    $peOffset = [BitConverter]::ToInt32($binary, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length) { throw 'CustomMix runtime PE header is out of bounds' }
    $machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
    if ($machine -ne 0x014C) { throw ('CustomMix runtime expected I386 machine 0x014C, got 0x{0:X4}' -f $machine) }

    $sceneCount=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\ArenaSceneryCatalog.inc') | Where-Object { $_ -match '^\s*\{' }).Count + 9
    for($start=0;$start -lt $sceneCount;$start+=24){
        $end=[Math]::Min($start+24,$sceneCount)
        & $exe $ExecutablePath $CarrierPath $ProfilesPath $start $end
        if($LASTEXITCODE -ne 0){throw "Native scene batch $start..$end failed"}
    }
    if ($LASTEXITCODE -ne 0) { throw "CustomMix runtime executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host ('Arena normal program native consumers RT1: PASS (RT0 contracts + RT1 composition; COFF-I386 0x{0:X4})' -f $machine)
