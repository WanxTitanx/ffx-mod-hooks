# Monster AI observe-only portable RT0/RT1 harness and production denylist.
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

$objDir = Join-Path $PSScriptRoot 'obj\monster-ai-observer-rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$sources = @(
    (Join-Path $PSScriptRoot 'tests\MonsterAiObserverRt0.cpp'),
    (Join-Path $PSScriptRoot 'hooks\FieldScoutAdmissionCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MonsterAiObserverCore.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MonsterAiDispatchShadow.cpp'),
    (Join-Path $PSScriptRoot 'hooks\MonsterAiDispatchTelemetry.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F7AiSwap.cpp')
)
$sourceArguments = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join ' '
$exe = Join-Path $objDir 'MonsterAiObserverRt0.exe'
$pdb = Join-Path $objDir 'MonsterAiObserverRt0.pdb'
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DFFXHOOKS_TESTING {1} /Fd"{2}" /Fe"{3}"' -f `
    $vcvarsall, $sourceArguments, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "Monster AI observer compilation failed with exit code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Monster AI observer harness failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$productionPaths = @(
    (Join-Path $PSScriptRoot 'hooks\F7AiSwap.cpp'),
    (Join-Path $PSScriptRoot 'hooks\F7AiSwap.h'),
    (Join-Path $PSScriptRoot 'dllmain.cpp'),
    (Join-Path (Split-Path $PSScriptRoot -Parent) 'NativeMenuShell\NativeMenuShell.h')
)
$production = ($productionPaths | ForEach-Object { Get-Content -LiteralPath $_ -Raw }) -join "`n"
$adapterSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\F7AiSwap.cpp') -Raw
$coreSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MonsterAiObserverCore.cpp') -Raw
$dispatchTelemetryHeader = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MonsterAiDispatchTelemetry.h') -Raw
$dispatchTelemetrySource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MonsterAiDispatchTelemetry.cpp') -Raw
$dispatchShadowHeader = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MonsterAiDispatchShadow.h') -Raw
$observerTestSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tests\MonsterAiObserverRt0.cpp') -Raw
$fieldScoutSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\FieldScoutHook.cpp') -Raw
$fieldScoutAdmissionSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\FieldScoutAdmissionCore.cpp') -Raw
$fieldScoutAdmissionHeader = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\FieldScoutAdmissionCore.h') -Raw
$batchCoordinatorSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.cpp') -Raw
$batchCoordinatorHeader = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.h') -Raw
$batchCoordinatorContract = $batchCoordinatorHeader + "`n" + $batchCoordinatorSource
$productionCpp = Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -Filter '*.cpp' -File |
    Where-Object { $_.FullName -notmatch '[\\/](tests|obj|bin|third_party|vcpkg_installed)[\\/]' }
if (@($productionCpp).Count -eq 0) {
    throw 'Production C++ source inventory is unexpectedly empty'
}
$forbidden = @(
    '0xDD6', '0x438', '0x606', '0x608',
    'F7AiSwap_ApplyAbilityNow', 'F7AiSwap_SaveConfig', 'F7AiSwap_Reload',
    'ApplyStatusToTarget', 'statusOnHit', 'entry[0xF78]'
)
foreach ($token in $forbidden) {
    if ($production.IndexOf($token, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Monster AI production denylist matched: $token"
    }
}
$coreHeader = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hooks\MonsterAiObserverCore.h') -Raw
foreach ($writeApi in @('WriteMemory', 'writeBytes', 'ByteWriter', 'MutationIo')) {
    if ($coreHeader.IndexOf($writeApi, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Portable observer unexpectedly exposes a write API: $writeApi"
    }
}
foreach ($required in @(
    'RVA_MONSTER_SCRIPT_REGISTRATION = 0x00384120u',
    'RVA_MONSTER_SCRIPT_CLEANUP = 0x00381660u',
    'RVA_MONSTER_SCRIPT_REGISTRATION_CALLER = 0x003839C4u',
    'RVA_MONSTER_AI_DISPATCH = MonsterAiShadow::kDispatcherRva',
    'RVA_MONSTER_AI_DISPATCH_RELOCATION_0 = 0x003AC9E8u',
    'RVA_MONSTER_AI_DISPATCH_RELOCATION_1 = 0x003AC9F6u',
    'RVA_MONSTER_AI_DISPATCH_RELOCATION_2 = 0x003ACA06u',
    'RVA_MONSTER_AI_DISPATCH_RELOCATION_3 = 0x003ACA1Eu',
    'std::array<uint8_t, 66> kDispatchSignature',
    'ValidateObserverProfile',
    'CountExecutableDirectCalls',
    'CountExecutableSignatureMatches',
    'CollectExecutableDirectCallReturns',
    'ValidateDispatchCallerReturns',
    'FFXHOOKS_VALIDATE_ONLY',
    'ObserveRegistration',
    'ObserveCleanup',
    'F7AiSwap_RequestStop',
    'OBSERVE PENDING RESTART',
    'OBSERVING',
    'INERT (RESTART REQUIRED)',
    'UNAVAILABLE',
    'STOPPING'
)) {
    if ($production.IndexOf($required, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Monster AI production contract is missing: $required"
    }
}
foreach ($knownOwner in @('Difficulty', 'SeymourBattle')) {
    if ($batchCoordinatorHeader.IndexOf($knownOwner, [System.StringComparison]::Ordinal) -lt 0 -or
        $batchCoordinatorSource.IndexOf("owner == Owner::$knownOwner", [System.StringComparison]::Ordinal) -lt 0) {
        throw "The shared coordinator must distinctly admit the reviewed owner: $knownOwner"
    }
}
$coordinatorPath = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot 'hooks\MinHookBatchCoordinator.cpp'))
$seymourOwnerPath = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot 'hooks\SeymourBattleCore.cpp'))
foreach ($sourceFile in $productionCpp) {
    $sourceText = Get-Content -LiteralPath $sourceFile.FullName -Raw
    $sourcePath = [System.IO.Path]::GetFullPath($sourceFile.FullName)
    if ($sourcePath -ne $coordinatorPath -and $sourcePath -ne $seymourOwnerPath -and
        $sourceText -match 'Owner::SeymourBattle') {
        throw "Only the reviewed Seymour core may exercise the distinct Seymour owner: $($sourceFile.FullName)"
    }
}
$seymourOwnerSource = Get-Content -LiteralPath $seymourOwnerPath -Raw
if ($seymourOwnerSource -notmatch 'EnableBatch\([\s\S]*?Owner::SeymourBattle' -or
    $seymourOwnerSource -notmatch 'NeutralizeBatch\([\s\S]*?Owner::SeymourBattle') {
    throw 'The reviewed Seymour owner must use the shared coordinator for exact enable and neutralization batches'
}
if ($production -notmatch 'void\s+__cdecl\s+RegistrationShim\(\)\s*\{\s*MonsterAiObserver::CallbackLease\s+callback\(&g_lifecycle\);') {
    throw 'Registration shim must acquire its callback lease as the first C++ statement'
}
if ($production -notmatch 'void\s+__cdecl\s+CleanupShim\(\)\s*\{\s*MonsterAiObserver::CallbackLease\s+callback\(&g_lifecycle\);') {
    throw 'Cleanup shim must acquire its callback lease as the first C++ statement'
}
if ($adapterSource -notmatch
        'int32_t\s+__cdecl\s+DispatchShim\(int32_t\s+actorIndex,\s*int32_t\s+commandStack32,\s*uint32_t\s+targetMask,\s*int32_t\s+force,\s*int32_t\s+n64\)\s*\{\s*MonsterAiObserver::CallbackLease\s+callback\(&g_lifecycle\);') {
    throw 'Physical dispatcher shim must retain the exact x86 cdecl five-DWORD ABI and lease first'
}
$dispatchShimMatch = [regex]::Match(
    $adapterSource,
    '(?s)int32_t\s+__cdecl\s+DispatchShim\(.*?\)\s*\{(?<body>.*?)\n\}\s*\n\s*bool\s+DetourCreate')
if (-not $dispatchShimMatch.Success -or
    $dispatchShimMatch.Groups['body'].Value -notmatch
        '(?s)ObserveDispatchAdapter\(.*?actorIndex,\s*commandStack32,\s*targetMask,\s*force,\s*n64,\s*sink,\s*original\)' -or
    $dispatchShimMatch.Groups['body'].Value -match
        '(?i)(effectiveCommand|replacementCommand|WriteProcessMemory|memcpy\s*\([^,]*(actor|script|command))') {
    throw 'Dispatcher shim must forward all five original arguments through the no-write adapter'
}
$dispatchEventMatch = [regex]::Match(
    $dispatchTelemetryHeader,
    '(?s)struct\s+DispatchEvent\s*\{(?<body>.*?)\};')
if (-not $dispatchEventMatch.Success -or
    $dispatchEventMatch.Groups['body'].Value -match 'uintptr_t|\bvoid\s*\*|scriptBytes|scriptPointer|actorPointer') {
    throw 'DispatchEvent must expose bounded scalar telemetry without raw pointers or script bytes'
}
foreach ($writeApi in @('WriteProcessMemory', 'MutationIo', 'effectiveCommand', 'replacementCommand')) {
    if (($dispatchTelemetryHeader + "`n" + $dispatchTelemetrySource).IndexOf(
            $writeApi, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Dispatch telemetry unexpectedly exposes mutation authority: $writeApi"
    }
}
$profileMatch = [regex]::Match(
    $adapterSource,
    '(?s)ProfileValidation\s+ValidateObserverProfile\(.*?\)\s*\{(?<body>.*?)\n\}\s*\n\s*const\s+char\*\s+ProfileValidationName')
foreach ($profileGate in @(
    'ObserverSignatureTarget::Dispatch',
    'CountExecutableSignatureMatches',
    'signatureCount != 1u',
    'CollectExecutableDirectCallReturns',
    'ValidateDispatchCallerReturns'
)) {
    if (-not $profileMatch.Success -or
        $profileMatch.Groups['body'].Value.IndexOf(
            $profileGate, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Dispatcher profile gate is missing: $profileGate"
    }
}
$removeBodyMatch = [regex]::Match(
    $adapterSource,
    '(?s)void\s+F7AiSwap_Remove\(\)\s*\{(?<body>.*?)\n\}\s*\n\s*bool\s+F7AiSwap_IsEnabled\(')
if (-not $removeBodyMatch.Success -or
    $removeBodyMatch.Groups['body'].Value -notmatch
        'RemoveObserverAdapter\(\s*RuntimeDetours\(\),\s*&MinHookBatch::ProcessCoordinator\(\),\s*MinHookBatch::RuntimeBatchIo\(\),\s*RuntimeDrain\(\),\s*&g_lifecycle,\s*&g_detourOwner\)') {
    throw 'Production removal must delegate to the fault-injected observer adapter'
}
if ($removeBodyMatch.Groups['body'].Value -match '\.disable\s*\(') {
    throw 'Production removal must not bypass partial-owner filtering with direct disables'
}
if ($removeBodyMatch.Groups['body'].Value -notmatch 'TeardownResult::RetainedInert' -or
    $removeBodyMatch.Groups['body'].Value -notmatch 'trampolines retained until process exit') {
    throw 'Production removal must publish truthful process-lifetime retained/inert status'
}
$portableTeardownMatch = [regex]::Match(
    $coreSource,
    '(?s)TeardownResult\s+StopDrainAndRetainDetourSet\(.*?\)\s*\{(?<body>.*?)\n\}\s*\n\s*\} // namespace')
if (-not $portableTeardownMatch.Success -or
    $portableTeardownMatch.Groups['body'].Value -notmatch
        '(?s)if\s*\(!owner->applyAttempted\s*&&\s*!owner->coordinatorPoisoned\)\s*\{.*?RemoveCreated\s*\(') {
    throw 'Only a never-applied observer transaction may call RemoveCreated'
}
$retentionMarker = $portableTeardownMatch.Groups['body'].Value.IndexOf(
    'Do not call MH_RemoveHook', [System.StringComparison]::Ordinal)
if ($retentionMarker -lt 0 -or
    $portableTeardownMatch.Groups['body'].Value.Substring($retentionMarker) -match
        '(RemoveCreated\s*\(|\.remove\s*\()') {
    throw 'Applied observer teardown must retain trampolines instead of calling RemoveCreated'
}
$installBodyMatch = [regex]::Match(
    $adapterSource,
    '(?s)bool\s+F7AiSwap_Install\(.*?\)\s*\{(?<body>.*?)\n\}\s*\n\s*void\s+F7AiSwap_RequestStop\(')
if (-not $installBodyMatch.Success -or
    $installBodyMatch.Groups['body'].Value -notmatch
        'InstallObserverAdapter\(\s*RuntimeDetours\(\),\s*&MinHookBatch::ProcessCoordinator\(\),\s*MinHookBatch::RuntimeBatchIo\(\),\s*RuntimeDrain\(\),\s*&g_lifecycle,') {
    throw 'Production installation must delegate to the fault-injected observer adapter'
}
foreach ($thirdTargetToken in @(
    'moduleBase + RVA_MONSTER_AI_DISPATCH',
    'reinterpret_cast<uintptr_t>(&DispatchShim)'
)) {
    if ($installBodyMatch.Groups['body'].Value.IndexOf(
            $thirdTargetToken, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Production observer transaction is missing dispatcher target: $thirdTargetToken"
    }
}
$statusBodyMatch = [regex]::Match(
    $adapterSource,
    '(?s)F7AiObserverStatus\s+F7AiSwap_Status\(\)\s*\{(?<body>.*?)\n\}\s*\n\s*const\s+char\*\s+F7AiSwap_StatusName\(')
if (-not $statusBodyMatch.Success -or
    $statusBodyMatch.Groups['body'].Value -notmatch 'ResolveRequestedStatus\(runtimeStatus,\s*requested\)') {
    throw 'Production status must resolve a newly enabled request as pending restart'
}
$stopBodyMatch = [regex]::Match(
    $adapterSource,
    '(?s)void\s+F7AiSwap_RequestStop\(\)\s*\{(?<body>.*?)\n\}\s*\n\s*void\s+F7AiSwap_Remove\(')
if (-not $stopBodyMatch.Success -or
    $stopBodyMatch.Groups['body'].Value -notmatch 'RuntimeStatus\(\)') {
    throw 'DllMain-safe stop request must read only the atomic runtime status'
}
foreach ($loaderLockForbidden in @('F7AiSwap_Status(', 'Config::', 'Sleep(', 'RemoveObserverAdapter(')) {
    if ($stopBodyMatch.Groups['body'].Value.IndexOf($loaderLockForbidden, [System.StringComparison]::Ordinal) -ge 0) {
        throw "DllMain-safe stop request contains forbidden work: $loaderLockForbidden"
    }
}
$runtimeStatusMatch = [regex]::Match(
    $adapterSource,
    '(?s)F7AiObserverStatus\s+RuntimeStatus\(\)\s*\{(?<body>.*?)\n\}')
if (-not $runtimeStatusMatch.Success -or
    $runtimeStatusMatch.Groups['body'].Value -notmatch 'InterlockedCompareExchange') {
    throw 'RuntimeStatus must remain an atomic-only loader-lock helper'
}
foreach ($loaderLockForbidden in @('Config::', 'Sleep(', 'ObserverLog(', 'RemoveObserverAdapter(')) {
    if ($runtimeStatusMatch.Groups['body'].Value.IndexOf(
            $loaderLockForbidden, [System.StringComparison]::Ordinal) -ge 0) {
        throw "RuntimeStatus contains forbidden loader-lock work: $loaderLockForbidden"
    }
}
if ($production.IndexOf('Monster AI Observer - Read-only', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Truthful observe-only UI label is missing'
}
if ($production.IndexOf(
        'Registration and dispatch telemetry only; no compatible swap pair is validated.',
        [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Truthful empty-whitelist UI detail is missing'
}
if ($production.IndexOf('MONSTER AI SWAP selected', [System.StringComparison]::Ordinal) -ge 0) {
    throw 'Legacy Monster AI Swap selection log remains reachable'
}
if ($production.IndexOf('Arrows/Mouse Navigate   Back Exit   F7 Exit', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Observe-only page footer must advertise navigation/back only'
}
if ($production.IndexOf('if (g_f7MenuKind != F7_MENU_AI)', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Observe-only page must ignore horizontal adjustment input'
}
if ($production.IndexOf('else if (g_f7MenuKind != F7_MENU_AI) {', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Observe-only information rows must ignore confirm input without selection feedback'
}

# MinHook owns one process-global heap. A subsystem-local uninitialize would free every other
# subsystem's trampoline, including an observer trampoline retained for a delayed prologue entrant.
foreach ($sourceFile in $productionCpp) {
    $sourceText = Get-Content -LiteralPath $sourceFile.FullName -Raw
    if ($sourceText -match '\bMH_Uninitialize\s*\(') {
        throw "Production subsystem must not uninitialize process-global MinHook: $($sourceFile.FullName)"
    }
}
foreach ($sourceFile in $productionCpp) {
    $sourceText = Get-Content -LiteralPath $sourceFile.FullName -Raw
    foreach ($queueApi in @('MH_Initialize', 'MH_QueueEnableHook', 'MH_QueueDisableHook', 'MH_ApplyQueued')) {
        if ($sourceText -match ("\b{0}\s*\(" -f [regex]::Escape($queueApi)) -and
            [System.IO.Path]::GetFullPath($sourceFile.FullName) -ne $coordinatorPath) {
            throw "Only the process-global coordinator may call $queueApi`: $($sourceFile.FullName)"
        }
    }
}
foreach ($required in @(
    'State::Batch',
    'State::Poisoned',
    'MonsterAiObserver',
    'FieldScout',
    'Difficulty',
    'SeymourBattle',
    'EnsureProcessInitialized',
    'MH_Initialize',
    'NeutralizeLocked',
    'MH_QueueEnableHook',
    'MH_QueueDisableHook',
    'MH_ApplyQueued'
)) {
    if ($batchCoordinatorContract.IndexOf($required, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Process-global MinHook coordinator contract is missing: $required"
    }
}
$applyFailureSection = [regex]::Match(
    $batchCoordinatorSource,
    '(?s)report\.primaryFailure\s*=\s*FailureStage::ApplyEnable;(?<body>.*?)return\s+report;')
if (-not $applyFailureSection.Success -or
    $applyFailureSection.Groups['body'].Value -notmatch 'FinishOwnedBatch\(true\)' -or
    $applyFailureSection.Groups['body'].Value -notmatch 'report\.result\s*=\s*BatchResult::Poisoned') {
    throw 'Any failed enable ApplyQueued attempt must leave the coordinator absorbing Poisoned'
}
$dllmainSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dllmain.cpp') -Raw
$globalInitializeIndex = $dllmainSource.IndexOf(
    'MinHookBatch::EnsureProcessInitialized()', [System.StringComparison]::Ordinal)
$featureStartIndexes = @(
    $dllmainSource.IndexOf('StartUnXBoosterHook(', [System.StringComparison]::Ordinal),
    $dllmainSource.IndexOf('StartSeymourBattleHook(', [System.StringComparison]::Ordinal)
) | Where-Object { $_ -ge 0 }
if ($globalInitializeIndex -lt 0 -or @($featureStartIndexes).Count -eq 0 -or
    $globalInitializeIndex -gt (($featureStartIndexes | Measure-Object -Minimum).Minimum)) {
    throw 'Process-global MinHook initialization must precede every feature start independent of feature gates'
}
foreach ($featureSource in @($adapterSource, $fieldScoutSource)) {
    if ($featureSource -notmatch 'MinHookBatch::EnsureProcessInitialized\s*\(') {
        throw 'MinHook features must consume the shared idempotent initialization contract'
    }
}
$fieldScoutRemoveMatch = [regex]::Match(
    $fieldScoutSource,
    '(?s)bool\s+RemoveFieldScoutHook\(FieldScoutLogFn\s+log\)\s*\{(?<body>.*?)\n\}\s*\n\s*bool\s+IsFieldScoutHookInstalled')
if (-not $fieldScoutRemoveMatch.Success) {
    throw 'FieldScout teardown function was not found'
}
$fieldScoutRemoveBody = $fieldScoutRemoveMatch.Groups['body'].Value
foreach ($stateField in @(
    'bool created',
    'bool queueEnable',
    'bool applyAttempted',
    'bool mayHaveRun',
    'bool isEnabled'
)) {
    if ($fieldScoutSource.IndexOf($stateField, [System.StringComparison]::Ordinal) -lt 0) {
        throw "FieldScout per-target ownership contract is missing: $stateField"
    }
}
if ($fieldScoutRemoveBody -notmatch 'MinHookBatch::NeutralizeBatch\s*\(' -or
    $fieldScoutRemoveBody -notmatch 'g_fieldScoutRetainedInert' -or
    $fieldScoutRemoveBody -notmatch 'retainRuntimeContext' -or
    $fieldScoutRemoveBody -notmatch 'restart required' -or
    $fieldScoutRemoveBody -match 'MH_ALL_HOOKS|MH_RemoveHook\s*\(') {
    throw 'Applied FieldScout teardown must neutralize its exact batch and retain trampolines'
}
$retainContextIndex = $fieldScoutRemoveBody.IndexOf(
    'if (retainRuntimeContext)', [System.StringComparison]::Ordinal)
$freeContextIndex = $fieldScoutRemoveBody.IndexOf(
    'FreeSeenStore()', [System.StringComparison]::Ordinal)
if ($retainContextIndex -lt 0 -or $freeContextIndex -lt 0 -or
    $retainContextIndex -gt $freeContextIndex) {
    throw 'FieldScout must return with complete runtime context retained after an apply boundary'
}
$fieldScoutInstallMatch = [regex]::Match(
    $fieldScoutSource,
    '(?s)FieldScoutInstallResult\s+InstallFieldScoutHook\(.*?\)\s*\{(?<body>.*?)\n\}\s*\n\s*bool\s+ApplyFieldScoutQueuedHooks')
if (-not $fieldScoutInstallMatch.Success) {
    throw 'FieldScout install function was not found'
}
$fieldScoutInstallBody = $fieldScoutInstallMatch.Groups['body'].Value
$reinstallIndex = $fieldScoutInstallBody.IndexOf(
    'g_fieldScoutRestartRequired || HasCreatedFieldScoutTargets()',
    [System.StringComparison]::Ordinal)
$replaceBaseIndex = $fieldScoutInstallBody.IndexOf(
    'g_base = moduleBase', [System.StringComparison]::Ordinal)
if ($reinstallIndex -lt 0 -or $replaceBaseIndex -lt 0 -or $reinstallIndex -gt $replaceBaseIndex) {
    throw 'FieldScout reinstall must reject retained old-base ownership before publishing a new base'
}
if ($fieldScoutInstallBody -notmatch 'MinHookBatch::EnableBatch\s*\(' -or
    $fieldScoutInstallBody -notmatch
        'FieldScoutAdmission::InitializeClosed\(&g_captureAdmission\)' -or
    $fieldScoutInstallBody -notmatch
        'BatchResult::Applied[\s\S]*FieldScoutAdmission::Open\(&g_captureAdmission\)') {
    throw 'FieldScout capture must stay closed until its complete owned batch is applied'
}
foreach ($preHookFailure in @(
    '(?s)if\s*\(!EnsureSeenStore\(\)\)\s*\{.*?ResetFieldScoutPreHookContext\(\)',
    '(?s)if\s*\(!OpenSessionFile\(\)\)\s*\{.*?ResetFieldScoutPreHookContext\(\)',
    '(?s)if\s*\(minHookInitialization\s*!=\s*MinHookBatch::InitializationResult::Ready\)\s*\{.*?ResetFieldScoutPreHookContext\(\)'
)) {
    if ($fieldScoutInstallBody -notmatch $preHookFailure) {
        throw 'FieldScout pre-hook failure must release the unowned session and dedupe context'
    }
}
if ($dllmainSource -match 'ApplyFieldScoutQueuedHooks\s*\(') {
    throw 'dllmain worker must not apply a FieldScout queue outside the owning install transaction'
}

# Every retained FieldScout detour can resume after disable from an uncounted machine prologue.
# Each family must check the same process-sticky admission before any transition/capture/thread
# side effect, while the portable core prevents a path callback from reopening after shutdown.
$shimContracts = @(
    @{ Function = 'BuildTextureSlot_FieldScoutHook'; Family = 'BuildTextureSlot' },
    @{ Function = 'GraphicFieldMapLoad_FieldScoutHook'; Family = 'GraphicFieldMapLoad' },
    @{ Function = 'LoadAndActivateDriver_FieldScoutHook'; Family = 'LoadAndActivateDriver' },
    @{ Function = 'GetInstanceNameByIndex_FieldScoutHook'; Family = 'GetInstanceNameByIndex' },
    @{ Function = 'MsBattleEncountExe_QuiesceHook'; Family = 'BattleEncounter' },
    @{ Function = 'WireInstanceToSceneNodes_FieldScoutHook'; Family = 'WireInstanceToSceneNodes' },
    @{ Function = 'FieldMap_CommitInstanceMappings_Hook'; Family = 'CommitInstanceMappings' },
    @{ Function = 'ChrSetWorldPosition_FieldScoutHook'; Family = 'ChrSetWorldPosition' },
    @{ Function = 'TakaraLoad_MaxHook'; Family = 'TakaraLoad' },
    @{ Function = 'WarpActor_MaxHook'; Family = 'WarpActor' },
    @{ Function = 'SampleZoneSlot_MaxHook'; Family = 'SampleZoneSlot' }
)
foreach ($contract in $shimContracts) {
    $functionIndex = $fieldScoutSource.IndexOf(
        "$($contract.Function)(", [System.StringComparison]::Ordinal)
    if ($functionIndex -lt 0) {
        throw "FieldScout shim definition is missing: $($contract.Function)"
    }
    $nextStatic = $fieldScoutSource.IndexOf(
        'static ', $functionIndex + $contract.Function.Length,
        [System.StringComparison]::Ordinal)
    if ($nextStatic -lt 0) { $nextStatic = $fieldScoutSource.Length }
    $shimBody = $fieldScoutSource.Substring($functionIndex, $nextStatic - $functionIndex)
    $entryToken = "FieldScoutAdmission::ShimFamily::$($contract.Family)"
    $entryIndex = $shimBody.IndexOf($entryToken, [System.StringComparison]::Ordinal)
    if ($entryIndex -lt 0 -or $entryIndex -gt 600) {
        throw "FieldScout shim must check sticky admission at entry: $($contract.Function)"
    }
}
if ($fieldScoutSource -notmatch
        '(?s)GraphicFieldMapLoad_FieldScoutHook\(.*?ShouldSkipFieldScoutCapture\(\)\s*&&\s*ExtractMapFieldLoose\(.*?SetCurrentMapField\(') {
    throw 'FieldScout map identity publication must remain behind sticky capture admission'
}
foreach ($required in @(
    'TryEnterAfterPrologue',
    'ApplyPathTransition',
    'compare_exchange_weak',
    'TryAcquireThreadStart',
    'CloseAndDrainThreadStarts',
    'RequestClose'
)) {
    if (($fieldScoutAdmissionHeader + "`n" + $fieldScoutAdmissionSource).IndexOf(
            $required, [System.StringComparison]::Ordinal) -lt 0) {
        throw "FieldScout sticky-admission contract is missing: $required"
    }
}
if ($fieldScoutSource -match '\bg_(shuttingDown|captureQuiesced)\b') {
    throw 'FieldScout must not retain split shutdown/quiesce variables that can reopen independently'
}
if ($fieldScoutSource -notmatch
        '(?s)static\s+void\s+NoteBattleTransitionFromPath\(.*?ApplyPathTransition\(.*?ResumeField' -or
    $fieldScoutSource -notmatch
        '(?s)static\s+void\s+StartPlayerTraceThread\(\).*?TryAcquireThreadStart\(.*?ReleaseThreadStart\(') {
    throw 'FieldScout transition and trace-thread start must consume sticky admission primitives'
}

# A failed process-global initialization is not a feature-local warning. No MinHook consumer may
# start when the coordinator is not Ready, independent of individual gates.
if ($dllmainSource -notmatch
        'const\s+bool\s+minHookReady\s*=\s*minHookInitialization\s*==\s*FfxHooks::MinHookBatch::InitializationResult::Ready' -or
    $dllmainSource -notmatch
        '(?s)MinHook-dependent features skipped;.*?process-global initialization is not ready' -or
    $dllmainSource -notmatch
        '(?s)enableFieldScout.*?else\s+if\s*\(!minHookReady\).*?FieldScout install blocked' -or
    $dllmainSource -notmatch
        '(?s)if\s*\(minHookReady\)\s*\{.*?F7_InstallHooks\(.*?F7AiSwap_Install\(') {
    throw 'dllmain worker must fail closed before every MinHook-dependent feature start'
}
$setupFailureMatch = [regex]::Match(
    $adapterSource,
    '(?s)void\s+F7AiSwap_ReportSetupFailure\(.*?\)\s*\{(?<body>.*?)\n\}')
if (-not $setupFailureMatch.Success -or
    $setupFailureMatch.Groups['body'].Value -notmatch 'if\s*\(!requested\)\s*return' -or
    $setupFailureMatch.Groups['body'].Value -notmatch 'PublishStatus\(F7AiObserverStatus::Unavailable\)' -or
    $setupFailureMatch.Groups['body'].Value -match 'EnsureProcessInitialized|RuntimeBatchIo|\bMH_') {
    throw 'requested Monster AI observer setup failure must publish UNAVAILABLE without touching MinHook'
}
if ($dllmainSource -notmatch
        '(?s)if\s*\(minHookReady\).*?F7AiSwap_Install\(.*?else\s*\{.*?F7AiSwap_ReportSetupFailure\(') {
    throw 'dllmain must publish Monster AI observer setup failure when shared MinHook is not ready'
}
if ($observerTestSource -match 'TestRetainedOwnerSurvivesFieldScoutLifecycle|difficultyTargets|createdAfterDifficulty' -or
    $observerTestSource -notmatch 'TestRetainedMonsterObserverOwnerSurvivesFieldScoutLifecycle' -or
    $observerTestSource -notmatch 'supportedImageBase\s*\+\s*0x00384120u' -or
    $observerTestSource -notmatch 'supportedImageBase\s*\+\s*0x00381660u' -or
    $observerTestSource -notmatch 'supportedImageBase\s*\+\s*0x003AC9E0u') {
    throw 'retained-owner coverage must name and use the exact Monster AI observer targets'
}
$projectSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'FfxHooksDll.vcxproj') -Raw
$buildSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build_hooks.ps1') -Raw
foreach ($projectEntry in @(
    'hooks\MonsterAiDispatchShadow.h',
    'hooks\MonsterAiDispatchTelemetry.h',
    'hooks\MonsterAiDispatchShadow.cpp',
    'hooks\MonsterAiDispatchTelemetry.cpp'
)) {
    if ($projectSource.IndexOf($projectEntry, [System.StringComparison]::Ordinal) -lt 0) {
        throw "x86 DLL project is missing dispatcher observer source: $projectEntry"
    }
}
foreach ($buildEntry in @(
    'hooks\MonsterAiDispatchShadow.cpp',
    'hooks\MonsterAiDispatchTelemetry.cpp'
)) {
    if ($buildSource.IndexOf($buildEntry, [System.StringComparison]::Ordinal) -lt 0) {
        throw "direct x86 release build is missing dispatcher observer source: $buildEntry"
    }
}
if ($dispatchShadowHeader.IndexOf(
        'a runtime observer must forward the original dispatcher arguments unchanged.',
        [System.StringComparison]::Ordinal) -lt 0) {
    throw 'Dispatch shadow core must retain its explicit no-mutation production boundary'
}
$handoffSource = Get-Content -LiteralPath (
    Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) `
        'docs\ai\SESSION_HANDOFF.md') -Raw
foreach ($required in @(
    'this commit is **not deployable by itself**',
    'separately reviewed Difficulty partial-create/apply/retention commit',
    '`Owner::Difficulty`'
)) {
    if ($handoffSource.IndexOf($required, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Composed Difficulty dependency documentation is missing: $required"
    }
}
foreach ($required in @(
    'Monster AI dispatcher observe-only production integration',
    '`0x003AC9E0`',
    '`0x003A454E`',
    '`0x003A4A60`',
    '`0x003A4B8C`',
    'invokes vanilla exactly once with all five DWORD arguments unchanged',
    'retains every trampoline/context until process exit',
    'no runtime pointer or script bytes',
    'mutation remains out of scope'
)) {
    if ($handoffSource.IndexOf($required, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Dispatcher observer handoff truth is missing: $required"
    }
}
$docsRoot = Join-Path (
    Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'docs'
$f7InLiveSource = Get-Content -LiteralPath (Join-Path $docsRoot 'F7_INLIVE.md') -Raw
$roadmapSource = Get-Content -LiteralPath (Join-Path $docsRoot 'ROADMAP.md') -Raw
$rt2ProtocolSource = Get-Content -LiteralPath (Join-Path $docsRoot 'RT2_PROTOCOL.md') -Raw
$knownBugsSource = Get-Content -LiteralPath (Join-Path $docsRoot 'KNOWN_BUGS.md') -Raw
$readmeSource = Get-Content -LiteralPath (Join-Path (Split-Path $docsRoot -Parent) 'README.md') -Raw
$f7InLiveContract = [regex]::Replace($f7InLiveSource, '\s+', ' ')
$roadmapContract = [regex]::Replace($roadmapSource, '\s+', ' ')
$rt2ProtocolContract = [regex]::Replace($rt2ProtocolSource, '\s+', ' ')
$knownBugsContract = [regex]::Replace($knownBugsSource, '\s+', ' ')
$readmeContract = [regex]::Replace($readmeSource, '\s+', ' ')
foreach ($required in @(
    'three-target registration/cleanup/dispatcher observer batch',
    '`0x003AC9E0`',
    '`0x003A454E`',
    '`0x003A4A60`',
    '`0x003A4B8C`',
    '`0x003AC9E8`',
    '`0x003AC9F6`',
    '`0x003ACA06`',
    '`0x003ACA1E`',
    'five cdecl DWORD arguments unchanged',
    'original exactly once',
    'queue readback',
    'value-only',
    'zero mutation',
    'process lifetime'
)) {
    if ($f7InLiveContract.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "F7 In-Live dispatcher observer documentation is missing: $required"
    }
}
foreach ($required in @(
    'registration/cleanup/dispatcher',
    'normal/force/death',
    'repeated-event preservation',
    'zero mutation'
)) {
    if ($roadmapContract.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "Roadmap dispatcher observer truth is missing: $required"
    }
}
foreach ($required in @(
    'F7 Monster AI observer RT2 slice',
    'OFF by default',
    '`0x003A454E`',
    '`0x003A4A60`',
    '`0x003A4B8C`',
    'five cdecl DWORD arguments unchanged',
    'original exactly once',
    'queue readback',
    'repeated-event preservation',
    'zero mutation',
    'live observations remain pending',
    'End the enabled battle',
    '`[ffx-hooks] MonsterAiObserver teardown generation=<active-generation> thread=<thread-id>`',
    'exactly one cleanup line',
    'matching the registered active generation',
    'retire that generation',
    'stale-generation dispatch',
    'preserve the exact cleanup line',
    'partial/reject'
)) {
    if ($rt2ProtocolContract.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "Monster AI RT2 slice is missing: $required"
    }
}
foreach ($required in @(
    'K-06',
    'three-target registration/cleanup/dispatcher lifecycle',
    'normal/force/death',
    'repeated-event preservation',
    'five cdecl DWORD arguments unchanged',
    'original exactly once',
    'queue readback',
    'zero mutation',
    'live observations remain pending'
)) {
    if ($knownBugsContract.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "Known-bugs Monster AI observer status is missing: $required"
    }
}
foreach ($required in @(
    'Monster AI Observer - Read-only',
    'registration/cleanup/dispatcher',
    'value-only telemetry',
    'zero mutation',
    'no compatible pair has been validated'
)) {
    if ($readmeContract.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "README Monster AI observer truth is missing: $required"
    }
}
$neverAppliedRemoveMatch = [regex]::Match(
    $fieldScoutSource,
    '(?s)static\s+bool\s+RemoveNeverAppliedOwnedHook\(.*?\)\s*\{(?<body>.*?)\n\}')
if (-not $neverAppliedRemoveMatch.Success -or
    $neverAppliedRemoveMatch.Groups['body'].Value -notmatch
        'g_fieldScoutApplyAttempted\s*\|\|\s*state->applyAttempted\s*\|\|\s*state->mayHaveRun' -or
    $neverAppliedRemoveMatch.Groups['body'].Value -notmatch 'MH_RemoveHook\s*\(') {
    throw 'FieldScout may remove hooks only behind the never-applied ownership guard'
}

Write-Host 'MONSTER AI OBSERVER RT0/RT1: PASS'
