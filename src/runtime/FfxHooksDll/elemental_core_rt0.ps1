# Jarvis-HOOK: portable production cores compiled with the shipping x86 toolchain.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x86 tools are unavailable' }
$vcvars = Join-Path $vs.Trim() 'VC\Auxiliary\Build\vcvarsall.bat'
$obj = Join-Path $here 'obj\elemental-core-rt0'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$cases = @('ElementRegistryRt0','ElementAffinityRt0','ElementScanCoreRt0',
    'ElementScanSettingsContractRt0','BattleDamagePolicyRt0','CombatExtensionBusRt0',
    'ElementPackJsonRt0','ElementPackCoreRt0','ElementBattleStateRt0','ElementPackAdmissionRt0',
    'ElementMonsterProofRt0','SpiraAbilityCatalogRt0','SpiraRulesRt0','ElementalScanViewRt0','ElementBuiltinCoreRt0')
foreach ($case in $cases) {
    $source = Join-Path $here ('tests\' + $case + '.cpp')
    $binary = Join-Path $obj ($case + '.exe')
    $object = Join-Path $obj ($case + '.obj')
    $command = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /O2 /MT /W4 /WX /utf-8 "{1}" /Fo"{2}" /Fe"{3}"' -f $vcvars,$source,$object,$binary
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw ('Core build failed: ' + $case + ' exit=' + $LASTEXITCODE) }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw ('Core test failed: ' + $case + ' exit=' + $LASTEXITCODE) }
}
Write-Output 'ELEMENTAL_CORE_MSVC_X86_RT0_PASS'
