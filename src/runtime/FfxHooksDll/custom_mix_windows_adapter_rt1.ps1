# CustomMix Ultra Windows adapter RT1 harness (x86, value-only memory facts, no game process).
$ErrorActionPreference = 'Stop'

$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
)
$vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $vswhere) {
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) { $vswhere = $vswhereCommand.Source }
}
if (-not $vswhere) { throw 'vswhere.exe was not found' }

$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) { throw 'Visual Studio C++ tools were not found' }
$vcvarsall = Join-Path $installationPath.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $vcvarsall)) { throw "vcvarsall.bat was not found: $vcvarsall" }

$objDir = Join-Path $PSScriptRoot 'obj\custommix-windows-adapter-rt1'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$testSource = Join-Path $PSScriptRoot 'tests\CustomMixWindowsAdapterRt1.cpp'
$adapterSource = Join-Path $PSScriptRoot 'hooks\CustomMixWindowsAdapter.cpp'
$adapterHeader = Join-Path $PSScriptRoot 'hooks\CustomMixWindowsAdapter.h'
$exe = Join-Path $objDir 'CustomMixWindowsAdapterRt1.exe'
$pdb = Join-Path $objDir 'CustomMixWindowsAdapterRt1.pdb'

# WHY: this lane may classify a borrowed carrier but may not acquire generic memory,
# filesystem, process, hook, or allocation authority. The portable core remains the
# only owner of the exact sixteen-byte patch/original/compare-restore transaction.
foreach ($path in @($adapterHeader, $adapterSource)) {
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $source = [System.IO.File]::ReadAllText($path)
    foreach ($forbidden in @(
        'WriteProcessMemory', 'ReadProcessMemory', 'VirtualProtect',
        'CreateFile', 'ReadFile', 'WriteFile', 'MoveFile', 'DeleteFile',
        'CreateProcess', 'OpenProcess', 'GetCurrentProcess', 'ShellExecute',
        'VirtualAlloc', 'HeapAlloc', 'malloc(', 'calloc(', 'realloc(',
        'fopen(', 'std::vector', 'std::ifstream', 'std::ofstream',
        'std::filesystem', '.bin'
    )) {
        if ($source.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "CustomMix Windows adapter contains forbidden capability token: $forbidden"
        }
    }
    if ([regex]::IsMatch($source, '\bVirtualQuery\s*\(')) {
        throw 'CustomMix Windows adapter must consume RegionInfo, not call VirtualQuery'
    }
    if ([regex]::IsMatch($source, '\b(new|delete)\b')) {
        throw 'CustomMix Windows adapter must not allocate or delete storage'
    }
}

$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING "{1}" "{2}" /Fd"{3}" /Fe"{4}"' -f `
    $vcvarsall, $testSource, $adapterSource, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "CustomMix Windows adapter RT1 compilation failed with exit code $LASTEXITCODE" }

    $binary = [System.IO.File]::ReadAllBytes($exe)
    if ($binary.Length -lt 0x40) { throw 'CustomMix Windows adapter RT1 output is not a valid PE image' }
    $peOffset = [BitConverter]::ToInt32($binary, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length) { throw 'CustomMix Windows adapter RT1 PE header is out of bounds' }
    $machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
    if ($machine -ne 0x014C) { throw ('CustomMix Windows adapter RT1 expected I386 machine 0x014C, got 0x{0:X4}' -f $machine) }

    & $exe
    if ($LASTEXITCODE -ne 0) { throw "CustomMix Windows adapter RT1 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host ('CustomMix Ultra Windows adapter x86: PASS (RT1 isolated value validation; COFF-I386 0x{0:X4})' -f $machine)
