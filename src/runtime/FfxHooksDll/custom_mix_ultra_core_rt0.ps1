# CustomMix Ultra RT0/RT1 harness. It compiles only the portable RAM transaction core as x86.
$ErrorActionPreference = 'Stop'

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

$objDir = Join-Path $PSScriptRoot 'obj\custommix-ultra-core-rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$testSource = Join-Path $PSScriptRoot 'tests\CustomMixUltraCoreRt0.cpp'
$coreSource = Join-Path $PSScriptRoot 'hooks\CustomMixUltraCore.cpp'
$exe = Join-Path $objDir 'CustomMixUltraCoreRt0.exe'
$pdb = Join-Path $objDir 'CustomMixUltraCoreRt0.pdb'

# WHY: selection and future platform adapters may validate or classify memory, but the
# exact sixteen-byte carrier window must keep a single writer. This source gate makes a
# second mutation site fail RT0 instead of silently bypassing compare-before-restore.
$coreText = [System.IO.File]::ReadAllText($coreSource)
$executeMarker = 'TransactionOutcome ExecuteTransaction('
$executeOffset = $coreText.IndexOf($executeMarker, [StringComparison]::Ordinal)
if ($executeOffset -lt 0) { throw 'ExecuteTransaction source marker was not found' }
$carrierWritePattern = 'std::memcpy\(carrier\.bytes \+ kFormationSlotOffset'
$carrierWrites = [regex]::Matches($coreText, $carrierWritePattern)
if ($carrierWrites.Count -ne 2) {
    throw "Expected exactly two carrier writes (patch + restore), found $($carrierWrites.Count)"
}
$returnOffset = $coreText.IndexOf('    return outcome;', $carrierWrites[1].Index,
                                  [StringComparison]::Ordinal)
if ($returnOffset -lt 0) { throw 'ExecuteTransaction return marker was not found' }
foreach ($write in $carrierWrites) {
    if ($write.Index -lt $executeOffset -or $write.Index -gt $returnOffset) {
        throw 'Carrier mutation was found outside ExecuteTransaction'
    }
}
if ([regex]::IsMatch($coreText, 'carrier\.bytes\s*\[[^\]]+\]\s*=')) {
    throw 'Direct carrier byte assignment bypasses ExecuteTransaction memcpy ownership'
}
$directRejects = [regex]::Matches(
    $coreText, 'return\s+Reject\(TransactionResult::([A-Za-z0-9_]+)\);')
if ($directRejects.Count -ne 1 -or
    $directRejects[0].Groups[1].Value -ne 'OriginalUnavailable') {
    throw 'Every callable-original rejection must use the exact-once passthrough path'
}
foreach ($forbidden in @(
    'WriteProcessMemory', 'VirtualProtect', 'CreateFile', 'MoveFile',
    'CreateProcess', 'MinHook', 'compose_last', 'manifest.json',
    'malloc(', 'calloc(', 'realloc(', 'std::vector', 'std::ifstream', 'std::ofstream'
)) {
    if ($coreText.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Portable CustomMix core contains forbidden runtime/disk token: $forbidden"
    }
}

$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT "{1}" "{2}" /Fd"{3}" /Fe"{4}"' -f `
    $vcvarsall, $testSource, $coreSource, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "CustomMix Ultra core RT0/RT1 compilation failed with exit code $LASTEXITCODE" }

    $binary = [System.IO.File]::ReadAllBytes($exe)
    if ($binary.Length -lt 0x40) { throw 'CustomMix Ultra core RT0/RT1 output is not a valid PE image' }
    $peOffset = [BitConverter]::ToInt32($binary, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length) { throw 'CustomMix Ultra core RT0/RT1 PE header is out of bounds' }
    $machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
    if ($machine -ne 0x014C) { throw ('CustomMix Ultra core RT0/RT1 expected I386 machine 0x014C, got 0x{0:X4}' -f $machine) }

    & $exe
    if ($LASTEXITCODE -ne 0) { throw "CustomMix Ultra core RT0/RT1 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'CustomMix Ultra portable x86 core: PASS (RT0 validation + RT1 isolated transaction)'
