[CmdletBinding()]
param(
    [ValidateSet(
        'None',
        'ResolverOrdering',
        'ForbiddenTokenRemoval',
        'TextModeSnapshot',
        'AcceptTruncatedLog',
        'OtherTargetOn',
        'AtomicReplaceBypass',
        'ProcessRecheckBypass',
        'SnapshotManifestLinkBypass',
        'EvidenceRootContainmentBypass',
        'StructuredLogValidationBypass',
        'FinalVerifyManifestOmission',
        'ReadmeAbsoluteSafetyRevert',
        'ReadmeUniversalSignatureRevert',
        'IniParserLimitsBypass',
        'PreflightIdentityPinBypass',
        'InitialManifestPinWindowBypass'
    )]
    [string]$Mutation = 'None',
    [string]$ScriptPath,
    [string]$ModulePath,
    [string]$ReadmePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$script:Passed = 0
$script:Failed = 0
$script:Skipped = 0
$script:Failures = New-Object System.Collections.Generic.List[string]
$script:ModuleImported = $false
$script:ImportedModule = $null
$script:HarnessPath = $MyInvocation.MyCommand.Path
$script:ForbiddenCommands = @('Start-Process', 'Stop-Process', 'Copy-Item', 'Start-Sleep')

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-False {
    param([bool]$Condition, [string]$Message)
    if ($Condition) { throw $Message }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    if ($Expected -is [System.Array] -or $Actual -is [System.Array]) {
        $difference = Compare-Object -ReferenceObject @($Expected) -DifferenceObject @($Actual) -SyncWindow 0
        if ($difference) {
            throw "$Message (expected=[$(@($Expected) -join ', ')], actual=[$(@($Actual) -join ', ')])"
        }
        return
    }
    if ($Expected -ne $Actual) {
        throw "$Message (expected=[$Expected], actual=[$Actual])"
    }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Pattern, [string]$Message)
    $thrown = $false
    try {
        & $Action
    } catch {
        $thrown = $true
        if ($Pattern -and $_.Exception.Message -notmatch $Pattern) {
            throw "$Message (wrong error: $($_.Exception.Message))"
        }
    }
    if (-not $thrown) { throw "$Message (no error was thrown)" }
}

function Assert-TextMatch {
    param([string]$Text, [string]$Pattern, [string]$Message)
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Assert-TextNotMatch {
    param([string]$Text, [string]$Pattern, [string]$Message)
    if ($Text -match $Pattern) { throw $Message }
}

function Test-Case {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        ++$script:Passed
        Write-Host "PASS: $Name"
    } catch {
        ++$script:Failed
        $detail = "FAIL: $Name -> $($_.Exception.Message)"
        $script:Failures.Add($detail)
        Write-Host $detail
    }
}

function Require-Library {
    if (-not $script:ModuleImported) {
        throw 'run_f8_rt2_lib.psm1 is missing or was not imported'
    }
}

function Get-Sha256Hex {
    param([byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString('X2') }) -join '')
    } finally {
        $sha.Dispose()
    }
}

function Write-BytesCreate {
    param([string]$Path, [byte[]]$Bytes)
    $stream = New-Object System.IO.FileStream(
        $Path,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None
    )
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
}

function New-FixtureFile {
    param([string]$Name, [byte[]]$Bytes = [byte[]](1, 2, 3))
    $path = Join-Path $script:FixtureRoot $Name
    $parent = Split-Path -Parent $path
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
    [System.IO.File]::WriteAllBytes($path, $Bytes)
    return $path
}

function New-TestPathSet {
    $setRoot = Join-Path 'pathsets' ([Guid]::NewGuid().ToString('N'))
    $paths = [ordered]@{
        BuiltDll = New-FixtureFile -Name (Join-Path $setRoot 'build\ffx-hooks.dll') -Bytes ([byte[]](1, 2, 3))
        Exe = New-FixtureFile -Name (Join-Path $setRoot 'game\FFX.exe') -Bytes ([byte[]](4, 5, 6))
        InstalledDll = New-FixtureFile -Name (Join-Path $setRoot 'game\modules\ffx-hooks.dll') -Bytes ([byte[]](1, 2, 3))
        Ini = New-FixtureFile -Name (Join-Path $setRoot 'game\_isolated\ffx-hooks.ini') -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[dashboard]`nenabled=1`n"))
    }
    return $paths
}

function Get-ExpectedCases {
    return @(
        [pscustomobject]@{ Case = 'permanent_sensor'; Canonical = 'boosters.permanent_sensor'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'seymour_battle_roster'; Canonical = 'boosters.playable_seymour'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'speed_hack'; Canonical = 'boosters.speed_hack'; ApplyMode = 'ConfigPolled' },
        [pscustomobject]@{ Case = 'entire_party_earns_ap'; Canonical = 'boosters.entire_party_earns_ap'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'invincible_party'; Canonical = 'cheats.invincible_party'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'invincible_enemies'; Canonical = 'cheats.invincible_enemies'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'always_overdrive'; Canonical = 'cheats.always_overdrive'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'always_critical'; Canonical = 'cheats.always_critical'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'damage_99999'; Canonical = 'cheats.damage_value'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'always_rare_drop'; Canonical = 'cheats.always_rare_drop'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'ap_100x'; Canonical = 'cheats.ap_100x'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'gil_100x'; Canonical = 'cheats.gil_100x'; ApplyMode = 'RuntimeAcknowledged' },
        [pscustomobject]@{ Case = 'arena_plus_compose_f7'; Canonical = 'arena_plus.compose_f7'; ApplyMode = 'ConfigPolled' },
        [pscustomobject]@{ Case = 'dialog_skip'; Canonical = 'input.dialog_skip'; ApplyMode = 'ConfigPolled' }
    )
}

function Get-CommandNamesFromAst {
    param([System.Management.Automation.Language.Ast]$Ast)
    return @($Ast.FindAll(
        { param($node) $node -is [System.Management.Automation.Language.CommandAst] },
        $true
    ) | ForEach-Object { $_.GetCommandName() } | Where-Object { $_ })
}

function Parse-ScriptAst {
    param([string]$Path)
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors)
    return [pscustomobject]@{ Ast = $ast; Tokens = $tokens; Errors = @($errors) }
}

function Get-ManifestFixture {
    param([string]$Case = 'speed_hack', [string]$SessionId = 'session-a')
    $snapshotPath = Join-Path $script:FixtureRoot 'ffx-hooks.ini.snapshot.bin'
    return [ordered]@{
        schema = 'ffx-hooks.f8-rt2-evidence/v1'
        case = $Case
        sessionId = $SessionId
        canonical = 'boosters.speed_hack'
        applyMode = 'ConfigPolled'
        preflightUtc = '2026-08-20T12:00:00.0000000Z'
        paths = [ordered]@{
            ini = [ordered]@{
                path = (Join-Path $script:FixtureRoot 'ffx-hooks.ini')
                sha256 = ('A' * 64)
                length = 3
            }
        }
        snapshot = [ordered]@{
            path = $snapshotPath
            sha256 = ('A' * 64)
            length = 3
        }
    }
}

function New-ValidLogText {
    param(
        [string]$Canonical = 'boosters.speed_hack',
        [string]$ApplyMode = 'ConfigPolled',
        [string]$OtherLine = '',
        [int]$ScalarValue = 25
    )
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('[ffx-hooks] F8 catalog rows=37 live=14 restart=16 not_wired=7')
    $source = if ($Canonical -eq 'arena_plus.compose_f7') { 'AuthoritativeCanonicalIni' } else { 'UnmarkedCanonicalIni' }
    $scalarKey = switch ($Canonical) {
        'cheats.ap_100x' { 'cheats.ap_multiplier' }
        'cheats.gil_100x' { 'cheats.gil_multiplier' }
        default { '' }
    }
    if ($scalarKey) {
        $lines.Add("[ffx-hooks] F8 scalar edit key=$scalarKey edit=SAVED requested=$ScalarValue configured=$ScalarValue")
    }
    $lines.Add("[ffx-hooks] F8 edit key=$Canonical edit=SAVED requested=1 effective=1 source=$source")
    if ($ApplyMode -eq 'RuntimeAcknowledged') {
        if ($scalarKey) {
            $lines.Add("[f8-runtime] key=$Canonical effective=1 source=$source state=applied gate=01 scalar=$ScalarValue")
        } else {
            $lines.Add("[f8-runtime] key=$Canonical effective=1 source=$source state=applied readback=01")
        }
    }
    if ($OtherLine) { $lines.Add($OtherLine) }
    $lines.Add("[ffx-hooks] F8 edit key=$Canonical edit=SAVED requested=0 effective=0 source=$source")
    if ($ApplyMode -eq 'RuntimeAcknowledged') {
        if ($scalarKey) {
            $lines.Add("[f8-runtime] key=$Canonical effective=0 source=$source state=restored gate=00 scalar=none")
        } else {
            $lines.Add("[f8-runtime] key=$Canonical effective=0 source=$source state=restored readback=00")
        }
    }
    return (($lines -join "`r`n") + "`r`n")
}

function Get-AllOffIniBytes {
    $text = @'
[boosters]
permanent_sensor=0
playable_seymour=0
speed_hack=0
entire_party_earns_ap=0
[cheats]
invincible_party=0
invincible_enemies=0
always_overdrive=0
always_critical=0
damage_value=0
always_rare_drop=0
ap_100x=0
gil_100x=0
[arena_plus]
compose_f7=0
[labs]
arena_plus_compose_f7=0
[input]
dialog_skip=0
[dashboard]
enabled=1
'@
    return [System.Text.Encoding]::UTF8.GetBytes($text)
}

function New-ProtocolFixture {
    $paths = New-TestPathSet
    [System.IO.File]::WriteAllBytes($paths.Ini, (Get-AllOffIniBytes))
    $protocolRoot = Split-Path -Parent (Split-Path -Parent $paths.Ini)
    $logPath = New-FixtureFile -Name (Join-Path ('protocol-logs\' + [Guid]::NewGuid().ToString('N')) 'ffx-hooks.log') -Bytes ([System.Text.Encoding]::UTF8.GetBytes("old crash ignored`r`n"))
    $counterPath = "$logPath.cnt"
    [System.IO.File]::WriteAllBytes($counterPath, [System.Text.Encoding]::ASCII.GetBytes('3'))
    $paths['Log'] = $logPath
    $paths['Counter'] = $counterPath
    $paths['GameRoot'] = $protocolRoot

    $supported = '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
    $hashes = @{
        $paths.BuiltDll = ('1' * 64)
        $paths.InstalledDll = ('1' * 64)
        $paths.Exe = $supported
    }
    $processState = [pscustomobject]@{ Calls = 0; Results = @() }
    $providers = @{
        ProcessProvider = {
            ++$processState.Calls
            if ($processState.Results.Count -ge $processState.Calls) {
                return @($processState.Results[$processState.Calls - 1])
            }
            return @()
        }.GetNewClosure()
        HashProvider = { param($path) return $hashes[$path] }.GetNewClosure()
        FlagExistsProvider = { param($path) return $false }
        Environment = @{}
        ClockProvider = { [DateTimeOffset]::Parse('2026-08-20T12:34:56-03:00') }
        CommitProvider = { '2a8f8c540c3f9a9e22dcfa8adb9fd400282f47ec' }
    }
    [pscustomobject]@{
        Paths = $paths
        Providers = $providers
        ProcessState = $processState
        EvidenceRoot = Join-Path $script:FixtureRoot ('evidence-' + [Guid]::NewGuid().ToString('N'))
    }
}

function Add-ValidProtocolLog {
    param(
        [Parameter(Mandatory)]$Fixture,
        [string]$Canonical = 'boosters.speed_hack',
        [string]$ApplyMode = 'ConfigPolled',
        [string]$OtherLine = ''
    )
    $logBytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -Canonical $Canonical -ApplyMode $ApplyMode -OtherLine $OtherLine))
    $stream = New-Object System.IO.FileStream(
        $Fixture.Paths.Log,
        [System.IO.FileMode]::Append,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read
    )
    try {
        $stream.Write($logBytes, 0, $logBytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
    [System.IO.File]::WriteAllBytes($Fixture.Paths.Counter, [System.Text.Encoding]::ASCII.GetBytes('4'))
}

function New-SeymourMemoryEvidenceFixture {
    param([scriptblock]$Transform)

    $persistentState = '00 01 02'
    $persistentAbility = '03 04 05 06 08 09 0A FF FF FF FF FF FF FF FF FF FF'
    $localStateBefore = '00 01 02 03 04 05 06'
    $localAbilityBefore = '08 09 0A FF FF FF FF FF FF FF FF FF FF FF FF FF FF'
    $localAbilityOn = '08 09 0A 07 FF FF FF FF FF FF FF FF FF FF FF FF FF'
    $localStateAfterSwitch = '00 01 02 03 04 05 07'
    $localAbilityAfterSwitch = '08 09 0A 06 FF FF FF FF FF FF FF FF FF FF FF FF FF'
    $evidence = [ordered]@{
        schema = 'ffx-hooks.seymour-battle-memory/v1'
        executable = [ordered]@{
            sha256 = '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
            moduleBase = '0x00400000'
        }
        captureSequence = @('before', 'on', 'afterSwitch', 'off', 'nextBattle')
        leaves = @(
            [ordered]@{
                name = 'persistentState'; rva = '0x00D307E8'; runtimeVa = '0x011307E8'; size = 3
                snapshots = [ordered]@{ before = $persistentState; on = $persistentState; afterSwitch = $persistentState; off = $persistentState; nextBattle = $persistentState }
            },
            [ordered]@{
                name = 'persistentAbility'; rva = '0x00D307EB'; runtimeVa = '0x011307EB'; size = 17
                snapshots = [ordered]@{ before = $persistentAbility; on = $persistentAbility; afterSwitch = $persistentAbility; off = $persistentAbility; nextBattle = $persistentAbility }
            },
            [ordered]@{
                name = 'localState'; rva = '0x00D2C895'; runtimeVa = '0x0112C895'; size = 7
                snapshots = [ordered]@{ before = $localStateBefore; on = $localStateBefore; afterSwitch = $localStateAfterSwitch; off = $localStateAfterSwitch; nextBattle = $localStateBefore }
            },
            [ordered]@{
                name = 'localAbility'; rva = '0x00D2C8A3'; runtimeVa = '0x0112C8A3'; size = 17
                snapshots = [ordered]@{ before = $localAbilityBefore; on = $localAbilityOn; afterSwitch = $localAbilityAfterSwitch; off = $localAbilityAfterSwitch; nextBattle = $localAbilityBefore }
            }
        )
        observations = [ordered]@{
            seymourSelected = $true
            controlledTurnCompleted = $true
            switchCompleted = $true
            normalExitCompleted = $true
            nextBattleEntered = $true
            nextBattleNoReintroduction = $true
            sphereGridNotOpened = $true
        }
    }
    if ($null -ne $Transform) { [void](& $Transform $evidence) }
    $path = Join-Path $script:FixtureRoot ('seymour-memory-' + [Guid]::NewGuid().ToString('N') + '.json')
    $json = ConvertTo-Json -InputObject $evidence -Depth 20
    [System.IO.File]::WriteAllBytes($path, (New-Object System.Text.UTF8Encoding($false)).GetBytes($json))
    return [pscustomobject]@{ Path = $path; Evidence = $evidence }
}

function Rewrite-PreflightManifestBundle {
    param(
        [Parameter(Mandatory)][string]$EvidenceDirectory,
        [Parameter(Mandatory)][scriptblock]$Transform
    )
    $manifestPath = Join-Path $EvidenceDirectory 'manifest.json'
    $sidecarPath = Join-Path $EvidenceDirectory 'manifest.sha256'
    $manifestBytes = [System.IO.File]::ReadAllBytes($manifestPath)
    $manifest = [System.Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json -ErrorAction Stop
    [void](& $Transform $manifest)
    $json = ConvertTo-Json -InputObject $manifest -Depth 20 -Compress
    $bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($json)
    [System.IO.File]::WriteAllBytes($manifestPath, $bytes)
    [System.IO.File]::WriteAllBytes($sidecarPath, [System.Text.Encoding]::ASCII.GetBytes((Get-Sha256Hex $bytes)))
    return $manifest
}

function Assert-FileRecordMatches {
    param(
        [Parameter(Mandatory)]$Record,
        [Parameter(Mandatory)][string]$ExpectedPath,
        [Parameter(Mandatory)][string]$Context
    )
    $resolved = [System.IO.Path]::GetFullPath($ExpectedPath)
    Assert-Equal $resolved ([System.IO.Path]::GetFullPath([string]$Record.path)) "$Context path"
    $bytes = [System.IO.File]::ReadAllBytes($resolved)
    Assert-Equal $bytes.Length ([long]$Record.length) "$Context length"
    Assert-Equal (Get-Sha256Hex $bytes) ([string]$Record.sha256) "$Context SHA-256"
}

function Move-PreflightEvidenceFixture {
    param(
        [Parameter(Mandatory)][string]$SourceDirectory,
        [Parameter(Mandatory)][string]$DestinationDirectory,
        [string]$RecordedRoot
    )
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $DestinationDirectory)) | Out-Null
    [System.IO.Directory]::Move($SourceDirectory, $DestinationDirectory)
    $newSnapshot = Join-Path $DestinationDirectory 'ffx-hooks.ini.snapshot.bin'
    [void](Rewrite-PreflightManifestBundle -EvidenceDirectory $DestinationDirectory -Transform {
        param($manifest)
        $manifest.snapshot.path = [System.IO.Path]::GetFullPath($newSnapshot)
        $manifest.evidence.directory = [System.IO.Path]::GetFullPath($DestinationDirectory)
        if (-not [string]::IsNullOrWhiteSpace($RecordedRoot)) {
            if ($null -eq $manifest.evidence.PSObject.Properties['root']) {
                $manifest.evidence | Add-Member -NotePropertyName root -NotePropertyValue ([System.IO.Path]::GetFullPath($RecordedRoot))
            } else {
                $manifest.evidence.root = [System.IO.Path]::GetFullPath($RecordedRoot)
            }
        }
    }.GetNewClosure())
    return [System.IO.Path]::GetFullPath($DestinationDirectory)
}

function Invoke-F8Rt0MutationProbe {
    param(
        [Parameter(Mandatory)][string]$Mutation,
        [Parameter(Mandatory)][string]$FixtureRoot,
        [Parameter(Mandatory)][string]$ScriptPath,
        [Parameter(Mandatory)][string]$ModulePath,
        [Parameter(Mandatory)][string]$HarnessPath,
        [Parameter(Mandatory)][string]$ReadmePath
    )
    $mutationRoot = Join-Path $FixtureRoot ("mutation-$Mutation-$([Guid]::NewGuid().ToString('N'))")
    [System.IO.Directory]::CreateDirectory($mutationRoot) | Out-Null
    $mutantHarness = $HarnessPath
    $mutantModule = $ModulePath
    $mutantReadme = $ReadmePath
    $expectedFailure = ''

    if ($Mutation -eq 'ForbiddenTokenRemoval') {
        $source = [System.IO.File]::ReadAllText($HarnessPath)
        $old = '$script:ForbiddenCommands = @(''Start-Process'', ''Stop-Process'', ''Copy-Item'', ''Start-Sleep'')'
        $new = '$script:ForbiddenCommands = @(''Stop-Process'', ''Copy-Item'', ''Start-Sleep'')'
        $mutated = $source.Replace($old, $new)
        if ($mutated -ceq $source) { throw 'ForbiddenTokenRemoval mutation anchor was not found' }
        $mutantHarness = Join-Path $mutationRoot 'run_f8_rt2_rt0_mutant.ps1'
        [System.IO.File]::WriteAllBytes($mutantHarness, (New-Object System.Text.UTF8Encoding($false)).GetBytes($mutated))
        $expectedFailure = 'static safety denylist retains every forbidden command token'
    } elseif ($Mutation -eq 'ReadmeAbsoluteSafetyRevert') {
        $source = [System.IO.File]::ReadAllText($ReadmePath)
        # Keep this historical probe focused on the scoped default-safety contract.
        $oldSafety = 'The 13 F8 gameplay mutation targets are OFF by default.'
        $newSafety = 'All hooks are OFF by default. Nothing touches your game until you arm a flag.'
        $mutated = $source.Replace($oldSafety, $newSafety)
        if ($mutated -ceq $source) { throw 'ReadmeAbsoluteSafetyRevert mutation anchor was not found' }
        $mutantReadme = Join-Path $mutationRoot 'README-mutant.md'
        [System.IO.File]::WriteAllBytes($mutantReadme, (New-Object System.Text.UTF8Encoding($false)).GetBytes($mutated))
        $expectedFailure = 'README lacks the scoped gameplay-target/dashboard-adapter safety contract'
    } elseif ($Mutation -eq 'ReadmeUniversalSignatureRevert') {
        $source = [System.IO.File]::ReadAllText($ReadmePath)
        # Keep this historical probe focused on the Compatibility signature-safety boundary.
        $lineEnding = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
        # Replace only the scoped guarantee. The feature-specific paragraphs intentionally evolve as
        # reverse-engineering evidence improves, while this universal-overclaim mutant must stay stable.
        $old = @(
            '- Only specific supported-profile/expected-byte inline-patch families fail closed on mismatch;',
            '  that guarantee is scoped to those validated sites.'
        ) -join $lineEnding
        $new = @(
            '- Hooks install **only if the target byte signature matches** (signature-validated patching).',
            '  On mismatch, the hook stays off and logs — it never corrupts the game.'
        ) -join $lineEnding
        $mutated = $source.Replace($old, $new)
        if ($mutated -ceq $source) { throw 'ReadmeUniversalSignatureRevert mutation anchor was not found' }
        $mutantReadme = Join-Path $mutationRoot 'README-signature-mutant.md'
        [System.IO.File]::WriteAllBytes($mutantReadme, (New-Object System.Text.UTF8Encoding($false)).GetBytes($mutated))
        $expectedFailure = 'README lacks the scoped inline-patch mismatch guarantee'
    } else {
        $source = [System.IO.File]::ReadAllText($ModulePath)
        $mutated = $null
        switch ($Mutation) {
            'ResolverOrdering' {
                $old = 'if ($environmentValue.Valid) {'
                $new = 'if ($environmentValue.Valid -and $environmentValue.Value) {'
                $expectedFailure = 'resolver mirrors disable, off flags, positive env true/false, INI, flag, and default precedence'
            }
            'TextModeSnapshot' {
                $old = 'Write-F8BytesCreateNew -Path $snapshotPath -Bytes $IniBytes'
                $new = '$normalized = [System.Text.Encoding]::UTF8.GetBytes([System.Text.Encoding]::UTF8.GetString($IniBytes).Replace("`r`n", "`n")); Write-F8BytesCreateNew -Path $snapshotPath -Bytes $normalized'
                $expectedFailure = 'snapshot preserves BOM, CRLF, and no-final-newline bytes exactly'
            }
            'AcceptTruncatedLog' {
                $old = 'throw "log was truncated or rotated: current length $($stream.Length) < start length $StartLength"'
                $new = 'return [pscustomobject]@{ Bytes = [byte[]]@(); Length = 0; Sha256 = Get-F8Sha256Hex -Bytes ([byte[]]@()) }'
                $expectedFailure = 'log slicing is byte-exact, ignores old errors, and rejects truncation/prefix mismatch/counter drift'
            }
            'OtherTargetOn' {
                $old = 'throw "other target ON request found: $editKey"'
                $new = '$null = $editKey'
                $expectedFailure = 'log rejects wrong selected effective/source and any other-target ON or apply'
            }
            'AtomicReplaceBypass' {
                $old = '$moved = [bool](& $MoveProvider $temporaryPath $DestinationPath)'
                $new = '[System.IO.File]::WriteAllBytes($DestinationPath, $snapshotBytes); [System.IO.File]::Delete($temporaryPath); $moved = $true'
                $expectedFailure = 'atomic restore failure preserves destination and cleans only its owned temp'
            }
            'ProcessRecheckBypass' {
                $old = '$beforeReplacePassed = [bool](& $BeforeReplaceProvider $temporaryPath $DestinationPath)'
                $new = '$beforeReplacePassed = $true'
                $expectedFailure = 'Verify process recheck occurs after flushed temp creation and immediately before replace'
            }
            'SnapshotManifestLinkBypass' {
                $old = 'if ($expectedHash -cne $iniHash -or $expectedLength -ne $iniLength) {'
                $new = 'if ($false) {'
                $expectedFailure = 'partial snapshot relabeling that omits paths.ini is rejected before temp or move'
            }
            'EvidenceRootContainmentBypass' {
                $old = 'if (-not $parent.Equals($root, [System.StringComparison]::OrdinalIgnoreCase)) {'
                $new = 'if ($false) {'
                $expectedFailure = 'Verify requires EvidenceDirectory to be a direct child with the exact leaf form'
            }
            'StructuredLogValidationBypass' {
                $mutated = $source
                $mutated = $mutated.Replace("if (`$editCode -cne 'SAVED') {", 'if ($false) {')
                $mutated = $mutated.Replace("if (`$failureMatch.Success -and `$failureMatch.Groups[2].Value -ine 'none') {", 'if ($false) {')
                $mutated = $mutated.Replace(' F8 edit key=$selected edit=SAVED requested=1\b', ' F8 edit key=$selected .*\brequested=1\b')
                $mutated = $mutated.Replace(' F8 edit key=$selected edit=SAVED requested=0\b', ' F8 edit key=$selected .*\brequested=0\b')
                $expectedFailure = 'selected ON and OFF anchors require edit SAVED and reject every production edit failure'
            }
            'FinalVerifyManifestOmission' {
                $mutated = $source.Replace("        restorationVerdict = 'restoration-verdict.json'", '')
                $expectedFailure = 'Verify writes a versioned final manifest after all exact artifacts and then its sidecar'
            }
            'IniParserLimitsBypass' {
                $mutated = $source
                $mutated = $mutated.Replace("if (`$Bytes.Length -eq 0) { throw 'empty INI input is rejected by the runtime loader' }", 'if ($false) { throw ''disabled'' }')
                $mutated = $mutated.Replace("if (`$Bytes.Length -gt 65535) { throw 'INI input exceeds the 65535-byte runtime limit' }", 'if ($false) { throw ''disabled'' }')
                $mutated = $mutated.Replace("if (`$parsedPairCount -ge 256) { throw 'INI parsed pair count exceeds the 256-pair runtime limit' }", 'if ($false) { throw ''disabled'' }')
                $mutated = $mutated.Replace("if ([System.Text.Encoding]::UTF8.GetByteCount(`$flat) -ge 128) { throw 'INI flat key reaches the 128-byte runtime limit' }", 'if ($false) { throw ''disabled'' }')
                $mutated = $mutated.Replace("if ([System.Text.Encoding]::UTF8.GetByteCount(`$value) -ge 512) { throw 'INI value reaches the 512-byte runtime limit' }", 'if ($false) { throw ''disabled'' }')
                $expectedFailure = 'INI parser requires nonempty input'
            }
            'PreflightIdentityPinBypass' {
                $old = 'foreach ($entry in $recordsToValidate) {'
                $new = 'foreach ($entry in @()) {'
                $expectedFailure = 'Verify rejects post-validation Preflight identity drift before final evidence success'
            }
            'InitialManifestPinWindowBypass' {
                $old = '$preflightManifestPin = $manifestEnvelope.ManifestRecord'
                $new = '$preflightManifestPin = Get-F8FileIntegrityRecord -Path ([string]$manifestEnvelope.ManifestRecord.path)'
                $expectedFailure = 'Verify consumes the same-read Preflight envelope without an initial pin reread'
            }
            default { throw "unsupported mutation probe: $Mutation" }
        }
        if ($null -eq $mutated) { $mutated = $source.Replace($old, $new) }
        if ($mutated -ceq $source) { throw "$Mutation mutation anchor was not found" }
        $mutantModule = Join-Path $mutationRoot 'run_f8_rt2_lib_mutant.psm1'
        [System.IO.File]::WriteAllBytes($mutantModule, (New-Object System.Text.UTF8Encoding($false)).GetBytes($mutated))
    }

    # Route the child Information stream so its FAIL text participates in discrimination.
    $quotedHarness = $mutantHarness.Replace("'", "''")
    $quotedScript = $ScriptPath.Replace("'", "''")
    $quotedModule = $mutantModule.Replace("'", "''")
    $quotedReadme = $mutantReadme.Replace("'", "''")
    $childCommand = "& '$quotedHarness' -ScriptPath '$quotedScript' -ModulePath '$quotedModule' -ReadmePath '$quotedReadme' 6>&1"
    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $childCommand 2>&1
    $exitCode = $LASTEXITCODE
    # Keep the complete expected assertion text; Out-String's default width can truncate it.
    $text = $output | Out-String -Width 4096
    # Child host formatting can wrap a long assertion, and Test-Case prefixes its failure text.
    $normalizedText = [regex]::Replace($text, '\s+', ' ')
    $normalizedExpectedFailure = [regex]::Replace($expectedFailure, '\s+', ' ')
    [pscustomobject]@{
        Detected = [bool]($exitCode -ne 0 -and $normalizedText.Contains($normalizedExpectedFailure))
        ExitCode = $exitCode
        ExpectedFailure = $expectedFailure
        Output = $text
    }
}

function Get-UniqueBoundedMarkdownSection {
    param(
        [string]$Text,
        [string]$Heading,
        [ValidateRange(1, 6)]
        [int]$Level
    )
    $escapedHeading = [regex]::Escape($Heading)
    $headingMatches = [regex]::Matches($Text, '(?m)^' + $escapedHeading + '\r?$')
    if ($headingMatches.Count -ne 1) {
        throw "Markdown authority heading must occur exactly once: $Heading"
    }
    # WHY: validate only the selected authority block so historical sections cannot
    # satisfy current claims or trigger false contradictions elsewhere in the file.
    $pattern = '(?ms)^' + $escapedHeading + '\r?\n.*?(?=^#{1,' + $Level + '} |\z)'
    $sectionMatches = [regex]::Matches($Text, $pattern)
    if ($sectionMatches.Count -ne 1) {
        throw "Markdown authority section could not be bounded uniquely: $Heading"
    }
    return $sectionMatches[0].Value
}

function Assert-UniqueBoundaryField {
    param([string]$Text, [string]$Label, [string]$ExactPattern)
    $labelPattern = '(?im)^(?:- )?\*\*' + [regex]::Escape($Label) + ':\*\*'
    if ([regex]::Matches($Text, $labelPattern).Count -ne 1) {
        throw "Maechen authority field must occur exactly once: $Label"
    }
    if ([regex]::Matches($Text, $ExactPattern).Count -ne 1) {
        throw "Maechen authority field has a missing or contradictory value: $Label"
    }
}

function Assert-MaechenOfflineBoundary {
    param(
        [string]$Text,
        [switch]$RequireEndpointField,
        [switch]$RequireIniBlock,
        [switch]$RequireHostMarkers,
        [switch]$RequireCanonicalProduction,
        [switch]$RequireClientCredentialDeclaration,
        [switch]$RequireHandoffCredentialDeclaration
    )
    if ([string]::IsNullOrWhiteSpace($Text) -or $Text.Length -gt 65535) {
        throw 'Maechen authority section is empty or exceeds its bounded test budget'
    }

    Assert-UniqueBoundaryField $Text 'Hooks runtime-source head' '(?im)^(?:- )?\*\*Hooks runtime-source head:\*\* `bf75747cc5d7ee3dfe70c50ba1728081d7df6e50`\r?$'
    if ($RequireCanonicalProduction) {
        # WHY: canonical Vercel production and Firebase mirror parity are independent facts;
        # accepting a generic "published" label would hide the known stale mirror.
        Assert-UniqueBoundaryField $Text 'Website source commit' '(?im)^\*\*Website source commit:\*\* `10c8d491d190644fcdc266d69d8e38be15cfbc19`\r?$'
        Assert-UniqueBoundaryField $Text 'Website production merge' '(?im)^\*\*Website production merge:\*\* `9ceb69aa2c44652020339c33884af761356e356a` \(PR #38 squash\)\r?$'
        Assert-UniqueBoundaryField $Text 'Vercel production deployment' '(?im)^\*\*Vercel production deployment:\*\* `dpl_CRTwTmHngZxrAkMqT2Pr4PbpnPPh` \(`Ready`\)\r?$'
        Assert-UniqueBoundaryField $Text 'Server release SHA' '(?im)^\*\*Server release SHA:\*\* `9ceb69aa2c44652020339c33884af761356e356a`\r?$'
        Assert-UniqueBoundaryField $Text 'Endpoint status' '(?im)^\*\*Endpoint status:\*\* `canonical Vercel production route live-verified; Firebase mirror stale`\r?$'
    } else {
        Assert-UniqueBoundaryField $Text 'Website source commits' '(?im)^(?:- )?\*\*Website source commits:\*\* `c6b3f04acdb22f9dacec78a8bb20048daaee4039`, `cc623804f67e3c49256b29d5ef6bef3f848a41e8`, `6a85ca5ddf2e60be4f4a1d6598e24a02c13103a8`, and `253639e4425259fec60cd6500ef1322e2f7eadce`\r?$'
        Assert-UniqueBoundaryField $Text 'Server release SHA' '(?im)^(?:- )?\*\*Server release SHA:\*\* `pending`\r?$'
        Assert-UniqueBoundaryField $Text 'Endpoint status' '(?im)^(?:- )?\*\*Endpoint status:\*\* `unpublished / not live-verified`\r?$'
    }
    if ($RequireEndpointField) {
        Assert-UniqueBoundaryField $Text 'Endpoint' '(?im)^\*\*Endpoint:\*\* `https://ffxmodstudio\.com/api/maechen/game/v1`\r?$'
    }

    if ($Text -notmatch '(?is)public route.{0,180}no\s+client\s+authentication' -or
        $Text -notmatch '(?is)\bdefault OFF\b.{0,240}\[maechen\].{0,120}enabled\s*=\s*0.{0,120}locale\s*=\s*pt' -or
        $Text -notmatch '(?is)`Maechen is thinking\.\.\.`.{0,180}local wait label.{0,180}not (?:model )?chain-of-thought') {
        throw 'Maechen boundary lacks public/no-auth, default-OFF, or local-wait truth'
    }
    # WHY: general no-auth wording cannot substitute for each authority surface's
    # explicit declaration that client/provider credentials are absent.
    if ($RequireClientCredentialDeclaration) {
        $clientDeclaration = '(?is)This is a public route; no client authentication is sent\.\s+The DLL contains no\s+provider key, token, cookie, or Authorization header\.'
        if ([regex]::Matches($Text, $clientDeclaration).Count -ne 1) {
            throw 'Maechen client authority lacks its explicit no-provider-credential declaration'
        }
    }
    if ($RequireHandoffCredentialDeclaration) {
        $handoffDeclaration = '(?is)There is no endpoint override or client\s+credential\.'
        if ([regex]::Matches($Text, $handoffDeclaration).Count -ne 1) {
            throw 'Maechen handoff authority lacks its explicit no-override/client-credential declaration'
        }
    }

    $contradictions = @(
        '(?im)^(?:[-*]\s*)?(?:(?:external\s+)?configurable|external)\s+(?:URL|URI|host|origin|endpoint)\s*:\s*\S+',
        '(?im)^(?:[-*]\s*)?(?:(?:provider|client|Maechen)\s+)?(?:API\s+)?(?:key|token|credential)\s*:\s*\S+',
        '(?im)^(?:[-*]\s*)?client\s+(?:Authorization|cookie)\s*:\s*\S+',
        '(?im)^(?:[-*]\s*)?(?:(?:plain\s+)?F9\s+(?:is\s+)?(?:enabled|ON)\s+by default|default\s+F9\s*:\s*(?:enabled|ON))\b',
        '(?im)^(?:[-*]\s*)?(?:(?:model\s+)?chain-of-thought|provider\s+reasoning)\s+(?:is\s+)?(?:shown|displayed|rendered|returned)\b'
    )
    if (-not $RequireCanonicalProduction) {
        $contradictions += '(?im)^(?:[-*]\s*)?(?:Maechen\s+)?(?:endpoint|server|service)\s+is\s+(?:live|published|production-ready)\b'
    }
    foreach ($contradiction in $contradictions) {
        if ($Text -match $contradiction) {
            throw "Maechen boundary contains a positive contradictory claim: $contradiction"
        }
    }

    if ($RequireIniBlock) {
        $configMatches = [regex]::Matches($Text, '(?ms)^```ini\r?\n(?<config>.*?)^```\r?$')
        if ($configMatches.Count -ne 1) { throw 'Maechen authority must contain one exact INI block' }
        $configLines = @($configMatches[0].Groups['config'].Value -split '\r?\n' |
            ForEach-Object { $_.Trim() } | Where-Object { $_ })
        $expectedConfig = @('[maechen]', 'enabled = 0', 'locale = pt')
        if (Compare-Object $expectedConfig $configLines -SyncWindow 0) {
            throw 'Maechen INI block is not the exact default-OFF two-key template'
        }
    }

    if ($RequireHostMarkers) {
        $markerPattern = '(?im)^- \[(?<state>[ xX])\] `https://(?<host>[^/`]+)/release\.json` - (?<detail>[^\r\n]+)\r?$'
        $markers = [regex]::Matches($Text, $markerPattern)
        if ($markers.Count -ne 3) { throw 'Maechen release proof must contain exactly three host markers' }
        $actualHosts = @()
        foreach ($marker in $markers) {
            $actualHosts += $marker.Groups['host'].Value
        }
        $expectedHosts = @('ffxmodstudio.com', 'ffx-mod-website.vercel.app', 'ffx-mod-studio.web.app')
        if (Compare-Object $expectedHosts $actualHosts -SyncWindow 0) {
            throw 'Maechen release proof hosts are missing, duplicated, or reordered'
        }
        $expectedStates = if ($RequireCanonicalProduction) { @('x', 'x', ' ') } else { @(' ', ' ', ' ') }
        $actualStates = @($markers | ForEach-Object { $_.Groups['state'].Value.ToLowerInvariant() })
        if (Compare-Object $expectedStates $actualStates -SyncWindow 0) {
            throw 'Maechen release proof checked states contradict canonical production or mirror parity'
        }
        if ($RequireCanonicalProduction) {
            if ($markers[0].Groups['detail'].Value -cne '`9ceb69aa2c44652020339c33884af761356e356a`' -or
                $markers[1].Groups['detail'].Value -cne 'same Vercel production release' -or
                $markers[2].Groups['detail'].Value -notmatch '^stale `9ce32532fd3c1985df434574d07e70284b0445d9`; Firebase sync was blocked by .+ GitHub Actions billing/spending gate$') {
                throw 'Maechen release proof details do not preserve exact production and stale-mirror provenance'
            }
        }
    }
}

if ($ScriptPath) {
    # Mutant harnesses live under a disposable temp directory; the supplied script keeps their docs rooted in the real repository.
    $repoScriptRoot = Split-Path -Parent ([System.IO.Path]::GetFullPath($ScriptPath))
} else {
    $repoScriptRoot = Split-Path -Parent $PSScriptRoot
    $ScriptPath = Join-Path $repoScriptRoot 'run_f8_rt2.ps1'
}
if (-not $ModulePath) { $ModulePath = Join-Path $repoScriptRoot 'run_f8_rt2_lib.psm1' }
if (-not $ReadmePath) {
    $ReadmePath = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $repoScriptRoot '..\..\..'))) 'README.md'
}

$tempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
$guid = [Guid]::NewGuid().ToString('N').Substring(0, 16)
$fixtureName = "f8r-$guid"
$script:FixtureRoot = [System.IO.Path]::Combine($tempBase, $fixtureName)
$resolvedFixture = [System.IO.Path]::GetFullPath($script:FixtureRoot)
$expectedPrefix = $tempBase + '\f8r-'
if (-not $resolvedFixture.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $resolvedFixture) -ne $fixtureName) {
    throw "unsafe RT0 fixture root: $resolvedFixture"
}
[System.IO.Directory]::CreateDirectory($resolvedFixture) | Out-Null

try {
    if (Test-Path -LiteralPath $ModulePath -PathType Leaf) {
        $script:ImportedModule = Import-Module -Name $ModulePath -Force -PassThru -ErrorAction Stop
        $script:ModuleImported = $true
    }

    $parsedScript = Parse-ScriptAst -Path $ScriptPath

    Test-Case 'main script parses without errors and is never executed by this harness' {
        Assert-Equal 0 $parsedScript.Errors.Count 'PowerShell parser errors'
        $parsedHarness = Parse-ScriptAst -Path $script:HarnessPath
        Assert-Equal 0 $parsedHarness.Errors.Count 'harness parser errors'
        $dotInvocations = @($parsedHarness.Ast.FindAll(
            { param($node) $node -is [System.Management.Automation.Language.CommandAst] -and $node.InvocationOperator -eq [System.Management.Automation.Language.TokenKind]::Dot },
            $true
        ))
        Assert-Equal 0 $dotInvocations.Count 'harness must not dot-source any script'
    }

    Test-Case 'main and helper forbid process launch, process stop, copy, and boot sleep commands' {
        $commands = New-Object System.Collections.Generic.List[string]
        foreach ($name in (Get-CommandNamesFromAst -Ast $parsedScript.Ast)) { $commands.Add($name) }
        if (Test-Path -LiteralPath $ModulePath -PathType Leaf) {
            $parsedModule = Parse-ScriptAst -Path $ModulePath
            Assert-Equal 0 $parsedModule.Errors.Count 'helper module parser errors'
            foreach ($name in (Get-CommandNamesFromAst -Ast $parsedModule.Ast)) { $commands.Add($name) }
        }
        $hits = @($commands | Where-Object { $script:ForbiddenCommands -contains $_ } | Sort-Object -Unique)
        Assert-Equal @() $hits 'forbidden command AST nodes'
    }

    Test-Case 'static safety denylist retains every forbidden command token' {
        $expected = @('Copy-Item', 'Start-Process', 'Start-Sleep', 'Stop-Process')
        Assert-Equal $expected @($script:ForbiddenCommands | Sort-Object) 'static forbidden-command denylist'
    }

    Test-Case 'main and helper forbid text-mode config APIs and recursive directory hashing' {
        $sources = Get-Content -Raw -LiteralPath $ScriptPath
        if (Test-Path -LiteralPath $ModulePath -PathType Leaf) {
            $sources += "`n" + (Get-Content -Raw -LiteralPath $ModulePath)
        }
        foreach ($token in @('Set-Content', 'Out-File')) {
            Assert-False ($sources -match "(?im)\b$([regex]::Escape($token))\b") "forbidden text-mode writer $token"
        }
        Assert-False ($sources -match '(?im)Get-FileHash[^\r\n]*(?:-Recurse|\*\*)') 'recursive directory hash is forbidden'
    }

    Test-Case 'main exposes the exact mandatory Case and Phase protocol interface' {
        $parameters = @($parsedScript.Ast.ParamBlock.Parameters)
        $names = @($parameters | ForEach-Object { $_.Name.VariablePath.UserPath })
        $expectedNames = @(
            'Case', 'Phase', 'DisposableSaveConfirmed', 'EditorClosedConfirmed',
            'ObservedApplied', 'ObservedRestored', 'RestoreConfigSnapshot',
            'SeymourEvidencePath',
            'EvidenceDirectory', 'EvidenceRoot'
        )
        Assert-Equal $expectedNames $names 'parameter names/order'

        foreach ($mandatoryName in @('Case', 'Phase', 'DisposableSaveConfirmed', 'EditorClosedConfirmed')) {
            $parameter = $parameters | Where-Object { $_.Name.VariablePath.UserPath -eq $mandatoryName }
            $parameterText = $parameter.Extent.Text
            Assert-True ($parameterText -match '(?is)Parameter\s*\(\s*Mandatory') "$mandatoryName must be mandatory"
        }
        foreach ($switchName in @('DisposableSaveConfirmed', 'EditorClosedConfirmed', 'ObservedApplied', 'ObservedRestored', 'RestoreConfigSnapshot')) {
            $parameter = $parameters | Where-Object { $_.Name.VariablePath.UserPath -eq $switchName }
            Assert-True ($parameter.Extent.Text -match '(?is)\[switch\]') "$switchName must be a switch"
        }
    }

    Test-Case 'Case ValidateSet contains exactly the 14 allowed names and Phase exactly two phases' {
        $parameters = @($parsedScript.Ast.ParamBlock.Parameters)
        $caseParameter = $parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Case' }
        $phaseParameter = $parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Phase' }
        Assert-True ($null -ne $caseParameter) 'Case parameter missing'
        Assert-True ($null -ne $phaseParameter) 'Phase parameter missing'
        $caseAttribute = $caseParameter.Attributes | Where-Object { $_.TypeName.FullName -eq 'ValidateSet' }
        $phaseAttribute = $phaseParameter.Attributes | Where-Object { $_.TypeName.FullName -eq 'ValidateSet' }
        $caseValues = @($caseAttribute.PositionalArguments | ForEach-Object { $_.SafeGetValue() })
        $phaseValues = @($phaseAttribute.PositionalArguments | ForEach-Object { $_.SafeGetValue() })
        Assert-Equal @((Get-ExpectedCases).Case) $caseValues 'Case ValidateSet'
        Assert-Equal @('Preflight', 'Verify') $phaseValues 'Phase ValidateSet'
    }

    Test-Case 'case table has the exact 14 case-to-key-to-mode mappings' {
        Require-Library
        $actual = @(Get-F8Rt2CaseTable)
        $expected = @(Get-ExpectedCases)
        Assert-Equal 14 $actual.Count 'case table count'
        for ($i = 0; $i -lt $expected.Count; ++$i) {
            Assert-Equal $expected[$i].Case $actual[$i].Case "case[$i]"
            Assert-Equal $expected[$i].Canonical $actual[$i].Canonical "canonical[$i]"
            Assert-Equal $expected[$i].ApplyMode $actual[$i].ApplyMode "apply mode[$i]"
            Assert-False ([bool]$actual[$i].DefaultValue) "default must be false for $($actual[$i].Case)"
        }
        Assert-Equal 11 @($actual | Where-Object ApplyMode -eq 'RuntimeAcknowledged').Count 'RuntimeAcknowledged count'
        Assert-Equal 3 @($actual | Where-Object ApplyMode -eq 'ConfigPolled').Count 'ConfigPolled count'
        $ap = @($actual | Where-Object Case -ceq 'ap_100x')[0]
        $gil = @($actual | Where-Object Case -ceq 'gil_100x')[0]
        Assert-Equal 'cheats.ap_multiplier' $ap.ScalarKey 'AP scalar key'
        Assert-Equal 'cheats.gil_multiplier' $gil.ScalarKey 'Gil scalar key'
        foreach ($spec in @($ap, $gil)) {
            Assert-Equal 100 $spec.ScalarDefault 'scalar default'
            Assert-Equal 1 $spec.ScalarMinimum 'scalar minimum'
            Assert-Equal 100 $spec.ScalarMaximum 'scalar maximum'
            Assert-TextMatch ([string]$spec.ManualScope) 'baseline.*non-default multiplier.*OFF restoration' 'scalar RT2 scope'
        }
        Assert-Equal 2 @($actual | Where-Object { -not [string]::IsNullOrEmpty([string]$_.ScalarKey) }).Count 'scalar case count'

        $seymour = @($actual | Where-Object Case -ceq 'seymour_battle_roster')[0]
        Assert-TextMatch ([string]$seymour.ManualScope) '(?is)battle roster.*Sphere Grid.*persistent.*local.*Switch' 'Seymour battle-only RT2 scope'
        $seymourSteps = @(Get-F8ManualSteps -Spec $seymour)
        Assert-Equal 7 $seymourSteps.Count 'Seymour manual step count'
        Assert-TextMatch ($seymourSteps -join "`n") '(?is)module base.*before.*ON.*selection.*controlled turn.*afterSwitch.*OFF.*normal.*exit.*nextBattle.*no reintroduction.*JSON.*SeymourEvidencePath.*close' 'Seymour exact five-phase manual workflow'
        Assert-Equal 'ffx-hooks.seymour-battle-memory/v1' $seymour.RuntimeEvidenceSchema 'Seymour runtime evidence schema'
        Assert-Equal 4 @($seymour.RuntimeLeaves).Count 'Seymour runtime leaf count'
        $expectedLeaves = @(
            [pscustomobject]@{ Name = 'persistentState'; Rva = '0x00D307E8'; Size = 3 },
            [pscustomobject]@{ Name = 'persistentAbility'; Rva = '0x00D307EB'; Size = 17 },
            [pscustomobject]@{ Name = 'localState'; Rva = '0x00D2C895'; Size = 7 },
            [pscustomobject]@{ Name = 'localAbility'; Rva = '0x00D2C8A3'; Size = 17 }
        )
        for ($i = 0; $i -lt $expectedLeaves.Count; ++$i) {
            Assert-Equal $expectedLeaves[$i].Name $seymour.RuntimeLeaves[$i].Name "Seymour runtime leaf[$i] name"
            Assert-Equal $expectedLeaves[$i].Rva $seymour.RuntimeLeaves[$i].Rva "Seymour runtime leaf[$i] RVA"
            Assert-Equal $expectedLeaves[$i].Size $seymour.RuntimeLeaves[$i].Size "Seymour runtime leaf[$i] size"
        }

        $validEvidence = New-SeymourMemoryEvidenceFixture
        $validated = Read-F8SeymourMemoryEvidence -Path $validEvidence.Path
        Assert-True ([bool]$validated.Valid) 'valid Seymour raw-memory evidence was rejected'
        Assert-Equal '0x00400000' $validated.Executable.ModuleBase 'validated module base'
        Assert-Equal 4 @($validated.Leaves).Count 'validated Seymour leaf count'
        Assert-Equal 'restored' $validated.Lifecycle.persistentOff 'persistent OFF lifecycle verdict'
        Assert-Equal 'restore-pending-current-battle-retained' $validated.Lifecycle.localOff 'retained local OFF lifecycle verdict'
        Assert-Equal 'clean-no-reintroduction' $validated.Lifecycle.nextBattle 'next-battle lifecycle verdict'

        $cleanLocalOff = New-SeymourMemoryEvidenceFixture -Transform {
            param($proof)
            $proof.leaves[2].snapshots.off = $proof.leaves[2].snapshots.before
            $proof.leaves[3].snapshots.off = $proof.leaves[3].snapshots.before
        }
        $cleanLocalValidated = Read-F8SeymourMemoryEvidence -Path $cleanLocalOff.Path
        Assert-Equal 'clean' $cleanLocalValidated.Lifecycle.localOff 'already-clean local OFF lifecycle verdict'

        $wrongVa = New-SeymourMemoryEvidenceFixture -Transform { param($proof) $proof.leaves[0].runtimeVa = $proof.leaves[0].rva }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $wrongVa.Path | Out-Null } 'runtime VA|module base|RVA' 'RVA presented as runtime VA was accepted'
        $missingPhase = New-SeymourMemoryEvidenceFixture -Transform { param($proof) $proof.leaves[1].snapshots.Remove('nextBattle') }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $missingPhase.Path | Out-Null } 'nextBattle|snapshot|missing|exact' 'missing next-battle snapshot was accepted'
        $ambiguous = New-SeymourMemoryEvidenceFixture -Transform { param($proof) $proof.leaves[2].snapshots.on = '00 01 02 03 04 05 ??' }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $ambiguous.Path | Out-Null } 'byte|snapshot|hex|ambiguous' 'ambiguous raw bytes were accepted'
        $noSwitchDelta = New-SeymourMemoryEvidenceFixture -Transform {
            param($proof)
            $proof.leaves[2].snapshots.afterSwitch = $proof.leaves[2].snapshots.on
            $proof.leaves[3].snapshots.afterSwitch = $proof.leaves[3].snapshots.on
        }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $noSwitchDelta.Path | Out-Null } 'Switch|different|order|equal' 'equal ON/after-Switch record was accepted'
        $missingTurn = New-SeymourMemoryEvidenceFixture -Transform { param($proof) $proof.observations.controlledTurnCompleted = $false }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $missingTurn.Path | Out-Null } 'controlledTurnCompleted|observation|true' 'missing controlled-turn observation was accepted'
        $persistentOffStillOwned = New-SeymourMemoryEvidenceFixture -Transform {
            param($proof)
            $proof.leaves[1].snapshots.off = $proof.leaves[3].snapshots.afterSwitch
        }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $persistentOffStillOwned.Path | Out-Null } 'OFF persistent|restored|actor ID 7|baseline' 'persistent OFF still containing Seymour was accepted'
        $localOffMembershipDrift = New-SeymourMemoryEvidenceFixture -Transform {
            param($proof)
            $proof.leaves[2].snapshots.off = '00 01 02 03 04 05 08'
        }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $localOffMembershipDrift.Path | Out-Null } 'OFF local|membership|retained|baseline' 'foreign local OFF membership drift was accepted'
        $localOffDuplicate = New-SeymourMemoryEvidenceFixture -Transform {
            param($proof)
            $proof.leaves[2].snapshots.off = '00 01 02 03 04 07 07'
        }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $localOffDuplicate.Path | Out-Null } 'OFF local|membership|retained|baseline' 'duplicate Seymour in local OFF was accepted'
        $nextBattleReintroduced = New-SeymourMemoryEvidenceFixture -Transform {
            param($proof)
            $proof.leaves[2].snapshots.nextBattle = $proof.leaves[2].snapshots.off
            $proof.leaves[3].snapshots.nextBattle = $proof.leaves[3].snapshots.off
        }
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $nextBattleReintroduced.Path | Out-Null } 'next-battle|reintroduction|actor ID 7|baseline' 'next-battle Seymour reintroduction was accepted'
        $duplicatePath = Join-Path $script:FixtureRoot ('seymour-memory-duplicate-' + [Guid]::NewGuid().ToString('N') + '.json')
        $validText = [System.IO.File]::ReadAllText($validEvidence.Path)
        $duplicateText = [regex]::Replace(
            $validText,
            '"schema"\s*:\s*"ffx-hooks\.seymour-battle-memory/v1"',
            '"schema": "ambiguous", "schema": "ffx-hooks.seymour-battle-memory/v1"',
            1)
        Assert-False ($duplicateText -ceq $validText) 'duplicate-property fixture anchor was not found'
        [System.IO.File]::WriteAllBytes($duplicatePath, (New-Object System.Text.UTF8Encoding($false)).GetBytes($duplicateText))
        Assert-Throws { Read-F8SeymourMemoryEvidence -Path $duplicatePath | Out-Null } 'duplicate|ambiguous|schema' 'duplicate JSON property was accepted'
    }

    Test-Case 'scalar manual steps require baseline, one non-default Rate, ON, OFF, and close' {
        Require-Library
        foreach ($caseName in @('ap_100x', 'gil_100x')) {
            $spec = Get-F8CaseSpec -Case $caseName
            $steps = @(Get-F8ManualSteps -Spec $spec)
            $joined = $steps -join "`n"
            Assert-Equal 6 $steps.Count "$caseName scalar manual step count"
            Assert-TextMatch $joined '(?is)launch.*baseline.*non-default.*Rate.*save.*ON.*applied.*OFF.*restoration.*close' "$caseName scalar manual sequence"
        }

        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'ap_100x' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        $manifestBytes = [System.IO.File]::ReadAllBytes($preflight.ManifestPath)
        $manifest = [System.Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json -ErrorAction Stop
        Assert-Equal @($preflight.ManualSteps) @($manifest.manualSteps) 'returned/manifest scalar manual steps'
        Assert-TextMatch (@($manifest.manualSteps) -join "`n") '(?is)baseline.*non-default.*Rate.*ON.*applied.*OFF.*restoration.*close' 'manifest scalar workflow'
    }

    Test-Case 'only Compose exposes compatibility authority, legacy, environment, and flag sources' {
        Require-Library
        $table = @(Get-F8Rt2CaseTable)
        foreach ($case in $table) {
            if ($case.Case -eq 'arena_plus_compose_f7') {
                Assert-Equal 'f8_authority.arena_plus_compose_f7' $case.AuthorityKey 'Compose authority'
                Assert-Equal 'labs.arena_plus_compose_f7' $case.LegacyKey 'Compose legacy'
                Assert-Equal 'FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7' $case.EnvironmentName 'Compose env'
                Assert-Equal 'FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7' $case.DisableEnvironmentName 'Compose disable env'
                Assert-Equal 'arena_plus_compose_f7.flag' $case.FlagName 'Compose flag'
            } else {
                foreach ($property in @('AuthorityKey', 'LegacyKey', 'EnvironmentName', 'DisableEnvironmentName', 'FlagName', 'OffFlagName', 'GlobalOffFlagName')) {
                    Assert-True ([string]::IsNullOrEmpty([string]$case.$property)) "$($case.Case) unexpectedly has $property"
                }
            }
        }
    }

    Test-Case 'INI parser supports initial UTF-8 BOM, CRLF/LF, and trimmed section/key/value' {
        Require-Library
        $bytes = [byte[]](0xEF, 0xBB, 0xBF) + [System.Text.Encoding]::UTF8.GetBytes(" [Boosters ] `r`n speed_hack = YES `r`n[CHEATS]`nalways_critical = off")
        $ini = ConvertFrom-F8IniBytes -Bytes $bytes
        Assert-Equal 'YES' $ini['boosters.speed_hack'] 'trimmed BOM/CRLF value'
        Assert-Equal 'off' $ini['cheats.always_critical'] 'LF value'
    }

    Test-Case 'INI parser keeps the first exact case-insensitive duplicate occurrence' {
        Require-Library
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("[Boosters]`nspeed_hack=1`nSPEED_HACK=0`n")
        $ini = ConvertFrom-F8IniBytes -Bytes $bytes
        Assert-Equal '1' $ini['BOOSTERS.SPEED_HACK'] 'first duplicate must win'
    }

    Test-Case 'INI parser rejects suffix and underscore aliases' {
        Require-Library
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack_extra=1`n[other]`nboosters_speed_hack=1`n")
        $ini = ConvertFrom-F8IniBytes -Bytes $bytes
        Assert-False $ini.ContainsKey('boosters.speed_hack') 'suffix/underscore alias acquired canonical key'
    }

    Test-Case 'INI parser requires nonempty input' {
        Require-Library
        $oneByte = ConvertFrom-F8IniBytes -Bytes ([byte[]](0x23))
        Assert-Equal 0 $oneByte.Count 'one-byte comment INI was rejected'
        Assert-Throws { ConvertFrom-F8IniBytes -Bytes ([byte[]]@()) | Out-Null } 'empty|size|INI' 'empty INI was accepted'
    }

    Test-Case 'INI parser accepts 65535 bytes and rejects 65536 bytes' {
        Require-Library
        $maximum = New-Object byte[] 65535
        $maximum[0] = 0x23
        for ($i = 1; $i -lt $maximum.Length; ++$i) { $maximum[$i] = 0x61 }
        $atLimit = ConvertFrom-F8IniBytes -Bytes $maximum
        Assert-Equal 0 $atLimit.Count '65535-byte INI was rejected'
        $tooLarge = New-Object byte[] 65536
        $tooLarge[0] = 0x23
        Assert-Throws { ConvertFrom-F8IniBytes -Bytes $tooLarge } '65535|size|large|INI' '65536-byte INI was accepted'
    }

    Test-Case 'INI parser enforces 256 parsed pairs including case-insensitive duplicates' {
        Require-Library
        $acceptedText = (@(for ($i = 0; $i -lt 256; ++$i) { "duplicate=$i" }) -join "`n")
        $accepted = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes($acceptedText))
        Assert-Equal 1 $accepted.Count '256 duplicate pairs did not preserve first-key semantics'
        Assert-Equal '0' $accepted['duplicate'] '256 duplicate pairs did not preserve the first value'
        $rejectedText = $acceptedText + "`nduplicate=256"
        Assert-Throws {
            ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes($rejectedText)) | Out-Null
        } '256|pair|INI' '257th parsed pair was accepted after duplicate collapse'
    }

    Test-Case 'INI parser accepts 127-byte flat keys and rejects 128-byte flat keys' {
        Require-Library
        $eAcute = ([char]0x00E9).ToString()
        $key127 = ((($eAcute * 63) -join '') + 'a')
        $key128 = (($eAcute * 64) -join '')
        Assert-Equal 127 ([System.Text.Encoding]::UTF8.GetByteCount($key127)) 'key fixture boundary'
        Assert-Equal 128 ([System.Text.Encoding]::UTF8.GetByteCount($key128)) 'oversize key fixture boundary'
        $acceptedKey = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("$key127=1"))
        Assert-Equal '1' $acceptedKey[$key127] '127-byte flat key was rejected'
        Assert-Throws { ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("$key128=1")) | Out-Null } '128|key|INI' '128-byte flat key was accepted'
    }

    Test-Case 'INI parser accepts 511-byte values and rejects 512-byte values' {
        Require-Library
        $eAcute = ([char]0x00E9).ToString()
        $value511 = ((($eAcute * 255) -join '') + 'a')
        $value512 = (($eAcute * 256) -join '')
        Assert-Equal 511 ([System.Text.Encoding]::UTF8.GetByteCount($value511)) 'value fixture boundary'
        Assert-Equal 512 ([System.Text.Encoding]::UTF8.GetByteCount($value512)) 'oversize value fixture boundary'
        $acceptedValue = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("k=$value511"))
        Assert-Equal $value511 $acceptedValue['k'] '511-byte value was rejected'
        Assert-Throws { ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("k=$value512")) | Out-Null } '512|value|INI' '512-byte value was accepted'
    }

    Test-Case 'boolean parser accepts exactly eight canonical true and false spellings' {
        Require-Library
        foreach ($text in @('1', 'true', 'TRUE', 'yes', 'YeS', 'on', 'ON')) {
            $parsed = ConvertFrom-F8BoolText -Text $text
            Assert-True $parsed.Valid "true literal $text rejected"
            Assert-True $parsed.Value "true literal $text resolved false"
        }
        foreach ($text in @('0', 'false', 'FALSE', 'no', 'No', 'off', 'OFF')) {
            $parsed = ConvertFrom-F8BoolText -Text $text
            Assert-True $parsed.Valid "false literal $text rejected"
            Assert-False $parsed.Value "false literal $text resolved true"
        }
        foreach ($text in @('', '2', 'enable', 'disabled', ' yes ')) {
            $parsed = ConvertFrom-F8BoolText -Text $text
            Assert-False $parsed.Valid "invalid literal [$text] acquired authority"
        }
    }

    Test-Case 'resolver mirrors disable, off flags, positive env true/false, INI, flag, and default precedence' {
        Require-Library
        $compose = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'arena_plus_compose_f7')[0]
        $gameRoot = Join-Path $script:FixtureRoot 'resolver-game'
        [System.IO.Directory]::CreateDirectory($gameRoot) | Out-Null
        $allTrueIni = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[f8_authority]`narena_plus_compose_f7=1`n[arena_plus]`ncompose_f7=1`n[labs]`narena_plus_compose_f7=1`n"))

        $none = { param($path) return $false }
        $result = Resolve-F8BoolGate -Spec $compose -Ini $allTrueIni -Environment @{ FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7 = 'true'; FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7 = 'true' } -GameRoot $gameRoot -FlagExistsProvider $none
        Assert-False $result.Value 'disable env true did not force false'
        Assert-Equal 'DisableEnvironment' $result.Source 'disable env source'

        $offPath = Join-Path $gameRoot 'modules\arena_plus_compose_f7.flag.off'
        $offProvider = { param($path) return $path -eq $offPath }.GetNewClosure()
        $specWithOff = $compose.PSObject.Copy()
        $specWithOff | Add-Member -NotePropertyName OffFlagName -NotePropertyValue 'arena_plus_compose_f7.flag.off' -Force
        $result = Resolve-F8BoolGate -Spec $specWithOff -Ini $allTrueIni -Environment @{ FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7 = 'false'; FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7 = 'true' } -GameRoot $gameRoot -FlagExistsProvider $offProvider
        Assert-False $result.Value 'off flag did not force false'
        Assert-Equal 'LegacyOffFlag' $result.Source 'off flag source'

        $result = Resolve-F8BoolGate -Spec $compose -Ini $allTrueIni -Environment @{ FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7 = 'false' } -GameRoot $gameRoot -FlagExistsProvider $none
        Assert-False $result.Value 'positive env false was ignored'
        Assert-Equal 'Environment' $result.Source 'positive env false source'

        $result = Resolve-F8BoolGate -Spec $compose -Ini $allTrueIni -Environment @{ FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7 = 'true' } -GameRoot $gameRoot -FlagExistsProvider $none
        Assert-True $result.Value 'positive env true was ignored'
        Assert-Equal 'Environment' $result.Source 'positive env true source'

        $result = Resolve-F8BoolGate -Spec $compose -Ini $allTrueIni -Environment @{} -GameRoot $gameRoot -FlagExistsProvider $none
        Assert-True $result.Value 'authority+canonical not selected'
        Assert-Equal 'AuthoritativeCanonicalIni' $result.Source 'authority source'

        $legacyIni = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[f8_authority]`narena_plus_compose_f7=invalid`n[arena_plus]`ncompose_f7=1`n[labs]`narena_plus_compose_f7=0`n"))
        $result = Resolve-F8BoolGate -Spec $compose -Ini $legacyIni -Environment @{ FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7 = 'invalid' } -GameRoot $gameRoot -FlagExistsProvider $none
        Assert-False $result.Value 'invalid higher values acquired authority over legacy'
        Assert-Equal 'LegacyIni' $result.Source 'legacy source after invalid values'

        $defaultIni = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[arena_plus]`ncompose_f7=1`n"))
        $result = Resolve-F8BoolGate -Spec $compose -Ini $defaultIni -Environment @{} -GameRoot $gameRoot -FlagExistsProvider $none
        Assert-False $result.Value 'marked spec accepted unmarked canonical'
        Assert-Equal 'DefaultValue' $result.Source 'marked spec default source'
    }

    Test-Case 'positive flag resolver checks modules, config, modules/config, then root in exact order' {
        Require-Library
        $compose = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'arena_plus_compose_f7')[0]
        $gameRoot = Join-Path $script:FixtureRoot 'flag-order-game'
        $emptyIni = @{}
        $expectedSources = @('LegacyFlagModules', 'LegacyFlagConfig', 'LegacyFlagModulesConfig', 'LegacyFlagRoot')
        $relative = @(
            'modules\arena_plus_compose_f7.flag',
            'config\arena_plus_compose_f7.flag',
            'modules\config\arena_plus_compose_f7.flag',
            'arena_plus_compose_f7.flag'
        )
        for ($winner = 0; $winner -lt $relative.Count; ++$winner) {
            $existing = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
            for ($i = $winner; $i -lt $relative.Count; ++$i) { [void]$existing.Add((Join-Path $gameRoot $relative[$i])) }
            $provider = { param($path) return $existing.Contains($path) }.GetNewClosure()
            $result = Resolve-F8BoolGate -Spec $compose -Ini $emptyIni -Environment @{} -GameRoot $gameRoot -FlagExistsProvider $provider
            Assert-True $result.Value "flag winner $winner did not enable"
            Assert-Equal $expectedSources[$winner] $result.Source "flag source order $winner"
        }
    }

    Test-Case 'unmarked canonical is used only when the spec has no authority key' {
        Require-Library
        $speed = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]
        $ini = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=on`n"))
        $result = Resolve-F8BoolGate -Spec $speed -Ini $ini -Environment @{} -GameRoot $script:FixtureRoot -FlagExistsProvider { param($path) $false }
        Assert-True $result.Value 'unmarked canonical ignored for no-authority spec'
        Assert-Equal 'UnmarkedCanonicalIni' $result.Source 'unmarked canonical source'
    }

    Test-Case 'Compose blocker detection rejects disable, off flag, and positive-env false only' {
        Require-Library
        foreach ($source in @('DisableEnvironment', 'LegacyOffFlag', 'Environment')) {
            $result = [pscustomobject]@{ Value = $false; Source = $source }
            Assert-True (Test-F8HigherAuthorityOnBlocker -Spec (@(Get-F8Rt2CaseTable | Where-Object Case -eq 'arena_plus_compose_f7')[0]) -Resolution $result) "$source was not detected as blocker"
        }
        Assert-False (Test-F8HigherAuthorityOnBlocker -Spec (@(Get-F8Rt2CaseTable | Where-Object Case -eq 'arena_plus_compose_f7')[0]) -Resolution ([pscustomobject]@{ Value = $false; Source = 'LegacyIni' })) 'legacy INI falsely treated as higher-authority blocker'
        Assert-False (Test-F8HigherAuthorityOnBlocker -Spec (@(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]) -Resolution ([pscustomobject]@{ Value = $false; Source = 'Environment' })) 'non-Compose falsely treated as blocker'
    }

    Test-Case 'process provider fails closed for FFX open, editor open, and query failure' {
        Require-Library
        Assert-Throws { Assert-F8ProcessesClosed -ProcessProvider { @([pscustomobject]@{ Name = 'FFX'; Id = 101 }) } -CheckName 'test' } 'FFX' 'FFX-open gate'
        Assert-Throws { Assert-F8ProcessesClosed -ProcessProvider { @([pscustomobject]@{ Name = 'FFXProjectEditor'; Id = 202 }) } -CheckName 'test' } 'FFXProjectEditor' 'editor-open gate'
        Assert-Throws { Assert-F8ProcessesClosed -ProcessProvider { throw 'query unavailable' } -CheckName 'test' } 'query|process' 'query-failure gate'
        Assert-True (Assert-F8ProcessesClosed -ProcessProvider { @() } -CheckName 'test') 'closed process gate did not pass'
    }

    Test-Case 'sensitive operation repeats the process check and catches a second-check race' {
        Require-Library
        $state = [pscustomobject]@{ Calls = 0 }
        $provider = {
            ++$state.Calls
            if ($state.Calls -eq 1) { return @() }
            return @([pscustomobject]@{ Name = 'FFX'; Id = 303 })
        }.GetNewClosure()
        Assert-Throws { Assert-F8DoubleProcessGate -ProcessProvider $provider -Operation { return 'never' } -CheckName 'race' } 'FFX' 'second-check race did not fail closed'
        Assert-Equal 2 $state.Calls 'process provider call count'
    }

    Test-Case 'exact leaf validator rejects each missing path and directory-instead-of-file' {
        Require-Library
        $supported = '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
        foreach ($missingKey in @('BuiltDll', 'Exe', 'InstalledDll', 'Ini')) {
            $paths = New-TestPathSet
            [System.IO.File]::Delete($paths[$missingKey])
            Assert-Throws { Get-F8ExactLeafEvidence -Paths $paths -SupportedExeHash $supported -HashProvider { param($path) ('A' * 64) } } $missingKey "missing $missingKey accepted"
        }
        $paths = New-TestPathSet
        [System.IO.File]::Delete($paths.InstalledDll)
        [System.IO.Directory]::CreateDirectory($paths.InstalledDll) | Out-Null
        Assert-Throws { Get-F8ExactLeafEvidence -Paths $paths -SupportedExeHash $supported -HashProvider { param($path) ('A' * 64) } } 'InstalledDll|directory|file' 'directory accepted as file'
    }

    Test-Case 'leaf validator requires matching DLL hashes and the supported executable hash' {
        Require-Library
        $supported = '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
        $paths = New-TestPathSet
        $hashes = @{
            $paths.BuiltDll = ('1' * 64)
            $paths.InstalledDll = ('2' * 64)
            $paths.Exe = $supported
            $paths.Ini = ('3' * 64)
        }
        $provider = { param($path) return $hashes[$path] }.GetNewClosure()
        Assert-Throws { Get-F8ExactLeafEvidence -Paths $paths -SupportedExeHash $supported -HashProvider $provider } 'deploy|DLL|hash' 'DLL mismatch accepted'
        $hashes[$paths.InstalledDll] = ('1' * 64)
        $hashes[$paths.Exe] = ('4' * 64)
        Assert-Throws { Get-F8ExactLeafEvidence -Paths $paths -SupportedExeHash $supported -HashProvider $provider } 'unsupported|executable|FFX' 'unsupported EXE accepted'
        $hashes[$paths.Exe] = $supported
        $evidence = Get-F8ExactLeafEvidence -Paths $paths -SupportedExeHash $supported -HashProvider $provider
        Assert-Equal $supported $evidence.Exe.Sha256 'supported EXE hash'
        Assert-Equal ('1' * 64) $evidence.BuiltDll.Sha256 'built DLL hash'
    }

    Test-Case 'dashboard gate requires exact dashboard.enabled true and rejects disabled or invalid' {
        Require-Library
        $enabled = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[dashboard]`nenabled=1`n"))
        $disabled = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[dashboard]`nenabled=0`n"))
        $invalid = ConvertFrom-F8IniBytes -Bytes ([System.Text.Encoding]::UTF8.GetBytes("[dashboard]`nenabled=maybe`n"))
        Assert-True (Test-F8DashboardEnabled -Ini $enabled) 'enabled dashboard rejected'
        Assert-False (Test-F8DashboardEnabled -Ini $disabled) 'disabled dashboard accepted'
        Assert-False (Test-F8DashboardEnabled -Ini $invalid) 'invalid dashboard accepted'
    }

    Test-Case 'snapshot preserves BOM, CRLF, and no-final-newline bytes exactly' {
        Require-Library
        $iniPath = New-FixtureFile -Name 'snapshot-source\ffx-hooks.ini' -Bytes ([byte[]](9))
        $evidenceDir = Join-Path $script:FixtureRoot 'snapshot-evidence'
        [System.IO.Directory]::CreateDirectory($evidenceDir) | Out-Null
        $bytes = [byte[]](0xEF, 0xBB, 0xBF) + [System.Text.Encoding]::UTF8.GetBytes("[dashboard]`r`nenabled=1")
        $snapshot = New-F8ConfigSnapshot -IniBytes $bytes -IniPath $iniPath -EvidenceDirectory $evidenceDir
        Assert-Equal $bytes ([System.IO.File]::ReadAllBytes($snapshot.Path)) 'snapshot bytes'
        Assert-Equal $bytes.Length $snapshot.Length 'snapshot length'
        Assert-Equal (Get-Sha256Hex $bytes) $snapshot.Sha256 'snapshot hash'
    }

    Test-Case 'atomic restore uses a same-directory owned temp and succeeds byte-exactly' {
        Require-Library
        $moduleText = Get-Content -Raw -LiteralPath $ModulePath
        Assert-TextMatch $moduleText '\[System\.IO\.FileMode\]::CreateNew' 'atomic temp is not CreateNew'
        Assert-TextMatch $moduleText '\.Flush\(\$true\)' 'atomic temp is not flushed to disk'
        Assert-TextMatch $moduleText 'MoveFileExW\([^\r\n]+0x1\s+-bor\s+0x8' 'MoveFileExW flags are not REPLACE_EXISTING|WRITE_THROUGH'
        $destination = New-FixtureFile -Name 'restore-success\ffx-hooks.ini' -Bytes ([byte[]](7, 7))
        $snapshotPath = New-FixtureFile -Name 'restore-success\snapshot.ini' -Bytes ([byte[]](1, 2, 3, 4))
        $moveState = [pscustomobject]@{ Temporary = $null }
        $move = {
            param($temporary, $target)
            $moveState.Temporary = $temporary
            [System.IO.File]::Delete($target)
            [System.IO.File]::Move($temporary, $target)
            return $true
        }.GetNewClosure()
        $result = Restore-F8ConfigSnapshot -SnapshotPath $snapshotPath -DestinationPath $destination -ExpectedSha256 (Get-Sha256Hex ([byte[]](1, 2, 3, 4))) -ExpectedLength 4 -BeforeReplaceProvider { $true } -MoveProvider $move
        Assert-True $result.Restored 'restore result false'
        Assert-Equal (Split-Path -Parent $destination) (Split-Path -Parent $moveState.Temporary) 'restore temp directory'
        Assert-Equal ([byte[]](1, 2, 3, 4)) ([System.IO.File]::ReadAllBytes($destination)) 'restored bytes'
        Assert-False (Test-Path -LiteralPath $moveState.Temporary) 'restore temp survived success'
    }

    Test-Case 'atomic restore failure preserves destination and cleans only its owned temp' {
        Require-Library
        $destination = New-FixtureFile -Name 'restore-failure\ffx-hooks.ini' -Bytes ([byte[]](7, 8))
        $snapshotPath = New-FixtureFile -Name 'restore-failure\snapshot.ini' -Bytes ([byte[]](1, 2, 3))
        $unrelated = New-FixtureFile -Name 'restore-failure\unrelated.tmp' -Bytes ([byte[]](9))
        $moveState = [pscustomobject]@{ Temporary = $null }
        $move = { param($temporary, $target) $moveState.Temporary = $temporary; return $false }.GetNewClosure()
        Assert-Throws { Restore-F8ConfigSnapshot -SnapshotPath $snapshotPath -DestinationPath $destination -ExpectedSha256 (Get-Sha256Hex ([byte[]](1, 2, 3))) -ExpectedLength 3 -BeforeReplaceProvider { $true } -MoveProvider $move } 'atomic|replace|MoveFileEx' 'atomic failure accepted'
        Assert-Equal ([byte[]](7, 8)) ([System.IO.File]::ReadAllBytes($destination)) 'destination changed on failed replace'
        Assert-False (Test-Path -LiteralPath $moveState.Temporary) 'owned temp survived failed replace'
        Assert-True (Test-Path -LiteralPath $unrelated -PathType Leaf) 'unrelated temp was removed'
    }

    Test-Case 'atomic restore fails closed on destination lock and repeated successful restore is safe' {
        Require-Library
        $destination = New-FixtureFile -Name 'restore-lock\ffx-hooks.ini' -Bytes ([byte[]](7, 8))
        $snapshotPath = New-FixtureFile -Name 'restore-lock\snapshot.ini' -Bytes ([byte[]](1, 2, 3))
        $hash = Get-Sha256Hex ([byte[]](1, 2, 3))
        $lock = New-Object System.IO.FileStream($destination, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        try {
            Assert-Throws { Restore-F8ConfigSnapshot -SnapshotPath $snapshotPath -DestinationPath $destination -ExpectedSha256 $hash -ExpectedLength 3 -BeforeReplaceProvider { $true } } 'atomic|replace|MoveFileEx|locked|access' 'destination lock accepted'
        } finally {
            $lock.Dispose()
        }
        $first = Restore-F8ConfigSnapshot -SnapshotPath $snapshotPath -DestinationPath $destination -ExpectedSha256 $hash -ExpectedLength 3 -BeforeReplaceProvider { $true }
        $second = Restore-F8ConfigSnapshot -SnapshotPath $snapshotPath -DestinationPath $destination -ExpectedSha256 $hash -ExpectedLength 3 -BeforeReplaceProvider { $true }
        Assert-True ($first.Restored -and $second.Restored) 'repeated restore failed'
        Assert-Equal ([byte[]](1, 2, 3)) ([System.IO.File]::ReadAllBytes($destination)) 'repeated restore bytes'
    }

    Test-Case 'Verify without RestoreConfigSnapshot leaves INI untouched and reports snapshot path' {
        Require-Library
        $destination = New-FixtureFile -Name 'verify-no-restore\ffx-hooks.ini' -Bytes ([byte[]](7, 8))
        $snapshotPath = New-FixtureFile -Name 'verify-no-restore\snapshot.ini' -Bytes ([byte[]](1, 2, 3))
        $before = [System.IO.File]::ReadAllBytes($destination)
        $result = Test-F8RestoreAuthorization -RestoreConfigSnapshot:$false -SnapshotPath $snapshotPath
        Assert-False $result.Authorized 'restore unexpectedly authorized'
        Assert-Equal $snapshotPath $result.SnapshotPath 'snapshot path not reported'
        Assert-Equal $before ([System.IO.File]::ReadAllBytes($destination)) 'INI mutated without restore authorization'
    }

    Test-Case 'manifest round-trip enforces case/session and rejects ambiguous evidence selection' {
        Require-Library
        $evidenceDir = Join-Path $script:FixtureRoot 'manifest-roundtrip'
        [System.IO.Directory]::CreateDirectory($evidenceDir) | Out-Null
        $snapshotPath = New-FixtureFile -Name 'manifest-roundtrip\ffx-hooks.ini.snapshot.bin' -Bytes ([byte[]](1, 2, 3))
        $manifest = Get-ManifestFixture
        $manifest.snapshot.path = $snapshotPath
        $manifest.snapshot.sha256 = Get-Sha256Hex ([byte[]](1, 2, 3))
        $manifest.paths.ini.sha256 = $manifest.snapshot.sha256
        $manifest.paths.ini.length = 3
        $written = Write-F8IntegrityManifest -Manifest $manifest -EvidenceDirectory $evidenceDir
        $loaded = Read-F8IntegrityManifest -EvidenceDirectory $evidenceDir -ExpectedCase 'speed_hack' -ExpectedSessionId 'session-a'
        Assert-Equal 'speed_hack' $loaded.case 'manifest case'
        Assert-Throws { Read-F8IntegrityManifest -EvidenceDirectory $evidenceDir -ExpectedCase 'dialog_skip' -ExpectedSessionId 'session-a' } 'case' 'case mismatch accepted'
        Assert-Throws { Read-F8IntegrityManifest -EvidenceDirectory $evidenceDir -ExpectedCase 'speed_hack' -ExpectedSessionId 'session-b' } 'session' 'session mismatch accepted'
        Assert-Throws { Resolve-F8VerifyEvidenceDirectory -EvidenceDirectory '' -EvidenceRoot $script:FixtureRoot } 'exact|EvidenceDirectory' 'ambiguous evidence root accepted'
    }

    Test-Case 'manifest envelope pins the exact validated and parsed byte arrays' {
        Require-Library
        $evidenceDir = Join-Path $script:FixtureRoot 'manifest-envelope'
        [System.IO.Directory]::CreateDirectory($evidenceDir) | Out-Null
        $snapshotPath = New-FixtureFile -Name 'manifest-envelope\ffx-hooks.ini.snapshot.bin' -Bytes ([byte[]](1, 2, 3))
        $manifest = Get-ManifestFixture
        $manifest.snapshot.path = $snapshotPath
        $manifest.snapshot.sha256 = Get-Sha256Hex ([byte[]](1, 2, 3))
        $manifest.paths.ini.sha256 = $manifest.snapshot.sha256
        $manifest.paths.ini.length = 3
        $written = Write-F8IntegrityManifest -Manifest $manifest -EvidenceDirectory $evidenceDir
        $manifestBytes = [System.IO.File]::ReadAllBytes($written.ManifestPath)
        $sidecarBytes = [System.IO.File]::ReadAllBytes($written.HashPath)
        $envelope = & $script:ImportedModule {
            param($directory, $caseName, $sessionId)
            Read-F8IntegrityManifestEnvelope -EvidenceDirectory $directory -ExpectedCase $caseName -ExpectedSessionId $sessionId
        } $evidenceDir 'speed_hack' 'session-a'
        Assert-Equal 'speed_hack' $envelope.Manifest.case 'envelope parsed manifest case'
        Assert-Equal 3 @($envelope.ManifestRecord.PSObject.Properties).Count 'manifest record exact field count'
        Assert-Equal 3 @($envelope.SidecarRecord.PSObject.Properties).Count 'sidecar record exact field count'
        Assert-Equal ([System.IO.Path]::GetFullPath($written.ManifestPath)) $envelope.ManifestRecord.path 'manifest record path'
        Assert-Equal $manifestBytes.Length $envelope.ManifestRecord.length 'manifest record parsed-byte length'
        Assert-Equal (Get-Sha256Hex $manifestBytes) $envelope.ManifestRecord.sha256 'manifest record parsed-byte SHA'
        Assert-Equal ([System.IO.Path]::GetFullPath($written.HashPath)) $envelope.SidecarRecord.path 'sidecar record path'
        Assert-Equal $sidecarBytes.Length $envelope.SidecarRecord.length 'sidecar record validated-byte length'
        Assert-Equal (Get-Sha256Hex $sidecarBytes) $envelope.SidecarRecord.sha256 'sidecar record validated-byte SHA'

        # A coordinated post-return rewrite is internally consistent on disk but must not replace
        # the identity pinned from the exact byte arrays that produced Envelope.Manifest.
        [byte[]]$rewrittenManifestBytes = $manifestBytes + [byte[]](0x20)
        [System.IO.File]::WriteAllBytes($written.ManifestPath, $rewrittenManifestBytes)
        [System.IO.File]::WriteAllBytes($written.HashPath, [System.Text.Encoding]::ASCII.GetBytes((Get-Sha256Hex $rewrittenManifestBytes)))
        Assert-Throws {
            & $script:ImportedModule {
                param($manifestRecord, $sidecarRecord, $manifestPath)
                Assert-F8PinnedPreflightIdentity -ManifestRecord $manifestRecord -SidecarRecord $sidecarRecord -PreflightManifestPath $manifestPath
            } $envelope.ManifestRecord $envelope.SidecarRecord $written.ManifestPath
        } 'pinned Preflight manifest.*identity changed' 'post-return coordinated mutation replaced the parsed-byte identity'
    }

    Test-Case 'Verify consumes the same-read Preflight envelope without an initial pin reread' {
        $source = Get-Content -LiteralPath $ModulePath -Raw
        $reader = [regex]::Match($source, '(?s)function Read-F8IntegrityManifestEnvelope\s*\{(?<body>.*?)\r?\n\}\r?\n\r?\nfunction Read-F8IntegrityManifest\s*\{')
        Assert-True $reader.Success 'same-read manifest envelope function is missing'
        Assert-TextMatch $reader.Groups['body'].Value '(?s)New-F8FileIntegrityRecordFromBytes\s+-Path\s+\$manifestPath\s+-Bytes\s+\$manifestBytes' 'manifest pin is not derived from the parsed byte array'
        Assert-TextMatch $reader.Groups['body'].Value '(?s)New-F8FileIntegrityRecordFromBytes\s+-Path\s+\$hashPath\s+-Bytes\s+\$sidecarBytes' 'sidecar pin is not derived from the validated byte array'
        Assert-TextNotMatch $reader.Groups['body'].Value '(?i)Get-F8FileIntegrityRecord' 'envelope rereads a path to create an initial pin'

        $invokeWindow = [regex]::Match($source, '(?s)\$manifestEnvelope\s*=\s*Read-F8IntegrityManifestEnvelope(?<body>.*?)if\s*\(\[string\]\(Get-F8RequiredValue\s+\$manifest\s+''canonical''')
        Assert-True $invokeWindow.Success 'Verify does not consume the manifest envelope'
        Assert-TextMatch $invokeWindow.Groups['body'].Value '(?s)\$manifest\s*=\s*\$manifestEnvelope\.Manifest' 'Verify does not use the parsed envelope manifest'
        Assert-TextMatch $invokeWindow.Groups['body'].Value '(?s)\$preflightManifestPin\s*=\s*\$manifestEnvelope\.ManifestRecord' 'Verify does not use the envelope manifest pin'
        Assert-TextMatch $invokeWindow.Groups['body'].Value '(?s)\$preflightManifestSidecarPin\s*=\s*\$manifestEnvelope\.SidecarRecord' 'Verify does not use the envelope sidecar pin'
        Assert-TextNotMatch $invokeWindow.Groups['body'].Value '(?i)Get-F8FileIntegrityRecord' 'Verify opens an initial pin window by rereading a Preflight path'
    }

    Test-Case 'manifest integrity rejects tampered manifest and snapshot fields/bytes' {
        Require-Library
        $evidenceDir = Join-Path $script:FixtureRoot 'manifest-tamper'
        [System.IO.Directory]::CreateDirectory($evidenceDir) | Out-Null
        $snapshot = New-FixtureFile -Name 'manifest-tamper\ffx-hooks.ini.snapshot.bin' -Bytes ([byte[]](1, 2, 3))
        $manifest = Get-ManifestFixture
        $manifest.snapshot.path = $snapshot
        $manifest.snapshot.sha256 = Get-Sha256Hex ([byte[]](1, 2, 3))
        $manifest.snapshot.length = 3
        $manifest.paths.ini.sha256 = $manifest.snapshot.sha256
        $manifest.paths.ini.length = 3
        $written = Write-F8IntegrityManifest -Manifest $manifest -EvidenceDirectory $evidenceDir
        [System.IO.File]::AppendAllText($written.ManifestPath, " ")
        Assert-Throws { Read-F8IntegrityManifest -EvidenceDirectory $evidenceDir -ExpectedCase 'speed_hack' -ExpectedSessionId 'session-a' } 'integrity|SHA|manifest' 'tampered manifest accepted'

        [System.IO.File]::Delete($written.ManifestPath)
        [System.IO.File]::Delete($written.HashPath)
        $written = Write-F8IntegrityManifest -Manifest $manifest -EvidenceDirectory $evidenceDir
        [System.IO.File]::WriteAllBytes($snapshot, [byte[]](1, 2, 4))
        Assert-Throws { Read-F8IntegrityManifest -EvidenceDirectory $evidenceDir -ExpectedCase 'speed_hack' -ExpectedSessionId 'session-a' } 'snapshot|integrity|SHA' 'tampered snapshot accepted'
    }

    Test-Case 'manifest snapshot must remain inside its exact evidence directory' {
        Require-Library
        $evidenceDir = Join-Path $script:FixtureRoot 'manifest-containment'
        [System.IO.Directory]::CreateDirectory($evidenceDir) | Out-Null
        $outsideSnapshot = New-FixtureFile -Name 'manifest-outside\ffx-hooks.ini.snapshot.bin' -Bytes ([byte[]](1, 2, 3))
        $manifest = Get-ManifestFixture
        $manifest.snapshot.path = $outsideSnapshot
        $manifest.snapshot.sha256 = Get-Sha256Hex ([byte[]](1, 2, 3))
        $manifest.snapshot.length = 3
        $manifest.paths.ini.sha256 = $manifest.snapshot.sha256
        $manifest.paths.ini.length = 3
        Assert-Throws { Write-F8IntegrityManifest -Manifest $manifest -EvidenceDirectory $evidenceDir } 'snapshot|evidence|outside' 'outside snapshot path accepted'
    }

    Test-Case 'Verify requires the exact snapshot leaf before any replace' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Add-ValidProtocolLog -Fixture $fixture
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $wrongSnapshotPath = Join-Path $preflight.EvidenceDirectory 'snapshot.bin'
        [System.IO.File]::Move($preflight.SnapshotPath, $wrongSnapshotPath)
        [void](Rewrite-PreflightManifestBundle -EvidenceDirectory $preflight.EvidenceDirectory -Transform {
            param($manifest)
            $manifest.snapshot.path = [System.IO.Path]::GetFullPath($wrongSnapshotPath)
        }.GetNewClosure())
        $state = [pscustomobject]@{ MoveCalls = 0 }
        $fixture.Providers.MoveProvider = {
            param($temporary, $destination)
            ++$state.MoveCalls
            [System.IO.File]::Delete($destination)
            [System.IO.File]::Move($temporary, $destination)
            return $true
        }.GetNewClosure()
        $fixture.ProcessState.Calls = 0
        $thrown = $false
        try {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
        } catch {
            $thrown = $true
        }
        Assert-Equal 0 $state.MoveCalls 'MoveProvider ran for a noncanonical snapshot leaf'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'destination changed for a noncanonical snapshot leaf'
        Assert-True $thrown 'noncanonical snapshot leaf was accepted'
    }

    Test-Case 'partial snapshot relabeling that omits paths.ini is rejected before temp or move' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Add-ValidProtocolLog -Fixture $fixture
        [byte[]]$tamperedSnapshot = (Get-AllOffIniBytes) + [System.Text.Encoding]::UTF8.GetBytes("`n; partial relabel")
        [System.IO.File]::WriteAllBytes($preflight.SnapshotPath, $tamperedSnapshot)
        [void](Rewrite-PreflightManifestBundle -EvidenceDirectory $preflight.EvidenceDirectory -Transform {
            param($manifest)
            # Injected transforms run outside the harness helper scope, so hash locally.
            $sha = [System.Security.Cryptography.SHA256]::Create()
            try {
                $manifest.snapshot.sha256 = (($sha.ComputeHash($tamperedSnapshot) | ForEach-Object { $_.ToString('X2') }) -join '')
            } finally {
                $sha.Dispose()
            }
            $manifest.snapshot.length = $tamperedSnapshot.Length
        }.GetNewClosure())
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $destinationDirectory = Split-Path -Parent $fixture.Paths.Ini
        $ownedTempPattern = '^\.' + [regex]::Escape((Split-Path -Leaf $fixture.Paths.Ini)) + '\.restore\.[0-9a-f]{32}\.tmp$'
        $state = [pscustomobject]@{ MoveCalls = 0 }
        $fixture.Providers.MoveProvider = {
            param($temporary, $destination)
            ++$state.MoveCalls
            [System.IO.File]::Delete($destination)
            [System.IO.File]::Move($temporary, $destination)
            return $true
        }.GetNewClosure()
        $fixture.ProcessState.Calls = 0
        $thrown = $false
        try {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
        } catch {
            $thrown = $true
        }
        Assert-Equal 0 $state.MoveCalls 'MoveProvider ran before snapshot matched manifest paths.ini'
        Assert-True ($fixture.ProcessState.Calls -lt 3) 'final pre-replace gate ran after an inconsistent snapshot reached temp creation'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'destination changed for a coordinated snapshot/paths.ini mismatch'
        $leftovers = @([System.IO.Directory]::GetFiles($destinationDirectory) | Where-Object { (Split-Path -Leaf $_) -cmatch $ownedTempPattern })
        Assert-Equal 0 $leftovers.Count 'owned temp survived snapshot-link rejection'
        Assert-True $thrown 'coordinated snapshot/paths.ini mismatch was accepted'
    }

    Test-Case 'Preflight and Verify share and record the same safe default evidence root' {
        Require-Library
        $fixture = New-ProtocolFixture
        $isolatedRepoRoot = Join-Path $script:FixtureRoot ('default-root-repo-' + [Guid]::NewGuid().ToString('N'))
        $isolatedScriptRoot = Join-Path $isolatedRepoRoot 'src\runtime\FfxHooksDll'
        [System.IO.Directory]::CreateDirectory($isolatedScriptRoot) | Out-Null
        $expectedRoot = [System.IO.Path]::GetFullPath((Join-Path $isolatedRepoRoot 'work\f8_rt2')).TrimEnd('\')
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -ScriptRoot $isolatedScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Assert-Equal $expectedRoot ([System.IO.Path]::GetFullPath((Split-Path -Parent $preflight.EvidenceDirectory)).TrimEnd('\')) 'Preflight default evidence root'
        Assert-True ((Split-Path -Leaf $preflight.EvidenceDirectory) -cmatch '^\d{8}T\d{9}Z_speed_hack_[0-9a-f]{32}$') 'Preflight evidence leaf is not exact timestamp_case_session form'
        $manifest = Read-F8IntegrityManifest -EvidenceDirectory $preflight.EvidenceDirectory -ExpectedCase 'speed_hack' -ExpectedSessionId $preflight.SessionId
        $rootProperty = $manifest.evidence.PSObject.Properties['root']
        Assert-True ($null -ne $rootProperty) 'manifest did not record the default evidence root'
        Assert-Equal $expectedRoot ([System.IO.Path]::GetFullPath([string]$rootProperty.Value).TrimEnd('\')) 'manifest default evidence root'

        Add-ValidProtocolLog -Fixture $fixture
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n"))
        $fixture.ProcessState.Calls = 0
        $verified = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -ScriptRoot $isolatedScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Assert-True $verified.FinalVerdict 'Verify rejected the matching omitted default evidence root'
    }

    Test-Case 'custom Preflight evidence root must be repeated explicitly on Verify' {
        Require-Library
        $fixture = New-ProtocolFixture
        $isolatedRepoRoot = Join-Path $script:FixtureRoot ('custom-root-repo-' + [Guid]::NewGuid().ToString('N'))
        $isolatedScriptRoot = Join-Path $isolatedRepoRoot 'src\runtime\FfxHooksDll'
        [System.IO.Directory]::CreateDirectory($isolatedScriptRoot) | Out-Null
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $isolatedScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Add-ValidProtocolLog -Fixture $fixture
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $state = [pscustomobject]@{ MoveCalls = 0 }
        $fixture.Providers.MoveProvider = {
            param($temporary, $destination)
            ++$state.MoveCalls
            [System.IO.File]::Delete($destination)
            [System.IO.File]::Move($temporary, $destination)
            return $true
        }.GetNewClosure()
        $fixture.ProcessState.Calls = 0
        $thrown = $false
        try {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -ScriptRoot $isolatedScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
        } catch {
            $thrown = $true
        }
        Assert-Equal 0 $state.MoveCalls 'MoveProvider ran when a custom root was omitted on Verify'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'destination changed when a custom root was omitted on Verify'
        Assert-True $thrown 'custom Preflight root was accepted without explicit Verify repetition'
    }

    Test-Case 'Verify rejects a rehashed manifest with the wrong recorded evidence root' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        $wrongRoot = Join-Path $script:FixtureRoot ('wrong-recorded-root-' + [Guid]::NewGuid().ToString('N'))
        [void](Rewrite-PreflightManifestBundle -EvidenceDirectory $preflight.EvidenceDirectory -Transform {
            param($manifest)
            $manifest.evidence | Add-Member -Force -NotePropertyName root -NotePropertyValue ([System.IO.Path]::GetFullPath($wrongRoot))
        }.GetNewClosure())
        Add-ValidProtocolLog -Fixture $fixture
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $state = [pscustomobject]@{ MoveCalls = 0 }
        $fixture.Providers.MoveProvider = { param($temporary, $destination) ++$state.MoveCalls; return $false }.GetNewClosure()
        $fixture.ProcessState.Calls = 0
        $thrown = $false
        try {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
        } catch {
            $thrown = $true
        }
        Assert-Equal 0 $state.MoveCalls 'MoveProvider ran with a wrong recorded evidence root'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'destination changed with a wrong recorded evidence root'
        Assert-True $thrown 'wrong recorded evidence root was accepted'
    }

    Test-Case 'Verify requires EvidenceDirectory to be a direct child with the exact leaf form' {
        Require-Library
        foreach ($shape in @('nested', 'wrong-leaf')) {
            $fixture = New-ProtocolFixture
            $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
            Add-ValidProtocolLog -Fixture $fixture
            $originalLeaf = Split-Path -Leaf $preflight.EvidenceDirectory
            $destination = if ($shape -ceq 'nested') {
                Join-Path (Join-Path $fixture.EvidenceRoot 'nested') $originalLeaf
            } else {
                Join-Path $fixture.EvidenceRoot ("invalid_speed_hack_" + $preflight.SessionId)
            }
            $relocated = Move-PreflightEvidenceFixture -SourceDirectory $preflight.EvidenceDirectory -DestinationDirectory $destination -RecordedRoot $fixture.EvidenceRoot
            $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
            [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
            $state = [pscustomobject]@{ MoveCalls = 0 }
            $fixture.Providers.MoveProvider = { param($temporary, $target) ++$state.MoveCalls; return $false }.GetNewClosure()
            $fixture.ProcessState.Calls = 0
            $thrown = $false
            try {
                Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $relocated -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
            } catch {
                $thrown = $true
            }
            Assert-Equal 0 $state.MoveCalls "MoveProvider ran for $shape EvidenceDirectory"
            Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) "destination changed for $shape EvidenceDirectory"
            Assert-True $thrown "$shape EvidenceDirectory was accepted"
        }
    }

    Test-Case 'evidence root itself and sibling-prefix directories are not Verify children' {
        Require-Library
        $root = Join-Path $script:FixtureRoot ('strict-root-' + [Guid]::NewGuid().ToString('N'))
        $sibling = $root + '-sibling'
        [System.IO.Directory]::CreateDirectory($root) | Out-Null
        [System.IO.Directory]::CreateDirectory($sibling) | Out-Null
        Assert-Throws {
            Resolve-F8VerifyEvidenceDirectory -EvidenceDirectory $sibling -EvidenceRoot $root | Out-Null
        } 'outside|child|root' 'sibling-prefix directory was accepted as contained'
        Assert-Throws {
            Resolve-F8VerifyEvidenceDirectory -EvidenceDirectory $root -EvidenceRoot $root | Out-Null
        } 'child|root|EvidenceDirectory' 'EvidenceRoot itself was accepted as its own evidence child'
    }

    Test-Case 'Verify rejects uppercase session hex outside the generated lowercase leaf policy' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Add-ValidProtocolLog -Fixture $fixture
        $originalLeaf = Split-Path -Leaf $preflight.EvidenceDirectory
        $upperSession = $preflight.SessionId.ToUpperInvariant()
        $upperLeaf = $originalLeaf.Substring(0, $originalLeaf.Length - $preflight.SessionId.Length) + $upperSession
        $caseHop = Join-Path $fixture.EvidenceRoot ('case-hop-' + [Guid]::NewGuid().ToString('N'))
        [System.IO.Directory]::Move($preflight.EvidenceDirectory, $caseHop)
        $relocated = Move-PreflightEvidenceFixture -SourceDirectory $caseHop -DestinationDirectory (Join-Path $fixture.EvidenceRoot $upperLeaf) -RecordedRoot $fixture.EvidenceRoot
        [void](Rewrite-PreflightManifestBundle -EvidenceDirectory $relocated -Transform {
            param($manifest)
            $manifest.sessionId = $upperSession
        }.GetNewClosure())
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $state = [pscustomobject]@{ MoveCalls = 0 }
        $fixture.Providers.MoveProvider = { param($temporary, $target) ++$state.MoveCalls; return $false }.GetNewClosure()
        $fixture.ProcessState.Calls = 0
        $thrown = $false
        try {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $relocated -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
        } catch {
            $thrown = $true
        }
        Assert-Equal 0 $state.MoveCalls 'MoveProvider ran for uppercase session hex'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'destination changed for uppercase session hex'
        Assert-True $thrown 'uppercase session hex was accepted'
    }

    Test-Case 'log boundary rejects next-open rotation risk and reads byte offset/prefix hash/counter' {
        Require-Library
        $logPath = New-FixtureFile -Name 'log-boundary\ffx-hooks.log' -Bytes ([System.Text.Encoding]::UTF8.GetBytes('prefix'))
        $counterPath = New-FixtureFile -Name 'log-boundary\ffx-hooks.log.cnt' -Bytes ([System.Text.Encoding]::ASCII.GetBytes('8'))
        $boundary = Get-F8LogBoundary -LogPath $logPath -CounterPath $counterPath
        Assert-Equal 6 $boundary.StartLength 'log start byte length'
        Assert-Equal (Get-Sha256Hex ([System.Text.Encoding]::UTF8.GetBytes('prefix'))) $boundary.PrefixSha256 'log prefix hash'
        Assert-Equal 8 $boundary.OpenCounter 'open counter'
        [System.IO.File]::WriteAllText($counterPath, '9')
        Assert-Throws { Get-F8LogBoundary -LogPath $logPath -CounterPath $counterPath } 'rotate|counter|next open' 'rotation-risk counter accepted'
    }

    Test-Case 'log slicing is byte-exact, ignores old errors, and rejects truncation/prefix mismatch/counter drift' {
        Require-Library
        $old = [System.Text.Encoding]::UTF8.GetBytes("old crash exception conflict`r`n")
        $new = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText))
        $logPath = New-FixtureFile -Name 'log-slice\ffx-hooks.log' -Bytes ($old + $new)
        $slice = Read-F8LogSlice -LogPath $logPath -StartLength $old.Length -PrefixSha256 (Get-Sha256Hex $old)
        Assert-Equal $new $slice.Bytes 'log slice bytes'
        Assert-Equal (Get-Sha256Hex $new) $slice.Sha256 'log slice hash'

        Assert-Throws { Read-F8LogSlice -LogPath $logPath -StartLength (($old + $new).Length + 1) -PrefixSha256 (Get-Sha256Hex $old) } 'truncat|length' 'truncated log accepted'
        Assert-Throws { Read-F8LogSlice -LogPath $logPath -StartLength $old.Length -PrefixSha256 ('0' * 64) } 'prefix|hash' 'prefix mismatch accepted'
        Assert-Throws { Test-F8LogCounterTransition -StartCounter 2 -CurrentCounter 4 -StartCounterExisted:$true } 'counter|session' 'counter drift accepted'
        Assert-True (Test-F8LogCounterTransition -StartCounter 2 -CurrentCounter 3 -StartCounterExisted:$true) 'single counter transition rejected'
    }

    Test-Case 'ConfigPolled log requires common and ordered ON/OFF anchors plus both observations' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]
        $bytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText))
        $result = Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$true -ObservedRestored:$true
        Assert-True $result.Valid 'valid ConfigPolled evidence rejected'
        Assert-Equal 'ConfigPolled' $result.ApplyMode 'ConfigPolled mode'
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$false -ObservedRestored:$true } 'ObservedApplied|observation' 'missing applied observation accepted'
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$true -ObservedRestored:$false } 'ObservedRestored|observation' 'missing restored observation accepted'
        $noCommon = [System.Text.Encoding]::UTF8.GetBytes(((New-ValidLogText) -replace '(?m)^\[ffx-hooks\] F8 catalog.*\r?\n', ''))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $noCommon -ObservedApplied:$true -ObservedRestored:$true } 'catalog|common' 'missing common anchor accepted'
        $wrongOrder = [System.Text.Encoding]::UTF8.GetBytes("[ffx-hooks] F8 catalog rows=37 live=14 restart=16 not_wired=7`r`n[ffx-hooks] F8 edit key=boosters.speed_hack edit=SAVED requested=0 effective=0 source=UnmarkedCanonicalIni`r`n[ffx-hooks] F8 edit key=boosters.speed_hack edit=SAVED requested=1 effective=1 source=UnmarkedCanonicalIni`r`n")
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $wrongOrder -ObservedApplied:$true -ObservedRestored:$true } 'order|ordered' 'wrong anchor order accepted'
    }

    Test-Case 'RuntimeAcknowledged log additionally requires ordered applied/restored runtime anchors' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'permanent_sensor')[0]
        $valid = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode))
        Assert-True (Test-F8LogEvidence -CaseSpec $spec -SliceBytes $valid -ObservedApplied:$true -ObservedRestored:$true).Valid 'valid RuntimeAcknowledged evidence rejected'
        $missingApply = [System.Text.Encoding]::UTF8.GetBytes(((New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode) -replace '(?m)^\[f8-runtime\].*state=applied.*\r?\n', ''))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $missingApply -ObservedApplied:$true -ObservedRestored:$true } 'applied|runtime' 'missing runtime apply accepted'
        $missingRestore = [System.Text.Encoding]::UTF8.GetBytes(((New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode) -replace '(?m)^\[f8-runtime\].*state=restored.*\r?\n', ''))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $missingRestore -ObservedApplied:$true -ObservedRestored:$true } 'restored|runtime' 'missing runtime restore accepted'
    }

    Test-Case 'scalar reward evidence binds one non-default SAVED rate to exact applied and restored readback' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'ap_100x')[0]
        $validText = New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode -ScalarValue 25
        $valid = [System.Text.Encoding]::UTF8.GetBytes($validText)
        $result = Test-F8LogEvidence -CaseSpec $spec -SliceBytes $valid -ObservedApplied:$true -ObservedRestored:$true
        Assert-True $result.Valid 'valid scalar reward evidence rejected'

        $invalidCases = @(
            [pscustomobject]@{
                Name = 'missing scalar edit'
                Text = $validText -replace '(?m)^\[ffx-hooks\] F8 scalar edit.*\r?\n', ''
                Pattern = 'scalar.*edit|missing'
            },
            [pscustomobject]@{
                Name = 'other scalar key'
                Text = $validText.Replace('cheats.ap_multiplier', 'cheats.gil_multiplier')
                Pattern = 'scalar.*key|other.*scalar'
            },
            [pscustomobject]@{
                Name = 'non-SAVED scalar edit'
                Text = $validText.Replace('scalar edit key=cheats.ap_multiplier edit=SAVED', 'scalar edit key=cheats.ap_multiplier edit=PERSIST FAILED')
                Pattern = 'scalar.*SAVED|PERSIST'
            },
            [pscustomobject]@{
                Name = 'configured scalar differs from requested'
                Text = $validText.Replace('requested=25 configured=25', 'requested=25 configured=24')
                Pattern = 'scalar.*configured|mismatch'
            },
            [pscustomobject]@{
                Name = 'applied scalar differs from saved value'
                Text = $validText.Replace('state=applied gate=01 scalar=25', 'state=applied gate=01 scalar=24')
                Pattern = 'scalar.*readback|mismatch|applied'
            },
            [pscustomobject]@{
                Name = 'applied gate is OFF'
                Text = $validText.Replace('state=applied gate=01 scalar=25', 'state=applied gate=00 scalar=25')
                Pattern = 'gate|applied'
            },
            [pscustomobject]@{
                Name = 'restored gate remains ON'
                Text = $validText.Replace('state=restored gate=00 scalar=none', 'state=restored gate=01 scalar=none')
                Pattern = 'gate|restored'
            },
            [pscustomobject]@{
                Name = 'restored line claims stale applied scalar'
                Text = $validText.Replace('state=restored gate=00 scalar=none', 'state=restored gate=00 scalar=25')
                Pattern = 'scalar|restored'
            },
            [pscustomobject]@{
                Name = 'default scalar does not exercise a selected rate'
                Text = New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode -ScalarValue 100
                Pattern = 'non-default|selected.*scalar|default'
            },
            [pscustomobject]@{
                Name = 'out-of-range scalar'
                Text = New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode -ScalarValue 101
                Pattern = 'range|scalar'
            }
        )
        foreach ($invalid in $invalidCases) {
            $bytes = [System.Text.Encoding]::UTF8.GetBytes([string]$invalid.Text)
            Assert-Throws {
                Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$true -ObservedRestored:$true | Out-Null
            } ([string]$invalid.Pattern) "scalar evidence accepted: $($invalid.Name)"
        }
        Assert-Equal 25 $result.AppliedScalar 'validated scalar readback'
    }

    Test-Case 'scalar parser rejects non-scalar edits and every additional or misordered selected record' {
        Require-Library
        $nonScalar = Get-F8CaseSpec -Case 'speed_hack'
        $nonScalarText = New-ValidLogText -OtherLine '[ffx-hooks] F8 scalar edit key=cheats.ap_multiplier edit=SAVED requested=25 configured=25'
        Assert-Throws {
            Test-F8LogEvidence -CaseSpec $nonScalar -SliceBytes ([System.Text.Encoding]::UTF8.GetBytes($nonScalarText)) -ObservedApplied -ObservedRestored | Out-Null
        } 'scalar.*non-scalar|unexpected.*scalar' 'scalar edit was accepted for a non-scalar case'

        $spec = Get-F8CaseSpec -Case 'ap_100x'
        $valid = New-ValidLogText -Canonical $spec.Canonical -ApplyMode $spec.ApplyMode -ScalarValue 25
        $baseline = '[f8-runtime] key=cheats.ap_100x effective=0 source=UnmarkedCanonicalIni state=restored gate=00 scalar=none'
        $withBaseline = $valid.Replace(
            '[ffx-hooks] F8 catalog rows=37 live=14 restart=16 not_wired=7',
            "[ffx-hooks] F8 catalog rows=37 live=14 restart=16 not_wired=7`r`n$baseline")
        Assert-True (Test-F8LogEvidence -CaseSpec $spec -SliceBytes ([System.Text.Encoding]::UTF8.GetBytes($withBaseline)) -ObservedApplied -ObservedRestored).Valid 'truthful baseline restoration before ON was rejected'

        $extraCases = @(
            [pscustomobject]@{
                Name = 'additional matching apply'
                Text = $valid.Replace(
                    '[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=25',
                    "[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=25`r`n[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=25")
            },
            [pscustomobject]@{
                Name = 'contradictory restore in ON window'
                Text = $valid.Replace(
                    '[ffx-hooks] F8 edit key=cheats.ap_100x edit=SAVED requested=0 effective=0 source=UnmarkedCanonicalIni',
                    "$baseline`r`n[ffx-hooks] F8 edit key=cheats.ap_100x edit=SAVED requested=0 effective=0 source=UnmarkedCanonicalIni")
            },
            [pscustomobject]@{
                Name = 'additional restoration after OFF'
                Text = $valid + "$baseline`r`n"
            },
            [pscustomobject]@{
                Name = 'malformed selected scalar record'
                Text = $valid.Replace(
                    '[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=25',
                    '[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=banana') + `
                    "[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=25`r`n"
            },
            [pscustomobject]@{
                Name = 'applied record before ON'
                Text = $valid.Replace(
                    '[ffx-hooks] F8 scalar edit key=cheats.ap_multiplier edit=SAVED requested=25 configured=25',
                    "[f8-runtime] key=cheats.ap_100x effective=1 source=UnmarkedCanonicalIni state=applied gate=01 scalar=25`r`n[ffx-hooks] F8 scalar edit key=cheats.ap_multiplier edit=SAVED requested=25 configured=25")
            },
            [pscustomobject]@{
                Name = 'duplicate selected OFF'
                Text = $valid.Replace(
                    '[ffx-hooks] F8 edit key=cheats.ap_100x edit=SAVED requested=0 effective=0 source=UnmarkedCanonicalIni',
                    "[ffx-hooks] F8 edit key=cheats.ap_100x edit=SAVED requested=0 effective=0 source=UnmarkedCanonicalIni`r`n[ffx-hooks] F8 edit key=cheats.ap_100x edit=SAVED requested=0 effective=0 source=UnmarkedCanonicalIni")
            }
        )
        foreach ($case in $extraCases) {
            Assert-Throws {
                Test-F8LogEvidence -CaseSpec $spec -SliceBytes ([System.Text.Encoding]::UTF8.GetBytes([string]$case.Text)) -ObservedApplied -ObservedRestored | Out-Null
            } 'scalar|selected|record|phase|order|exactly|additional|malformed' "scalar ambiguity accepted: $($case.Name)"
        }
    }

    Test-Case 'log rejects wrong selected effective/source and any other-target ON or apply' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]
        $wrongEffective = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText).Replace('requested=1 effective=1', 'requested=1 effective=0'))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $wrongEffective -ObservedApplied:$true -ObservedRestored:$true } 'effective|selected|ON' 'wrong selected effective accepted'
        $wrongSource = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText).Replace('source=UnmarkedCanonicalIni', 'source=Environment'))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $wrongSource -ObservedApplied:$true -ObservedRestored:$true } 'source|selected' 'wrong selected source accepted'
        $otherRequest = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine '[ffx-hooks] F8 edit key=input.dialog_skip edit=SAVED requested=1 effective=1 source=UnmarkedCanonicalIni'))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $otherRequest -ObservedApplied:$true -ObservedRestored:$true } 'other|target|input.dialog_skip' 'other-target ON request accepted'
        $otherApply = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine '[f8-runtime] key=cheats.always_critical effective=1 source=UnmarkedCanonicalIni state=applied readback=01'))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $otherApply -ObservedApplied:$true -ObservedRestored:$true } 'other|target|always_critical' 'other-target apply accepted'
        $wrongCaseKey = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText).Replace('boosters.speed_hack', 'Boosters.speed_hack'))
        Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $wrongCaseKey -ObservedApplied:$true -ObservedRestored:$true } 'other|target|selected' 'wrong-case selected key accepted'
    }

    Test-Case 'new log slice rejects every crash, exception, access-violation, conflict, and restore-pending variant' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]
        foreach ($bad in @('crash', 'EXCEPTION', 'access violation', 'ACCESS_VIOLATION', 'conflict', 'RestorePending', 'RESTORE PENDING')) {
            $bytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine "[bad] $bad"))
            Assert-Throws { Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$true -ObservedRestored:$true } 'unsafe|error|conflict|restore|crash|exception|violation' "unsafe token $bad accepted"
        }
    }

    Test-Case 'selected ON and OFF anchors require edit SAVED and reject every production edit failure' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]
        foreach ($editFailure in @('REJECTED NOT WIRED', 'REJECTED UNAVAILABLE', 'PERSIST FAILED', 'UNKNOWN')) {
            foreach ($requested in @('1', '0')) {
                $invalidText = (New-ValidLogText).Replace(
                    "edit=SAVED requested=$requested",
                    "edit=$editFailure requested=$requested"
                )
                $bytes = [System.Text.Encoding]::UTF8.GetBytes($invalidText)
                Assert-Throws {
                    Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$true -ObservedRestored:$true | Out-Null
                } 'edit|SAVED|REJECTED|PERSIST|UNKNOWN' "selected requested=$requested edit=$editFailure was accepted"
            }
        }
        $otherTargetFailure = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine '[ffx-hooks] F8 edit key=input.dialog_skip edit=UNKNOWN requested=0 effective=0 source=UnmarkedCanonicalIni'))
        Assert-Throws {
            Test-F8LogEvidence -CaseSpec $spec -SliceBytes $otherTargetFailure -ObservedApplied:$true -ObservedRestored:$true | Out-Null
        } 'edit|SAVED|UNKNOWN' 'other-target non-SAVED edit was accepted'
    }

    Test-Case 'structured runtime failures are rejected for selected and other targets even with valid anchors' {
        Require-Library
        $spec = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'speed_hack')[0]
        $none = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine '[f8-runtime] key=input.dialog_skip failure=none'))
        Assert-True (Test-F8LogEvidence -CaseSpec $spec -SliceBytes $none -ObservedApplied:$true -ObservedRestored:$true).Valid 'runtime failure=none was rejected'
        $failures = @('read-failed', 'write-failed', 'readback-failed', 'ownership-conflict', 'battle-gate-closed', 'memory-span-invalid')
        foreach ($target in @($spec.Canonical, 'input.dialog_skip')) {
            foreach ($failure in $failures) {
                $line = "[f8-runtime] key=$target failure=$failure"
                $bytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine $line))
                Assert-Throws {
                    Test-F8LogEvidence -CaseSpec $spec -SliceBytes $bytes -ObservedApplied:$true -ObservedRestored:$true | Out-Null
                } 'failure|runtime|unsafe' "runtime failure=$failure for $target was accepted"
            }
        }
        $mixedCase = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText -OtherLine '[f8-runtime] key=input.dialog_skip FAILURE=READ-FAILED'))
        Assert-Throws {
            Test-F8LogEvidence -CaseSpec $spec -SliceBytes $mixedCase -ObservedApplied:$true -ObservedRestored:$true | Out-Null
        } 'failure|runtime|unsafe' 'case-insensitive runtime failure field was accepted'
    }

    Test-Case 'protocol treats false confirmation switches as absent and rejects Verify-only switches in Preflight' {
        Require-Library
        $fixture = New-ProtocolFixture
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed:$false -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'DisposableSaveConfirmed|attestation' 'false disposable-save switch accepted'
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed:$false -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'EditorClosedConfirmed|attestation' 'false editor switch accepted'
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'Preflight|ObservedApplied|Verify-only' 'Verify-only observation accepted in Preflight'
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -RestoreConfigSnapshot -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'Preflight|RestoreConfigSnapshot|Verify-only' 'restore switch accepted in Preflight'

        $unexpectedEvidence = New-SeymourMemoryEvidenceFixture
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'seymour_battle_roster' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -SeymourEvidencePath $unexpectedEvidence.Path -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'Preflight|SeymourEvidencePath|Verify-only' 'Seymour Verify evidence was accepted in Preflight'
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -SeymourEvidencePath $unexpectedEvidence.Path -EvidenceDirectory 'unused' -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'Seymour|case|only' 'Seymour evidence was accepted for another case'

        $seymourFixture = New-ProtocolFixture
        $seymourPreflight = Invoke-F8Rt2Protocol -Case 'seymour_battle_roster' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $seymourFixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $seymourFixture.Paths -Providers $seymourFixture.Providers
        Add-ValidProtocolLog -Fixture $seymourFixture -Canonical 'boosters.playable_seymour' -ApplyMode 'RuntimeAcknowledged'
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nplayable_seymour=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($seymourFixture.Paths.Ini, $changed)
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'seymour_battle_roster' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $seymourPreflight.EvidenceDirectory -EvidenceRoot $seymourFixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $seymourFixture.Paths -Providers $seymourFixture.Providers
        } 'SeymourEvidencePath|raw-memory|evidence|required' 'generic attestations passed Seymour Verify without raw-memory evidence'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($seymourFixture.Paths.Ini)) 'missing Seymour evidence still restored the INI'
    }

    Test-Case 'Preflight creates unique integrity evidence without mutating INI and checks processes twice' {
        Require-Library
        $fixture = New-ProtocolFixture
        $before = [System.IO.File]::ReadAllBytes($fixture.Paths.Ini)
        $result = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Assert-True (Test-Path -LiteralPath $result.EvidenceDirectory -PathType Container) 'evidence directory missing'
        Assert-True (Test-Path -LiteralPath $result.SnapshotPath -PathType Leaf) 'snapshot missing'
        Assert-True (Test-Path -LiteralPath $result.ManifestPath -PathType Leaf) 'manifest missing'
        Assert-Equal 14 @($result.Resolutions).Count 'Preflight resolver count'
        Assert-Equal 0 @($result.Resolutions | Where-Object Value).Count 'Preflight found an ON case'
        Assert-Equal 2 $fixture.ProcessState.Calls 'Preflight process checks'
        Assert-Equal $before ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'Preflight mutated INI'
        Assert-False ([string]::IsNullOrEmpty([string]$result.SessionId)) 'session id missing'
    }

    Test-Case 'Preflight validates both reward scalar keys for every selected case before snapshot' {
        Require-Library
        $missing = New-ProtocolFixture
        $missingResult = Invoke-F8Rt2Protocol -Case 'dialog_skip' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $missing.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $missing.Paths -Providers $missing.Providers
        Assert-True (Test-Path -LiteralPath $missingResult.SnapshotPath -PathType Leaf) 'missing scalar keys did not resolve to compatibility defaults'

        $valid = New-ProtocolFixture
        $validText = [System.Text.Encoding]::UTF8.GetString((Get-AllOffIniBytes)).Replace(
            "[cheats]`n", "[cheats]`nap_multiplier=1`ngil_multiplier=100`n")
        [System.IO.File]::WriteAllBytes($valid.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes($validText))
        $validResult = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $valid.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $valid.Paths -Providers $valid.Providers
        Assert-True (Test-Path -LiteralPath $validResult.SnapshotPath -PathType Leaf) 'valid scalar boundaries were rejected'

        foreach ($key in @('ap_multiplier', 'gil_multiplier')) {
            foreach ($bad in @('0', '101', '-1', '2x', '2147483648')) {
                $fixture = New-ProtocolFixture
                $otherKey = if ($key -ceq 'ap_multiplier') { 'gil_multiplier' } else { 'ap_multiplier' }
                $text = [System.Text.Encoding]::UTF8.GetString((Get-AllOffIniBytes)).Replace(
                    "[cheats]`n", "[cheats]`n$key=$bad`n$otherKey=25`n")
                [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes($text))
                Assert-Throws {
                    Invoke-F8Rt2Protocol -Case 'permanent_sensor' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers | Out-Null
                } "$key|digits|1.*100|remove|repair" "invalid scalar $key=$bad reached evidence snapshot"
                $snapshots = @(if (Test-Path -LiteralPath $fixture.EvidenceRoot -PathType Container) {
                    Get-ChildItem -LiteralPath $fixture.EvidenceRoot -Recurse -Filter 'ffx-hooks.ini.snapshot.bin' -ErrorAction SilentlyContinue
                })
                Assert-Equal 0 $snapshots.Count "invalid scalar $key=$bad wrote a snapshot"
            }
        }
    }

    Test-Case 'Preflight second-check race creates no snapshot or manifest' {
        Require-Library
        $fixture = New-ProtocolFixture
        $race = [pscustomobject]@{ Calls = 0 }
        $fixture.Providers.ProcessProvider = {
            ++$race.Calls
            if ($race.Calls -eq 1) { return @() }
            return @([pscustomobject]@{ Name = 'FFX'; Id = 404 })
        }.GetNewClosure()
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'FFX|recheck' 'Preflight second-check race accepted'
        $snapshots = @(Get-ChildItem -LiteralPath $fixture.EvidenceRoot -Recurse -Filter 'ffx-hooks.ini.snapshot.bin' -ErrorAction SilentlyContinue)
        $manifests = @(Get-ChildItem -LiteralPath $fixture.EvidenceRoot -Recurse -Filter 'manifest.json' -ErrorAction SilentlyContinue)
        Assert-Equal 0 $snapshots.Count 'snapshot written after race'
        Assert-Equal 0 $manifests.Count 'manifest written after race'
        $partialDirectories = @(Get-ChildItem -LiteralPath $fixture.EvidenceRoot -Directory -ErrorAction SilentlyContinue)
        Assert-Equal 1 $partialDirectories.Count 'expected one quarantined incomplete evidence directory'
        $fixture.Providers.ProcessProvider = { @() }
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $partialDirectories[0].FullName -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'manifest|missing' 'incomplete evidence directory was accepted by Verify'
    }

    Test-Case 'Preflight final process recheck occurs after identity providers and before snapshot' {
        Require-Library
        $fixture = New-ProtocolFixture
        $state = [pscustomobject]@{ Active = $false }
        $fixture.Providers.ProcessProvider = {
            if ($state.Active) { return @([pscustomobject]@{ Name = 'FFX'; Id = 406 }) }
            return @()
        }.GetNewClosure()
        $fixture.Providers.CommitProvider = {
            $state.Active = $true
            return '2a8f8c540c3f9a9e22dcfa8adb9fd400282f47ec'
        }.GetNewClosure()
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'FFX|recheck' 'identity-provider race escaped the final Preflight process check'
        $snapshots = @(Get-ChildItem -LiteralPath $fixture.EvidenceRoot -Recurse -Filter 'ffx-hooks.ini.snapshot.bin' -ErrorAction SilentlyContinue)
        Assert-Equal 0 $snapshots.Count 'snapshot written after identity-provider race'
    }

    Test-Case 'Preflight rejects a selected Compose higher-authority OFF blocker' {
        Require-Library
        $fixture = New-ProtocolFixture
        $fixture.Providers.Environment['FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7'] = 'false'
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'arena_plus_compose_f7' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'block|Environment|future ON' 'Compose environment-false blocker accepted'
    }

    Test-Case 'Verify without restore authorization preserves the changed INI and names the snapshot' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        $logBytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText))
        $stream = New-Object System.IO.FileStream($fixture.Paths.Log, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        try { $stream.Write($logBytes, 0, $logBytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        [System.IO.File]::WriteAllBytes($fixture.Paths.Counter, [System.Text.Encoding]::ASCII.GetBytes('4'))
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[f8_authority]`nspeed_hack=1`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $fixture.ProcessState.Calls = 0
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'RestoreConfigSnapshot|snapshot' 'Verify restored without explicit authorization'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'unauthorized Verify mutated INI'
    }

    Test-Case 'authorized Verify restores exact bytes, validates all gates, and is repeat-safe' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflightBytes = [System.IO.File]::ReadAllBytes($fixture.Paths.Ini)
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        $logBytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText))
        $stream = New-Object System.IO.FileStream($fixture.Paths.Log, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        try { $stream.Write($logBytes, 0, $logBytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        [System.IO.File]::WriteAllBytes($fixture.Paths.Counter, [System.Text.Encoding]::ASCII.GetBytes('4'))
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n"))
        $fixture.ProcessState.Calls = 0
        $first = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Assert-True $first.FinalVerdict 'first Verify verdict false'
        Assert-Equal $preflightBytes ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'first Verify bytes differ'
        Assert-Equal 3 $fixture.ProcessState.Calls 'first Verify process checks'
        $fixture.ProcessState.Calls = 0
        $second = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Assert-True $second.FinalVerdict 'second Verify verdict false'
        Assert-Equal $preflightBytes ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'second Verify bytes differ'
        Assert-False ($first.VerifyEvidenceDirectory -ceq $second.VerifyEvidenceDirectory) 'repeated Verify reused an evidence directory'

        $seymourFixture = New-ProtocolFixture
        $seymourPreflightBytes = [System.IO.File]::ReadAllBytes($seymourFixture.Paths.Ini)
        $seymourPreflight = Invoke-F8Rt2Protocol -Case 'seymour_battle_roster' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $seymourFixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $seymourFixture.Paths -Providers $seymourFixture.Providers
        Add-ValidProtocolLog -Fixture $seymourFixture -Canonical 'boosters.playable_seymour' -ApplyMode 'RuntimeAcknowledged'
        [System.IO.File]::WriteAllBytes($seymourFixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nplayable_seymour=0`n[dashboard]`nenabled=1`n"))
        $seymourEvidence = New-SeymourMemoryEvidenceFixture
        $seymourFixture.ProcessState.Calls = 0
        $seymourVerified = Invoke-F8Rt2Protocol -Case 'seymour_battle_roster' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -SeymourEvidencePath $seymourEvidence.Path -EvidenceDirectory $seymourPreflight.EvidenceDirectory -EvidenceRoot $seymourFixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $seymourFixture.Paths -Providers $seymourFixture.Providers
        Assert-True $seymourVerified.FinalVerdict 'Seymour Verify verdict false with complete raw-memory evidence'
        Assert-Equal $seymourPreflightBytes ([System.IO.File]::ReadAllBytes($seymourFixture.Paths.Ini)) 'Seymour Verify bytes differ'
        $sealedSeymourPath = Join-Path $seymourVerified.VerifyEvidenceDirectory 'seymour-memory-evidence.json'
        Assert-True (Test-Path -LiteralPath $sealedSeymourPath -PathType Leaf) 'sealed Seymour memory evidence is missing'
        $seymourManifest = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($seymourVerified.VerifyManifestPath)) | ConvertFrom-Json -ErrorAction Stop
        Assert-Equal 6 @($seymourManifest.artifacts.PSObject.Properties).Count 'Seymour final manifest artifact count'
        Assert-FileRecordMatches -Record $seymourManifest.artifacts.seymourMemoryEvidence -ExpectedPath $sealedSeymourPath -Context 'sealed Seymour memory evidence'
        $seymourReadParams = @{
            VerifyEvidenceDirectory = $seymourVerified.VerifyEvidenceDirectory
            PreflightManifestPath = $seymourPreflight.ManifestPath
            ExpectedCase = 'seymour_battle_roster'
            ExpectedSessionId = $seymourPreflight.SessionId
        }
        [void](Read-F8VerifyIntegrityManifest @seymourReadParams)
        $sealedBytes = [System.IO.File]::ReadAllBytes($sealedSeymourPath)
        try {
            [System.IO.File]::WriteAllBytes($sealedSeymourPath, ([byte[]]$sealedBytes + [byte[]](0x20)))
            Assert-Throws { Read-F8VerifyIntegrityManifest @seymourReadParams } 'artifact|integrity|SHA|length' 'tampered Seymour memory evidence was accepted'
        } finally {
            [System.IO.File]::WriteAllBytes($sealedSeymourPath, $sealedBytes)
        }
    }

    Test-Case 'Verify writes a versioned final manifest after all exact artifacts and then its sidecar' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Add-ValidProtocolLog -Fixture $fixture
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n"))
        $fixture.ProcessState.Calls = 0
        $verified = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        $verifyManifestPath = Join-Path $verified.VerifyEvidenceDirectory 'verify-manifest.json'
        $verifySidecarPath = Join-Path $verified.VerifyEvidenceDirectory 'verify-manifest.sha256'
        Assert-True (Test-Path -LiteralPath $verifyManifestPath -PathType Leaf) 'versioned final Verify manifest is missing'
        Assert-True (Test-Path -LiteralPath $verifySidecarPath -PathType Leaf) 'final Verify manifest sidecar is missing'
        $manifestBytes = [System.IO.File]::ReadAllBytes($verifyManifestPath)
        $sidecar = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($verifySidecarPath))
        Assert-Equal (Get-Sha256Hex $manifestBytes) $sidecar 'final Verify manifest sidecar hash'
        $manifest = [System.Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json -ErrorAction Stop
        Assert-Equal 'ffx-hooks.f8-rt2-verify-evidence/v1' $manifest.schema 'final Verify manifest schema'
        Assert-Equal 4 @($manifest.preflight.PSObject.Properties).Count 'final manifest exact preflight record count'
        Assert-Equal 'speed_hack' $manifest.preflight.case 'final manifest preflight case link'
        Assert-Equal $preflight.SessionId $manifest.preflight.sessionId 'final manifest preflight session link'
        Assert-FileRecordMatches -Record $manifest.preflight.manifest -ExpectedPath $preflight.ManifestPath -Context 'preflight manifest link'
        $preflightSidecarPath = Join-Path $preflight.EvidenceDirectory 'manifest.sha256'
        Assert-FileRecordMatches -Record $manifest.preflight.sidecar -ExpectedPath $preflightSidecarPath -Context 'preflight manifest sidecar link'
        $expectedArtifacts = [ordered]@{
            logSlice = 'log-slice.bin'
            logSliceSha256 = 'log-slice.sha256'
            parsedLogVerdict = 'parsed-log-verdict.json'
            beforeAfterHashes = 'before-after-hashes.json'
            restorationVerdict = 'restoration-verdict.json'
        }
        Assert-Equal $expectedArtifacts.Count @($manifest.artifacts.PSObject.Properties).Count 'final manifest artifact count'
        foreach ($key in $expectedArtifacts.Keys) {
            Assert-FileRecordMatches -Record $manifest.artifacts.$key -ExpectedPath (Join-Path $verified.VerifyEvidenceDirectory $expectedArtifacts[$key]) -Context "final artifact $key"
        }
        $manifestTime = (Get-Item -LiteralPath $verifyManifestPath).LastWriteTimeUtc
        foreach ($leaf in $expectedArtifacts.Values) {
            Assert-True ((Get-Item -LiteralPath (Join-Path $verified.VerifyEvidenceDirectory $leaf)).LastWriteTimeUtc -le $manifestTime) "artifact $leaf was written after the final manifest"
        }
        Assert-True ($manifestTime -le (Get-Item -LiteralPath $verifySidecarPath).LastWriteTimeUtc) 'final manifest sidecar predates the manifest'
        Assert-True ([bool]$verified.FinalEvidenceReadbackValid) 'post-write final evidence readback was not reported valid'
    }

    Test-Case 'final Verify manifest readback rejects each artifact and manifest tamper' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Add-ValidProtocolLog -Fixture $fixture
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n"))
        $fixture.ProcessState.Calls = 0
        $verified = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        Assert-True (Test-Path -LiteralPath (Join-Path $verified.VerifyEvidenceDirectory 'verify-manifest.json') -PathType Leaf) 'final Verify manifest required for tamper readback is missing'
        $reader = Get-Command -Name Read-F8VerifyIntegrityManifest -ErrorAction SilentlyContinue
        Assert-True ($null -ne $reader) 'final Verify integrity reader is missing'
        $readParams = @{
            VerifyEvidenceDirectory = $verified.VerifyEvidenceDirectory
            PreflightManifestPath = $preflight.ManifestPath
            ExpectedCase = 'speed_hack'
            ExpectedSessionId = $preflight.SessionId
        }
        $loaded = Read-F8VerifyIntegrityManifest @readParams
        Assert-Equal 'ffx-hooks.f8-rt2-verify-evidence/v1' $loaded.schema 'valid final manifest readback'
        foreach ($leaf in @('log-slice.bin', 'log-slice.sha256', 'parsed-log-verdict.json', 'before-after-hashes.json', 'restoration-verdict.json')) {
            $path = Join-Path $verified.VerifyEvidenceDirectory $leaf
            $original = [System.IO.File]::ReadAllBytes($path)
            try {
                [System.IO.File]::WriteAllBytes($path, ([byte[]]$original + [byte[]](0x20)))
                Assert-Throws { Read-F8VerifyIntegrityManifest @readParams } 'artifact|integrity|SHA|length' "tampered final artifact $leaf was accepted"
            } finally {
                [System.IO.File]::WriteAllBytes($path, $original)
            }
        }
        $verifyManifestPath = Join-Path $verified.VerifyEvidenceDirectory 'verify-manifest.json'
        $originalManifest = [System.IO.File]::ReadAllBytes($verifyManifestPath)
        try {
            [System.IO.File]::WriteAllBytes($verifyManifestPath, ([byte[]]$originalManifest + [byte[]](0x20)))
            Assert-Throws { Read-F8VerifyIntegrityManifest @readParams } 'manifest|integrity|SHA' 'tampered final manifest was accepted'
        } finally {
            [System.IO.File]::WriteAllBytes($verifyManifestPath, $originalManifest)
        }

        $verifySidecarPath = Join-Path $verified.VerifyEvidenceDirectory 'verify-manifest.sha256'
        $originalSidecar = [System.IO.File]::ReadAllBytes($verifySidecarPath)
        try {
            [System.IO.File]::WriteAllBytes($verifySidecarPath, [System.Text.Encoding]::ASCII.GetBytes(('0' * 64)))
            Assert-Throws { Read-F8VerifyIntegrityManifest @readParams } 'manifest|integrity|SHA|sidecar' 'tampered final manifest sidecar was accepted'
        } finally {
            [System.IO.File]::WriteAllBytes($verifySidecarPath, $originalSidecar)
        }

        foreach ($preflightLeaf in @('manifest.json', 'manifest.sha256')) {
            $preflightPath = Join-Path $preflight.EvidenceDirectory $preflightLeaf
            $preflightBytes = [System.IO.File]::ReadAllBytes($preflightPath)
            try {
                [System.IO.File]::WriteAllBytes($preflightPath, ([byte[]]$preflightBytes + [byte[]](0x20)))
                Assert-Throws { Read-F8VerifyIntegrityManifest @readParams } 'preflight|manifest|sidecar|integrity|SHA|length' "tampered Preflight $preflightLeaf was accepted by final readback"
            } finally {
                [System.IO.File]::WriteAllBytes($preflightPath, $preflightBytes)
            }
        }

        $missingArtifactPath = Join-Path $verified.VerifyEvidenceDirectory 'parsed-log-verdict.json'
        $missingArtifactBytes = [System.IO.File]::ReadAllBytes($missingArtifactPath)
        try {
            [System.IO.File]::Delete($missingArtifactPath)
            Assert-Throws { Read-F8VerifyIntegrityManifest @readParams } 'artifact|missing|integrity' 'incomplete final evidence bundle was accepted'
        } finally {
            [System.IO.File]::WriteAllBytes($missingArtifactPath, $missingArtifactBytes)
        }
    }

    Test-Case 'Verify rejects post-validation Preflight identity drift before final evidence success' {
        Require-Library
        foreach ($mode in @('coordinated', 'manifest-only', 'sidecar-only')) {
            $fixture = New-ProtocolFixture
            $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
            Add-ValidProtocolLog -Fixture $fixture
            [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n"))
            $snapshotBytes = [System.IO.File]::ReadAllBytes($preflight.SnapshotPath)
            $state = [pscustomobject]@{ Calls = 0 }
            $fixture.Providers.BeforeFinalEvidenceProvider = {
                param($manifestPath, $sidecarPath, $verifyDirectory)
                ++$state.Calls
                if ($mode -ceq 'coordinated' -or $mode -ceq 'manifest-only') {
                    $manifest = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($manifestPath)) | ConvertFrom-Json -ErrorAction Stop
                    $manifest | Add-Member -Force -NotePropertyName postValidationRewrite -NotePropertyValue $mode
                    $json = ConvertTo-Json -InputObject $manifest -Depth 20 -Compress
                    $bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($json)
                    [System.IO.File]::WriteAllBytes($manifestPath, $bytes)
                    if ($mode -ceq 'coordinated') {
                        # This provider also executes outside the harness helper scope.
                        $sha = [System.Security.Cryptography.SHA256]::Create()
                        try {
                            $hash = (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('X2') }) -join '')
                        } finally {
                            $sha.Dispose()
                        }
                        [System.IO.File]::WriteAllBytes($sidecarPath, [System.Text.Encoding]::ASCII.GetBytes($hash))
                    }
                } else {
                    [System.IO.File]::WriteAllBytes($sidecarPath, [System.Text.Encoding]::ASCII.GetBytes(('0' * 64)))
                }
                return $true
            }.GetNewClosure()
            $fixture.ProcessState.Calls = 0
            $result = $null
            $failure = ''
            try {
                $result = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
            } catch {
                $failure = $_.Exception.Message
            }
            Assert-Equal 1 $state.Calls "$mode final-evidence provider call count"
            Assert-True ($null -eq $result) "$mode returned a FinalVerdict/evidence success result"
            Assert-True ($failure -match '(?i)pinned Preflight manifest(?: sidecar)? identity changed') "$mode mismatch was not explicit: $failure"
            Assert-Equal $snapshotBytes ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) "$mode failed before completing the already-authorized restoration"
            $verifyDirectories = @([System.IO.Directory]::GetDirectories($preflight.EvidenceDirectory) | Where-Object { (Split-Path -Leaf $_) -cmatch '^\d{8}T\d{9}Z_verify_[0-9a-f]{32}$' })
            Assert-Equal 1 $verifyDirectories.Count "$mode Verify evidence directory count"
            foreach ($artifact in @('log-slice.bin', 'log-slice.sha256', 'parsed-log-verdict.json', 'before-after-hashes.json', 'restoration-verdict.json')) {
                Assert-True (Test-Path -LiteralPath (Join-Path $verifyDirectories[0] $artifact) -PathType Leaf) "$mode incomplete bundle lost artifact $artifact"
            }
            Assert-False (Test-Path -LiteralPath (Join-Path $verifyDirectories[0] 'verify-manifest.json') -PathType Leaf) "$mode wrote a final Verify manifest after pinned identity drift"
            Assert-False (Test-Path -LiteralPath (Join-Path $verifyDirectories[0] 'verify-manifest.sha256') -PathType Leaf) "$mode wrote a final Verify sidecar after pinned identity drift"
        }
    }

    Test-Case 'Verify restore remains blocked by invalid log evidence and a second-check process race' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        [System.IO.File]::WriteAllBytes($fixture.Paths.Counter, [System.Text.Encoding]::ASCII.GetBytes('4'))
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'catalog|anchor|log' 'invalid log did not block restore'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'invalid-log Verify mutated INI'

        $logBytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText))
        $stream = New-Object System.IO.FileStream($fixture.Paths.Log, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        try { $stream.Write($logBytes, 0, $logBytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        $race = [pscustomobject]@{ Calls = 0 }
        $fixture.Providers.ProcessProvider = {
            ++$race.Calls
            if ($race.Calls -eq 1) { return @() }
            return @([pscustomobject]@{ Name = 'FFXProjectEditor'; Id = 505 })
        }.GetNewClosure()
        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'FFXProjectEditor|recheck' 'Verify second-check race accepted'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'race Verify mutated INI'
    }

    Test-Case 'Verify process recheck occurs after flushed temp creation and immediately before replace' {
        Require-Library
        $fixture = New-ProtocolFixture
        $preflight = Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Preflight -DisposableSaveConfirmed -EditorClosedConfirmed -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        $logBytes = [System.Text.Encoding]::UTF8.GetBytes((New-ValidLogText))
        $stream = New-Object System.IO.FileStream($fixture.Paths.Log, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        try { $stream.Write($logBytes, 0, $logBytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        [System.IO.File]::WriteAllBytes($fixture.Paths.Counter, [System.Text.Encoding]::ASCII.GetBytes('4'))
        $changed = [System.Text.Encoding]::UTF8.GetBytes("[boosters]`nspeed_hack=0`n[dashboard]`nenabled=1`n")
        [System.IO.File]::WriteAllBytes($fixture.Paths.Ini, $changed)
        $snapshotBytes = [System.IO.File]::ReadAllBytes($preflight.SnapshotPath)
        $destinationDirectory = Split-Path -Parent $fixture.Paths.Ini
        $destinationLeaf = Split-Path -Leaf $fixture.Paths.Ini
        $ownedTempPattern = '^\.' + [regex]::Escape($destinationLeaf) + '\.restore\.[0-9a-f]{32}\.tmp$'
        $order = [pscustomobject]@{ ProcessCalls = 0; TempCountAtRecheck = -1; TempBytes = $null; MoveCalls = 0 }
        $fixture.Providers.ProcessProvider = {
            ++$order.ProcessCalls
            if ($order.ProcessCalls -eq 3) {
                $temps = @([System.IO.Directory]::GetFiles($destinationDirectory) | Where-Object { (Split-Path -Leaf $_) -cmatch $ownedTempPattern })
                $order.TempCountAtRecheck = $temps.Count
                if ($temps.Count -eq 1) { $order.TempBytes = [System.IO.File]::ReadAllBytes($temps[0]) }
                return @([pscustomobject]@{ Name = 'FFXProjectEditor'; Id = 506 })
            }
            return @()
        }.GetNewClosure()
        $fixture.Providers.MoveProvider = { param($temporary, $destination) ++$order.MoveCalls; return $true }.GetNewClosure()

        Assert-Throws {
            Invoke-F8Rt2Protocol -Case 'speed_hack' -Phase Verify -DisposableSaveConfirmed -EditorClosedConfirmed -ObservedApplied -ObservedRestored -RestoreConfigSnapshot -EvidenceDirectory $preflight.EvidenceDirectory -EvidenceRoot $fixture.EvidenceRoot -ScriptRoot $repoScriptRoot -Paths $fixture.Paths -Providers $fixture.Providers
        } 'FFXProjectEditor|recheck' 'Verify before-replace process race accepted'
        Assert-Equal 1 $order.TempCountAtRecheck 'process recheck did not observe the flushed owned temp'
        Assert-Equal $snapshotBytes $order.TempBytes 'owned temp bytes were unavailable or differed at process recheck'
        Assert-Equal 3 $order.ProcessCalls 'Verify did not retain the early gates plus the final pre-replace recheck'
        Assert-Equal 0 $order.MoveCalls 'MoveProvider ran after the process recheck failed'
        Assert-Equal $changed ([System.IO.File]::ReadAllBytes($fixture.Paths.Ini)) 'destination changed after the process recheck failed'
        $leftovers = @([System.IO.Directory]::GetFiles($destinationDirectory) | Where-Object { (Split-Path -Leaf $_) -cmatch $ownedTempPattern })
        Assert-Equal 0 $leftovers.Count 'owned temp survived the failed process recheck'
    }

    Test-Case 'final verdict is true only when every observation, byte, hash, OFF, and restoration gate passes' {
        Require-Library
        $valid = [ordered]@{
            ObservedApplied = $true
            ObservedRestored = $true
            LogValid = $true
            LogPrefixValid = $true
            SnapshotBytesValid = $true
            BuiltDllHashUnchanged = $true
            InstalledDllHashUnchanged = $true
            ExecutableHashUnchanged = $true
            IniHashRestored = $true
            AllFourteenOff = $true
            RestorationVerified = $true
        }
        Assert-True (Test-F8FinalVerdict -Checks $valid) 'complete gate set rejected'
        foreach ($key in @($valid.Keys)) {
            $mutated = [ordered]@{}
            foreach ($copyKey in $valid.Keys) { $mutated[$copyKey] = $valid[$copyKey] }
            $mutated[$key] = $false
            Assert-False (Test-F8FinalVerdict -Checks $mutated) "verdict stayed true with $key=false"
        }
        $missing = [ordered]@{}
        foreach ($copyKey in @($valid.Keys | Select-Object -Skip 1)) { $missing[$copyKey] = $valid[$copyKey] }
        Assert-False (Test-F8FinalVerdict -Checks $missing) 'verdict stayed true with a missing gate'
    }

    Test-Case 'Compose protocol is gate-only and exposes no battle-file or launch action' {
        Require-Library
        $compose = @(Get-F8Rt2CaseTable | Where-Object Case -eq 'arena_plus_compose_f7')[0]
        Assert-Equal 'Config/menu gating only; do not launch Compose or mutate battle files.' $compose.ManualScope 'Compose manual scope'
        $sources = (Get-Content -Raw -LiteralPath $ScriptPath) + "`n" + (Get-Content -Raw -LiteralPath $ModulePath)
        Assert-False ($sources -match '(?im)LaunchComboBattle|ArenaMultiBossLab|battle\\btl|Copy-Item') 'Compose functional/deploy action leaked into gate-only protocol'
    }

    Test-Case 'F8 dashboard document publishes exact catalog, defaults, modes, addresses, and lifecycle truth' {
        $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $repoScriptRoot '..\..\..'))
        $text = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\F8_DASHBOARD.md'))
        # WHY: File.ReadAllText preserves CRLF, so the line anchor must admit the
        # carriage return without weakening the exact reviewed-heading contract.
        Assert-TextMatch $text '(?im)^# F8 Dashboard \u2014 reviewed branch candidate\r?$' 'dashboard heading does not identify the reviewed branch candidate'
        Assert-TextNotMatch $text '(?i)committed HEAD|deployed branch|Production branch' 'dashboard falsely claims main, deployment, or Production status'
        Assert-TextMatch $text '(?s)Plugins, Boosters, Cheats,\s*Scout, Arena\+, Input, Dev, Lab' 'eight exact F8 tab names missing'
        Assert-TextMatch $text '36 rows:\s*14 LIVE, 15 RESTART REQUIRED, 7 NOT WIRED' '36/14/15/7 catalog truth missing'
        Assert-TextMatch $text '(?s)tracked INI.*built-in fallback.*enabled=1' 'dashboard template ON truth missing'
        Assert-TextMatch $text '11 `RuntimeAcknowledged`' '11 RuntimeAcknowledged truth missing'
        Assert-TextMatch $text 'three `ConfigPolled`' '3 ConfigPolled truth missing'
        Assert-TextMatch $text 'all 14 LIVE target\s*defaults are false' '14 LIVE defaults false truth missing'
        Assert-TextMatch $text '(?is)Speed Hack.*Ctrl\+Shift\+K.*1x/2x/4x/8x.*indicator' 'Ctrl+Shift+K Speed Hack cycle and indicator truth missing'
        Assert-TextMatch $text '(?is)Speed Hack.*Native.*2x/4x.*ARMED.*Fast field scenes.*FMV.*unsupported' 'Speed backend/telemetry/FMV boundary missing'
        Assert-TextMatch $text '(?i)F12 is not owned by Speed Hack' 'F12 release truth missing'
        Assert-TextMatch $text '(?is)Back.*Cancel.*idempotent.*clean' 'FLAGS Back/Cancel clean-exit contract missing'
        Assert-TextMatch $text '(?is)selected.*description.*header subtitle.*technical status.*inside the main panel' 'FLAGS selected-description/status layout truth missing'
        Assert-TextMatch $text '(?is)mouse.*tab-only.*row.*deferred' 'FLAGS tab-only mouse/deferred-row truth missing'
        Assert-TextMatch $text '(?is)Aurora developer UI.*default OFF.*explicit.*Ctrl\+Alt\+F9.*Ctrl\+Alt\+F10.*plain F9/F10.*not Aurora hotkeys' 'Aurora explicit-gate/chord/plain-key truth missing'
        Assert-TextMatch $text '(?is)Present producer.*preserved' 'Present producer preservation missing'
        foreach ($literal in @('0x00D2A8F8', '0x01F10EA0', '0x01F10EC4', '0x00D32060', '0x00D32088', 'exactly 7 contiguous bytes', '33 ms')) {
            Assert-True $text.Contains($literal) "F8 dashboard address/width/lifecycle truth missing: $literal"
        }
        Assert-TextMatch $text '(?s)Playable Seymour is \*\*LIVE only as an experimental battle-roster consumer\*\*.*default OFF.*RAM-only.*Sphere Grid.*0x00383ED0.*0x0038321C.*0x00381C76.*0x003821CF.*0x00384010.*0x00386080.*0x00390F07.*original exactly once.*0x00386A70.*0x00D32494.*0x00D307E8.*0x00D307EB.*0x00D2C895.*0x00D2C8A3.*RT2-pending' 'Seymour experimental shared-seam boundary missing'
        Assert-TextMatch $text '(?s)Compose F7.*gate-only' 'Compose gate-only boundary missing'
        Assert-TextMatch $text 'Dynamic `FreeLibrary`/hot unload is unsupported' 'hot-unload boundary missing'
        Assert-TextMatch $text '(?is)AP.*Gil.*shared.*page.*protection.*peer.*defer' 'shared-page protection ambiguity exception missing'
        Assert-TextMatch $text '(?is)Preflight.*both.*cheats\.ap_multiplier.*cheats\.gil_multiplier.*before.*snapshot' 'two-scalar Preflight validation missing'
        Assert-TextMatch $text 'state=restored gate=00 scalar=none' 'truthful scalar OFF log contract missing'
    }

    Test-Case 'README, roadmap, known-bugs, and Maechen records reject stale or premature claims' {
        $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $repoScriptRoot '..\..\..'))
        $paths = @('README.md', 'docs\ROADMAP.md', 'docs\KNOWN_BUGS.md')
        $combined = ($paths | ForEach-Object { [System.IO.File]::ReadAllText((Join-Path $repoRoot $_)) }) -join "`n"
        foreach ($pattern in @(
            '(?i)\[dashboard\]\s*enabled=0',
            '(?i)56 rows',
            '(?i)30 items',
            '(?i)Tab 4:\s*Field',
            '(?i)Playable Seymour (?:is )?fully wired',
            '(?i)30\s*Hz timer',
            '(?i)0xD2A8F8\b',
            '(?i)0x1F10EA0\b',
            '(?i)0x1F10EC4\b',
            '(?is)Speed Hack.{0,160}2x/4x (?:are )?gameplay',
            '(?is)Speed Hack.{0,200}8x (?:is )?(?:the )?(?:distinct )?cutscene'
        )) {
            Assert-TextNotMatch $combined $pattern "stale public F8 claim remains: $pattern"
        }
        foreach ($path in $paths) {
            $text = [System.IO.File]::ReadAllText((Join-Path $repoRoot $path))
            Assert-TextMatch $text '(?s)36.*14 LIVE.*15 RESTART REQUIRED.*7 NOT WIRED' "$path lacks catalog truth"
            Assert-TextMatch $text '(?i)RT2 pending|RT2-pending|no F8 RT2' "$path lacks RT2-pending boundary"
        }
        $roadmap = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\ROADMAP.md'))
        $knownBugs = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\KNOWN_BUGS.md'))
        Assert-TextNotMatch $combined '(?is)hooks install\s+only (?:if|when).*target bytes.*expected signature' 'public docs retain a universal hook signature-install claim'
        Assert-TextNotMatch $combined '(?i)never corrupts' 'public docs retain a universal no-corruption claim'
        Assert-TextMatch $combined '(?is)Speed Hack.*Native.*2x/4x.*ARMED.*field-scene.*RT2.*FMV.*unsupported' 'public docs lack the corrected Speed ownership/support boundary'
        Assert-TextMatch $knownBugs '(?is)Only specific supported-profile/expected-byte inline-patch families fail closed on mismatch.*validated sites.*Dialog Skip.*beta.*RT2-pending' 'known bugs lacks the scoped validated-family signature-safety boundary'
        Assert-TextMatch $roadmap '(?is)Maechen F9 transport restoration.*future.*no.*client-side.*credential' 'roadmap lacks the separate credential-free Maechen F9 future-work boundary'
        Assert-TextMatch $roadmap '(?is)configurable AP/Gil multipliers.*future' 'roadmap lacks the separate configurable AP/Gil future-work boundary'
        Assert-TextMatch $knownBugs '(?is)Back/Cancel black-screen regression.*fixed.*RT2 pending' 'known bugs lacks the fixed-branch/RT2-pending Back/Cancel boundary'
        Assert-TextMatch $knownBugs '(?is)mouse row navigation.*deferred' 'known bugs lacks the deferred mouse-row boundary'

        $maechenPath = Join-Path $repoRoot 'docs\MAECHEN_F9_CLIENT.md'
        Assert-True (Test-Path -LiteralPath $maechenPath -PathType Leaf) 'Maechen operator/client record is missing'
        $maechen = [System.IO.File]::ReadAllText($maechenPath)
        $handoff = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\ai\SESSION_HANDOFF.md'))
        $plan = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\superpowers\plans\2026-08-20-maechen-f9-client.md'))

        $clientBoundary = Get-UniqueBoundedMarkdownSection -Text $maechen -Heading '# Maechen F9 client' -Level 1
        # WHY: Windows PowerShell 5.1 decodes UTF-8-without-BOM scripts as ANSI,
        # so construct the Markdown em dash from ASCII-only source text.
        $maechenHandoffHeading = '## 2026-08-22 {0} Maechen F9 offline client boundary' -f ([char]0x2014)
        $handoffBoundary = Get-UniqueBoundedMarkdownSection -Text $handoff -Heading $maechenHandoffHeading -Level 2
        Assert-MaechenOfflineBoundary -Text $clientBoundary -RequireEndpointField -RequireIniBlock -RequireHostMarkers -RequireCanonicalProduction -RequireClientCredentialDeclaration
        Assert-MaechenOfflineBoundary -Text $handoffBoundary -RequireHandoffCredentialDeclaration

        Assert-TextMatch $clientBoundary '(?im)^\*\*Hooks base:\*\* `d1368089d960caca77c0a5b47c77cb5b3352eee5`\r?$' 'Maechen record lacks the exact Hooks base'
        Assert-TextMatch $handoffBoundary '(?is)approved Hooks base\s+`d1368089d960caca77c0a5b47c77cb5b3352eee5`' 'Maechen handoff lacks the exact Hooks base'
        Assert-TextMatch $clientBoundary '(?is)`f40dea4df30c7be675a4134c7070e20f2a9612fb` \(error taxonomy and modal guard\).*`abb4410d1597accc0204f55101c8953d6ee4ab4a` \(rejected-open rollback\).*`ad2aaaf2adbfd6452f2c627089a9df20c535bfd2` \(atomic F8 open latch\).*`5ddafde6d28ebbea85c329c84b60b23ff4240efc` \(modal and input rejection\).*`bf75747cc5d7ee3dfe70c50ba1728081d7df6e50` \(deferred arena ownership\)' 'Maechen record lacks the final scoped runtime lineage'
        Assert-TextMatch $handoffBoundary '(?is)`f40dea4df30c7be675a4134c7070e20f2a9612fb`.*`abb4410d1597accc0204f55101c8953d6ee4ab4a`.*`ad2aaaf2adbfd6452f2c627089a9df20c535bfd2`.*`5ddafde6d28ebbea85c329c84b60b23ff4240efc`.*`bf75747cc5d7ee3dfe70c50ba1728081d7df6e50`' 'Maechen handoff lacks the final scoped runtime lineage'
        Assert-TextMatch $clientBoundary '(?is)native F9 route allows exactly one provider candidate.*`poolside/laguna-s-2\.1-free`.*18-second candidate budget.*25-second request deadline.*`maxRetries: 0`.*`maxOutputTokens: 256`.*no\s+native\s+Verboo\s+or\s+other\s+direct-provider\s+fallback.*`AI_GATEWAY_API_KEY`.*server-only.*`MAECHEN_LEGACY_CHAT_ENABLED=true`.*`MAECHEN_LEGACY_PROVIDER_ALLOWLIST_VERIFIED=true`.*`MAECHEN_AUTO_TOPUP_CONFIRMED_OFF=true`' 'Maechen record lacks the exact Gateway-only architecture, budgets, gates, or server-only credential truth'
        Assert-TextNotMatch $clientBoundary '(?is)Gateway Laguna S 2\.1 Free first.{0,220}three Verboo unversioned|then\s+the\s+three\s+Verboo\s+unversioned' 'Maechen record retains the forbidden native Verboo/direct fallback architecture'
        Assert-TextNotMatch $clientBoundary '`maxOutputTokens: 512`' 'Maechen record retains the stale native output ceiling'
        Assert-TextMatch $handoffBoundary '(?is)Website source remains local and unpublished.{0,240}validated question.{0,120}provider user prompt' 'Maechen handoff lacks local Website provenance and user-prompt truth'
        Assert-TextMatch $clientBoundary '(?is)fixed local messages.{0,180}invalid-question.{0,120}rate-limit.{0,120}retry-later.{0,240}`Question rejected\. Edit and retry`.{0,180}`Too many requests\. Try again later`.{0,180}`Service unavailable\. Try again later`' 'Maechen record lacks the fixed local error taxonomy'
        Assert-TextMatch $clientBoundary '(?is)shared F8/F9 arbiter.{0,180}terminal reap wake.{0,180}rejected-open\s+rollback.{0,180}atomically releasing the F8 open latch' 'Maechen record lacks the wake/rollback/atomic-latch arbiter truth'
        Assert-TextMatch $handoffBoundary '(?is)F8/F9 arbiter.{0,180}reap wake.{0,180}rollback.{0,180}atomic F8 open latch' 'Maechen handoff lacks the wake/rollback/atomic-latch arbiter truth'
        Assert-TextMatch $clientBoundary '(?is)F8 runtime RT0: `2830/2830`.*F7 configuration RT0: `24/24`.*PowerShell protocol RT0: `76/76`.*Targeted Website review: `APPROVE` with no remaining Critical or Important finding.*Targeted runtime/UI scoped review: `APPROVE` with no remaining Critical or\s+Important finding' 'Maechen record lacks final offline gates and targeted-review truth'
        Assert-TextMatch $clientBoundary '(?is)Read-only whole-branch cross review: `APPROVE`.*Website reviewed object: `253639e4425259fec60cd6500ef1322e2f7eadce`.*Hooks reviewed head: `cc9f3e0f5ac93cbc0eb9fbc341b7c7af579b71e6`.*Findings: no Critical, Important, or Minor findings.*Review method: static/object-pinned; it did not rerun tests, build, network, or live actions.*Decision: approved `OFFLINE CANDIDATE` only' 'Maechen record lacks exact whole-branch review proof or offline-candidate boundary'
        Assert-TextMatch $handoffBoundary '(?is)read-only whole-branch cross review returned `APPROVE` for Website\s+`253639e4425259fec60cd6500ef1322e2f7eadce` and Hooks reviewed head\s+`cc9f3e0f5ac93cbc0eb9fbc341b7c7af579b71e6`.*no Critical, Important, or Minor\s+findings.*static/object-pinned.*did not rerun tests, build, network, or live\s+actions.*approved `OFFLINE CANDIDATE` only' 'Maechen handoff lacks exact whole-branch review proof or offline-candidate boundary'
        Assert-TextNotMatch ($clientBoundary + "`n" + $handoffBoundary) '(?is)explicit whole-branch `?APPROVE`? remains pending|no\s+whole-branch\s+cross review' 'Maechen authority retains stale pending or no-cross-review prose'
        Assert-TextMatch $clientBoundary '(?is)root-owned Release artifact.*`src/runtime/FfxHooksDll/bin/Release/ffx-hooks\.dll`.*unchanged Hooks\s+runtime-source\s+head\s+`bf75747cc5d7ee3dfe70c50ba1728081d7df6e50`.*PE32/i386.*1,240,576 bytes.*`5DAEB5AAC15E6EAEE3B434DF1D49FA5E6206F798362D1C5AFB6DF7536F401BDA`.*file last-write\s+UTC\s+`2026-08-22T11:51:31\.5848062Z`.*PE-header\s+timestamp\s+`2026-08-22T11:51:31Z`.*`src/runtime/FfxHooksDll/bin/Release/ffx-hooks-polyhook-lab\.dll`.*byte-identical.*four historical warnings.*No deploy,\s+network request,\s+game launch, or RT2 occurred.*`3AE6E2453FF92BB9A591F4FC87DE3D601A70CAE31C4BF8937EE5390E44A6C61A`.*superseded.*earlier intermediates.*not canonical' 'Maechen record lacks the refreshed root-owned artifact identity, build provenance, or offline boundary'
        Assert-TextMatch $clientBoundary '(?is)Imports: `KERNEL32\.dll`, `USER32\.dll`, `GDI32\.dll`, `d3d11\.dll`, and\s+`WINHTTP\.dll`.*all 10 bounded WinHTTP request APIs.*Exports: `FF10HgetName` and `FF10HgetVer`' 'Maechen record lacks exact import/export counts'
        Assert-TextMatch $handoffBoundary '(?is)expected five DLL families, all 10 bounded\s+`WINHTTP\.dll` APIs.*exports `FF10HgetName`/`FF10HgetVer`' 'Maechen handoff lacks exact import/export counts'
        Assert-TextMatch $clientBoundary '(?is)deterministic\s+Yunalesca\s+request.*HTTP\s+200.*`X-Maechen-Protocol:\s+1`.*provider-backed\s+ASCII\s+question.*HTTP\s+200.*`X-Maechen-Protocol:\s+1`.*Hooks\s+DLL\s+deployment.*game\s+RT2.*remain\s+pending' 'Maechen record lacks both canonical production probes or the pending Hooks RT2 boundary'
        Assert-TextMatch $handoffBoundary '(?is)no deploy.*no game launch.*no network request.*no RT2.*no push.*no Production promotion' 'Maechen handoff lacks the offline-only action boundary'
        Assert-TextMatch $handoffBoundary '(?is)F8 runtime RT0 passed `2830/2830`.*F7 configuration RT0 passed\s+`24/24`.*PowerShell protocol RT0 passed\s+`76/76`.*root-owned Release DLL.*`src/runtime/FfxHooksDll/bin/Release/ffx-hooks\.dll`.*unchanged runtime-source\s+head\s+`bf75747cc5d7ee3dfe70c50ba1728081d7df6e50`.*PE32/i386.*1,240,576 bytes.*`5DAEB5AAC15E6EAEE3B434DF1D49FA5E6206F798362D1C5AFB6DF7536F401BDA`.*file last-write\s+UTC\s+`2026-08-22T11:51:31\.5848062Z`.*PE-header\s+timestamp\s+`2026-08-22T11:51:31Z`.*lab copy\s+`src/runtime/FfxHooksDll/bin/Release/ffx-hooks-polyhook-lab\.dll`.*byte-identical.*four historical warnings.*`3AE6E2453FF92BB9A591F4FC87DE3D601A70CAE31C4BF8937EE5390E44A6C61A`.*superseded.*earlier intermediates.*not canonical.*no deploy.*no game launch.*no network request.*no RT2' 'Maechen handoff lacks final gate, refreshed root-owned artifact, build provenance, or offline boundary'
        Assert-TextMatch $plan '(?im)^- \[x\] Offline source, protocol, UI, targeted reviews, RT0, and Release-build tasks through Task 6 plus final review fixes\r?$' 'Maechen plan does not record the completed offline review-fix boundary'
        Assert-TextMatch $plan '(?is)\[x\] Canonical Vercel server publication.*\[ \] Firebase mirror parity.*\[ \] DLL deployment.*\[ \] manual RT2' 'Maechen plan does not separate completed canonical publication from pending mirror/runtime gates'
        Assert-TextMatch $plan '(?im)^- \[ \] game launch or native client/server RT2\r?$' 'Maechen plan does not keep the remaining live-game gate unchecked'

        # WHY: every mutant adds one realistic false claim to an otherwise-valid
        # authority block, proving the validator rejects additive contradictions.
        $firstMarkerUnchecked = [regex]::Replace(
            $clientBoundary,
            '(?m)^- \[[xX]\] (`https://ffxmodstudio\.com/release\.json`)',
            '- [ ] $1',
            1)
        $duplicateMarker = $clientBoundary + "`n" + '- [x] `https://ffxmodstudio.com/release.json` - `9ceb69aa2c44652020339c33884af761356e356a`' + "`n"
        $missingDefaultOff = [regex]::Replace($clientBoundary, '(?i)The feature is default OFF\.', 'The feature configuration is explicit.', 1)
        $mutations = [ordered]@{
            UncheckedCanonicalProof = [pscustomobject]@{ Text = $firstMarkerUnchecked; Error = 'checked states contradict' }
            DuplicateHostProof = [pscustomobject]@{ Text = $duplicateMarker; Error = 'exactly three host markers' }
            ConfigurableUrl = [pscustomobject]@{ Text = $clientBoundary + "`nExternal configurable URL: https://evil.example/api.`n"; Error = 'positive contradictory claim' }
            ConfigurableHost = [pscustomobject]@{ Text = $clientBoundary + "`nConfigurable host: evil.example.`n"; Error = 'positive contradictory claim' }
            ProviderKey = [pscustomobject]@{ Text = $clientBoundary + "`nProvider key: exposed-value`n"; Error = 'positive contradictory claim' }
            BareApiKey = [pscustomobject]@{ Text = $clientBoundary + "`nAPI key: exposed-value`n"; Error = 'positive contradictory claim' }
            ClientToken = [pscustomobject]@{ Text = $clientBoundary + "`nClient token: exposed-value`n"; Error = 'positive contradictory claim' }
            ClientCredential = [pscustomobject]@{ Text = $clientBoundary + "`nClient credential: exposed-value`n"; Error = 'positive contradictory claim' }
            ClientAuthorization = [pscustomobject]@{ Text = $clientBoundary + "`nClient Authorization: Bearer exposed-value`n"; Error = 'positive contradictory claim' }
            ClientCookie = [pscustomobject]@{ Text = $clientBoundary + "`nClient cookie: session=exposed`n"; Error = 'positive contradictory claim' }
            MissingDefaultOff = [pscustomobject]@{ Text = $missingDefaultOff; Error = 'lacks public/no-auth, default-OFF, or local-wait truth' }
            DefaultOnF9 = [pscustomobject]@{ Text = $clientBoundary + "`nPlain F9 is enabled by default.`n"; Error = 'positive contradictory claim' }
            ChainOfThought = [pscustomobject]@{ Text = $clientBoundary + "`nModel chain-of-thought is displayed.`n"; Error = 'positive contradictory claim' }
            ProviderReasoning = [pscustomobject]@{ Text = $clientBoundary + "`nProvider reasoning is displayed.`n"; Error = 'positive contradictory claim' }
            DuplicateAuthorityField = [pscustomobject]@{ Text = $clientBoundary + "`n" + '**Server release SHA:** `published-value`' + "`n"; Error = 'must occur exactly once' }
        }
        foreach ($mutation in $mutations.GetEnumerator()) {
            $mutatedBoundary = [string]$mutation.Value.Text
            Assert-False ($mutatedBoundary -ceq $clientBoundary) "Maechen adversarial mutation was not applied: $($mutation.Key)"
            Assert-Throws {
                Assert-MaechenOfflineBoundary -Text $mutatedBoundary -RequireEndpointField -RequireIniBlock -RequireHostMarkers -RequireCanonicalProduction -RequireClientCredentialDeclaration
            } $mutation.Value.Error "Maechen adversarial mutation was accepted: $($mutation.Key)"
        }

        $missingClientCredentialDeclaration = [regex]::Replace(
            $clientBoundary,
            '(?is)The DLL contains no\s+provider key, token, cookie, or Authorization header\.',
            'Client credentials remain outside this record.',
            1)
        Assert-False ($missingClientCredentialDeclaration -ceq $clientBoundary) 'Maechen client credential-declaration omission mutant was not applied'
        Assert-Throws {
            Assert-MaechenOfflineBoundary -Text $missingClientCredentialDeclaration -RequireEndpointField -RequireIniBlock -RequireHostMarkers -RequireCanonicalProduction -RequireClientCredentialDeclaration
        } 'explicit no-provider-credential declaration' 'Maechen client credential-declaration omission mutant was accepted'

        $missingHandoffCredentialDeclaration = [regex]::Replace(
            $handoffBoundary,
            '(?is)There is no endpoint override or client\s+credential\.',
            'Client configuration remains scoped to the template.',
            1)
        Assert-False ($missingHandoffCredentialDeclaration -ceq $handoffBoundary) 'Maechen handoff credential-declaration omission mutant was not applied'
        Assert-Throws {
            Assert-MaechenOfflineBoundary -Text $missingHandoffCredentialDeclaration -RequireHandoffCredentialDeclaration
        } 'explicit no-override/client-credential declaration' 'Maechen handoff credential-declaration omission mutant was accepted'
    }

    Test-Case 'README distinguishes OFF gameplay targets from the ON-by-template dashboard adapter' {
        $readme = [System.IO.File]::ReadAllText($ReadmePath)
        Assert-TextMatch $readme '(?is)Speed Hack.*boosters\.speed_hack.*Ctrl\+Shift\+K.*1x/2x/4x/8x.*Native.*2x/4x.*field-scene.*RT2.*FMV.*outside.*indicator.*F12 is not owned by Speed Hack.*screenshots' 'README names the truthful Speed backend/indicator/F12 contract'
        Assert-TextMatch $readme '(?is)13 F8 gameplay mutation targets are OFF by default.*dashboard menu adapter is ON by template.*does not arm any gameplay target' 'README lacks the scoped gameplay-target/dashboard-adapter safety contract'
        Assert-TextNotMatch $readme '(?i)All hooks are OFF by default' 'README retains the absolute all-hooks-OFF claim'
        Assert-TextNotMatch $readme '(?i)Nothing touches your game until you arm a flag' 'README retains the old absolute no-touch wording'
    }

    Test-Case 'README and runtime scope validated fixed-RVA Speed and Dialog detours' {
        $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $repoScriptRoot '..\..\..'))
        $readme = [System.IO.File]::ReadAllText($ReadmePath)
        $compatibilityMatch = [regex]::Match($readme, '(?ms)^## Compatibility\s*(?<body>.*?)(?=^## |\z)')
        Assert-True $compatibilityMatch.Success 'README Compatibility section is missing'
        $compatibility = $compatibilityMatch.Groups['body'].Value

        $speedSource = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'src\runtime\FfxHooksDll\hooks\SpeedHackHook.cpp'))
        $dialogSource = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'src\runtime\FfxHooksDll\hooks\DialogSkipHook.cpp'))
        $dllmainSource = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'src\runtime\FfxHooksDll\dllmain.cpp'))
        $architecture = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\ARCHITECTURE.md'))
        $speedInstall = [regex]::Match($speedSource, '(?s)bool InstallSpeedHackHook\(.*?(?=\r?\nvoid RemoveSpeedHackHook\()')
        $dialogInstall = [regex]::Match($dialogSource, '(?s)bool InstallDialogSkipHook\(.*?(?=\r?\nvoid RemoveDialogSkipHook\()')
        Assert-True ($speedInstall.Success -and $dialogInstall.Success) 'fixed-RVA detour installer source bounds are missing'
        Assert-TextMatch $speedInstall.Value '(?s)new PLH::x86Detour.*g_globalTargetAddress.*SpeedHackGlobalTickBridge' 'Speed Hack lost its sole validated global-tick detour'
        Assert-TextNotMatch $speedInstall.Value 'RVA_FFX_FIELD_UPDATE_AND_RENDER|UpdateAndRender_SpeedHack|g_fieldDetour' 'Speed Hack restored the superseded field-delta detour'
        Assert-TextMatch $dialogInstall.Value '(?s)new PLH::x86Detour.*RVA_FFX_FMODVOICE_READ_EVENT_DATA' 'Dialog Skip is no longer the characterized fixed-RVA PolyHook detour'
        $speedValidationPattern = '(?s)ParseExecutableIdentity.*IsSupportedExecutable.*ValidateImageRange\s*\(\s*RVA_FFX_NATIVE_SPEED_BOOSTER\s*,\s*sizeof\(uint32_t\).*ValidateImageRange\s*\(\s*RVA_FFX_NATIVE_SPEED_BOOSTER_AVAILABILITY\s*,\s*sizeof\(uint8_t\).*ValidateSpeedHackGlobalTargetSignature\s*\(\s*globalTarget\s*,\s*kSpeedHackGlobalTargetLength\s*,\s*moduleBase\s*\)'
        $dialogValidationPattern = '(?s)ParseExecutableIdentity.*IsSupportedExecutable.*ValidateImageRange.*ValidateDialogSkipTargetSignature\s*\(\s*target\s*,\s*kDialogSkipTargetLength\s*\)'
        $speedInstallCallPattern = '(?s)InstallDialogSkipHook\(g_base,\s*LogLine,\s*&dialogSkipStatus\).*InstallSpeedHackHook\(g_base,\s*dialogSkipReady,\s*LogLine,\s*&speedHackStatus\)'
        Assert-TextMatch $speedInstall.Value $speedValidationPattern 'Speed Hack lost a supported-profile/native-range/relocated-global gate'
        Assert-TextNotMatch $speedInstall.Value '\bSpeedHackTargetSignatureMatches\s*\(' 'Speed Hack restored the ASLR-unsafe literal disk-byte matcher'
        Assert-TextMatch $speedInstall.Value '(?s)IsUnXModuleLoaded\(\).*UnXModuleConflict|UnXModuleConflict.*IsUnXModuleLoaded\(' 'Speed Hack lost fail-closed UnX module ownership arbitration'
        Assert-TextMatch $speedSource '(?s)GlobalTargetStillOwned\(\).*std::memcmp.*ExecuteNativeAction\(.*InterlockedCompareExchange\(\s*g_nativeStateAddress' 'Speed Hack lost comparative native ownership or target-drift observation'
        Assert-TextMatch $speedSource '(?s)SpeedHackGlobalTickBridge\(\).*g_publishedRouteWord.*cmp edx, 3.*g_nativeStateAddress.*cmp dword ptr \[edx\], 0.*fld dword ptr \[esp \+ 40\]' 'Speed bridge must decode custom 8x before enforcing native zero'
        Assert-TextMatch $speedSource '(?s)ExecuteNativeAction\(.*ProducerEpochStillCurrent\(producerEpoch\).*InterlockedCompareExchange\(.*ProducerEpochStillCurrent\(producerEpoch\).*CompareRestoreNativeOwned\(\)' 'native write lost before/after epoch validation and compensation'
        Assert-TextMatch $speedSource '(?s)void RemoveSpeedHackHook\(\).*RequestSpeedHackStop\(\)' 'Speed teardown lost admission close'
        Assert-TextNotMatch ([regex]::Match($speedSource, '(?s)void RemoveSpeedHackHook\(\).*?(?=\r?\n\}\s*// namespace FfxHooks)').Value) 'unHook\(\)|delete static_cast<PLH::x86Detour\*>|g_globalTickTrampoline\s*=\s*0' 'Speed teardown must retain callback code/data for process lifetime'
        Assert-TextNotMatch $speedInstall.Value '(?i)UnX.*(?:GetProcAddress|SendInput|keybd_event|WritePrivateProfile)' 'Speed Hack must not call UnX internals, synthesize input, or mutate UnX configuration'
        Assert-TextMatch $dialogInstall.Value $dialogValidationPattern 'Dialog Skip lost its supported-profile/exact-target gate'
        Assert-TextMatch $dllmainSource $speedInstallCallPattern 'dllmain lost corrected Dialog-before-Speed dependency order'
        Assert-TextMatch $architecture '(?is)Speed Hack.*native.*0x008E82A4.*availability.*0x008E82AC.*field-service.*0x420C00.*Dialog Skip.*0x30AEC0.*signature.*fail closed' 'architecture lacks the native/field-service validated Speed/Dialog ownership contract'

        $legacySpeedInstall = [regex]::Replace(
            $speedInstall.Value,
            'globalValidation\s*=\s*ValidateSpeedHackGlobalTargetSignature\s*\(\s*globalTarget\s*,\s*kSpeedHackGlobalTargetLength\s*,\s*moduleBase\s*\);',
            'globalValidation.status = SpeedHackTargetSignatureMatches(globalTarget, kSpeedHackGlobalTargetLength) ? SpeedHackTargetStatus::Match : SpeedHackTargetStatus::Mismatch;',
            1)
        $legacyDllmain = $dllmainSource.Replace(
            'InstallSpeedHackHook(g_base, dialogSkipReady, LogLine, &speedHackStatus)',
            'InstallSpeedHackHook(g_base, LogLine)')
        Assert-True ($legacySpeedInstall -cne $speedInstall.Value -and $legacyDllmain -cne $dllmainSource) 'legacy Speed source mutation anchors are missing'
        Assert-TextNotMatch $legacySpeedInstall $speedValidationPattern 'legacy literal Speed matcher survived the relocated global-target source contract'
        Assert-TextNotMatch $legacyDllmain $speedInstallCallPattern 'legacy two-argument Speed install survived the status-aware source contract'

        Assert-TextMatch $readme '(?is)Aurora developer UI is default OFF.*explicit Aurora.*Ctrl\+Alt\+F9.*Ctrl\+Alt\+F10.*plain F9/F10.*not Aurora hotkeys' 'README documents the default-off Aurora gate, explicit chords, and released plain keys'
        Assert-TextMatch $compatibility '(?is)Only specific supported-profile/expected-byte inline-patch families fail closed on mismatch' 'README lacks the scoped inline-patch mismatch guarantee'
        Assert-TextNotMatch $compatibility '(?is)hooks install.*only if.*target byte signature matches|never corrupts' 'README retains a universal signature/no-corruption claim'
        Assert-TextMatch $compatibility '(?is)Speed Hack.*supported executable profile.*native.*availability.*field-service.*exact.*signature.*beta.*RT2-pending' 'README lacks the validated native/field-service Speed Hack beta/RT2-pending boundary'
        Assert-TextMatch $compatibility '(?is)Dialog Skip.*supported executable profile.*exact.*0x30AEC0.*beta.*RT2-pending' 'README lacks the corrected validated Dialog Skip beta/RT2-pending boundary'
        Assert-TextMatch $compatibility 'Dynamic `FreeLibrary`/hot unload is unsupported' 'README lost the hot-unload boundary'
        Assert-TextMatch $readme '(?is)`run_f8_rt2\.ps1`.*never launches or stops.*copies/deploys a DLL.*No F8 case is promoted' 'README lost the manual RT2/no-deploy boundary'
    }

    Test-Case 'architecture and RT2 protocol publish loader-lock and integrity evidence boundaries' {
        $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $repoScriptRoot '..\..\..'))
        $architecture = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\ARCHITECTURE.md'))
        Assert-TextMatch $architecture '(?s)Process detach.*lock-free.*RequestUnXBoosterStop' 'detach lock-free request truth missing'
        Assert-TextMatch $architecture '(?s)Dynamic\s+`FreeLibrary`/hot unload is unsupported.*not called under the loader lock' 'hot unload/loader lock truth missing'
        Assert-TextMatch $architecture '(?s)Present.*33 ms.*no F8 window `SetTimer`/`TIMERPROC`' 'Present 33 ms/no timer truth missing'

        $protocol = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\RT2_PROTOCOL.md'))
        Assert-TextNotMatch $protocol '(?i)signed RT2 log' 'protocol still claims a signed log'
        Assert-TextMatch $protocol '(?i)integrity-bounded manifest.*SHA-256' 'manifest SHA-256 boundary missing'
        Assert-TextMatch $protocol '(?i)not (?:a )?cryptographic signature' 'non-signature caveat missing'
        Assert-TextMatch $protocol '(?s)heartbeat.*family-specific|family-specific.*heartbeat' 'probe heartbeat is still universal'
        Assert-TextMatch $protocol '(?i)exactly one feature' 'one-feature session rule missing'
        Assert-TextMatch $protocol '(?i)case-specific touched-file inventory' 'case-specific touched-file inventory missing'
        Assert-TextMatch $protocol '(?i)human attestation.*not technical proof' 'disposable-save attestation boundary missing'
        Assert-TextMatch $protocol '(?s)Compose.*gate-only' 'Compose gate-only protocol boundary missing'
        Assert-TextMatch $protocol '(?is)both phases derive the same default `work/f8_rt2` EvidenceRoot' 'shared default EvidenceRoot contract missing'
        Assert-TextMatch $protocol '(?is)custom Preflight root must be repeated explicitly on Verify' 'custom EvidenceRoot repetition contract missing'
        Assert-TextMatch $protocol '(?is)direct strict child.*exact `<timestamp>_<case>_<session>` leaf' 'strict evidence child/leaf contract missing'
        Assert-TextMatch $protocol '(?is)exact leaf `ffx-hooks\.ini\.snapshot\.bin`.*`paths\.ini`.*before any restore temp' 'snapshot/paths.ini pre-mutation link missing'
        Assert-TextNotMatch $protocol '(?is)coordinated rewrite.*cannot authorize.*different INI identity' 'protocol retains the coordinated-rewrite authenticity overclaim'
        Assert-TextMatch $protocol '(?is)internal consistency.*accidental.*single-(?:file|record) drift' 'unkeyed internal-consistency boundary missing'
        Assert-TextMatch $protocol '(?is)unkeyed.*sidecar.*not authenticity' 'unkeyed sidecar authenticity caveat missing'
        Assert-TextMatch $protocol '(?is)coordinated rewrite.*external trusted pin.*outside (?:this|the) protocol' 'external trusted pin boundary missing'
        Assert-TextMatch $protocol '(?is)`manifest\.json`.*`manifest\.sha256`.*pinned.*initial.*unchanged.*final' 'dual Preflight identity pin contract missing'
        Assert-TextMatch $protocol '(?is)exact byte arrays.*validated.*parsed.*same read' 'same-read initial pin boundary missing'
        Assert-TextMatch $protocol '(?is)65,535 bytes.*256 parsed pairs.*duplicates.*128 UTF-8 bytes.*512 UTF-8 bytes' 'runtime INI parser limits missing'
        Assert-TextMatch $protocol '(?is)ON/OFF anchor.*`edit=SAVED`' 'SAVED anchor requirement missing'
        Assert-TextMatch $protocol '(?is)non-SAVED F8 edit.*non-`none` runtime `failure=`.*any target' 'structured log failure rejection missing'
        Assert-TextMatch $protocol ([regex]::Escape('ffx-hooks.f8-rt2-verify-evidence/v1')) 'final Verify schema missing'
        foreach ($leaf in @('log-slice.bin', 'log-slice.sha256', 'parsed-log-verdict.json', 'before-after-hashes.json', 'restoration-verdict.json')) {
            $quotedLeaf = ([char]0x60).ToString() + $leaf + ([char]0x60).ToString()
            Assert-True $protocol.Contains($quotedLeaf) "final Verify artifact missing from protocol: $leaf"
        }
        Assert-TextMatch $protocol '(?is)sidecar.*written last.*read back' 'sidecar-last/readback contract missing'
        Assert-TextMatch $protocol '(?is)`76/76`.*16.*mutation' 'current PowerShell RT0/mutation count missing'
        Assert-TextMatch $protocol '(?is)Speed Hack.*Ctrl\+Shift\+K.*1x.*2x.*4x.*8x.*indicator.*F12.*screenshot-only' 'RT2 protocol lacks the Speed Hack cycle/indicator/F12 observation contract'
        Assert-TextMatch $protocol '(?is)Native.*2x/4x.*ARMED.*not.*scene.*Fast field scenes.*8x.*APPLIED.*field gameplay.*dialogue.*rendered field-scene.*FMV.*unsupported' 'RT2 protocol lacks the bounded Speed observation and FMV boundary'
        Assert-TextMatch $protocol '(?is)Seymour.*D307E8.*D307EB.*D2C895.*D2C8A3.*Switch.*Sphere Grid.*must not' 'RT2 protocol lacks persistent/local Seymour snapshots and Sphere Grid exclusion'
        Assert-TextMatch $protocol ([regex]::Escape('ffx-hooks.seymour-battle-memory/v1')) 'Seymour raw-memory evidence schema missing'
        Assert-TextMatch $protocol '(?is)`-SeymourEvidencePath <json>`.*`moduleBase`.*runtimeVa.*module base.*RVA' 'Seymour evidence path/base/RVA/runtime-VA identity contract missing'
        Assert-TextMatch $protocol '(?is)`before`.*`on`.*`afterSwitch`.*`off`.*`nextBattle`' 'Seymour exact five-phase capture contract missing'
        Assert-TextMatch $protocol '(?is)seymourSelected.*controlledTurnCompleted.*switchCompleted.*normalExitCompleted.*nextBattleEntered.*nextBattleNoReintroduction.*sphereGridNotOpened' 'Seymour controlled observation fields missing'
        Assert-TextMatch $protocol ([regex]::Escape('seymour-memory-evidence.json')) 'sealed Seymour raw-memory artifact missing'
    }

    Test-Case 'session handoff separates exact HEAD, shared overlay, artifacts, and no-RT2 next action' {
        $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $repoScriptRoot '..\..\..'))
        $handoff = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\ai\SESSION_HANDOFF.md'))
        $plan = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'docs\superpowers\plans\2026-08-20-f8-runtime-ux.md'))
        foreach ($pattern in @(
            '(?i)exact committed HEAD',
            '(?i)shared dirty overlay',
            '(?i)clean artifact identity',
            '(?i)mixed.*nondeployable|mixed.*non-deployable',
            '(?i)no RT2|RT2.*not run',
            '(?i)no deploy',
            '(?i)Task 9'
        )) {
            Assert-TextMatch $handoff $pattern "handoff boundary missing: $pattern"
        }
        Assert-TextMatch $handoff ([regex]::Escape('7ca03669cfa0cbc11d20305771f4fca25b9851a4')) 'Task-8 committed base identity missing'
        Assert-TextMatch $handoff '(?is)section is versioned with Fix Round 1.*parent.*7ca03669cfa0cbc11d20305771f4fca25b9851a4' 'Task-8 durable Fix1 parent boundary missing'
        Assert-TextMatch $handoff '(?is)resulting commit identity.*git log.*authoritative' 'Task-8 resulting commit identity source missing'
        Assert-TextMatch $handoff '(?is)shared working bytes.*intentionally divergent.*current committed HEAD' 'Task-8 durable shared-working divergence boundary missing'
        Assert-TextNotMatch $handoff '(?is)remains\s+\*\*uncommitted\*\*|must not touch the main index until.*ruling|shared checkout is at\s+`7ca03669' 'Task-8 handoff retains a self-staling integration claim'
        Assert-TextMatch $handoff '(?is)`68/68`.*13.*mutation' 'Task-8 fix RT0/mutation count missing'
        Assert-TextMatch $handoff '(?is)default EvidenceRoot.*custom.*repeated' 'Task-8 fix evidence-root semantics missing'
        Assert-TextMatch $handoff ([regex]::Escape('ffx-hooks.f8-rt2-verify-evidence/v1')) 'Task-8 final Verify manifest schema missing'
        Assert-TextMatch $handoff '(?is)section is versioned with Fix Round 2.*parent.*ab4bb59ee7e68b2ffeadedbc62e3fe7eabaa79f3' 'Task-8 durable Fix2 parent boundary missing'
        Assert-TextMatch $handoff '(?is)unkeyed.*not authenticity.*external trusted pin' 'Task-8 coordinated-rewrite trust boundary missing'
        Assert-TextMatch $handoff '(?is)exact byte arrays.*same read' 'Task-8 same-read initial pin boundary missing'
        Assert-TextMatch $handoff '(?is)`71/71`.*15.*mutation' 'Task-8 Fix2 RT0/mutation count missing'
        Assert-TextMatch $handoff '(?is)Task 4 F8 Runtime UX.*codexclaudiocodeffeditor/f8-runtime-ux.*5fb43952a6ff68ec0cc20a6c6984126256da2497.*RT2.*pending.*no deploy' 'Task-4 branch/evidence/no-RT2 handoff boundary missing'

        # Keep this contract ASCII-only: Windows PowerShell 5.1 can parse a no-BOM test script through an ANSI code page.
        $finalSectionTitle = 'Final F8 Runtime UX completion'
        $finalSectionMatches = [regex]::Matches($handoff, "(?m)^## .*?$([regex]::Escape($finalSectionTitle))\r?$")
        Assert-Equal 1 $finalSectionMatches.Count 'final F8 UX completion handoff section must be unique'
        $finalSection = [regex]::Match($handoff, "(?ms)^## .*?$([regex]::Escape($finalSectionTitle))\r?\n(?<body>.*?)(?=^## |\z)")
        Assert-True $finalSection.Success 'final F8 UX completion handoff section body missing'
        $finalBody = $finalSection.Groups['body'].Value
        foreach ($pattern in @(
            '(?is)supersedes.*Task 3.*Task 4.*was.*final.*pre-deploy.*historical.*superseded',
            '6624554d7f66fe40050dd0a24443b265051ebb8f\.\.a6d81915a82e34f0aaec406cf053f59e3c1f5085',
            '(?is)60ddec97bb48de325ff3ccea8fa39d24d455fedb.*5fb43952a6ff68ec0cc20a6c6984126256da2497',
            '(?is)plain F9/F10.*free.*Ctrl\+Alt\+F9/F10.*source.*explicitly enabled.*Present producer.*retained',
            '(?is)1650.*24.*72/72.*16/16',
            '(?is)1,200,128.*BDE008E40C827A2306B36D2603308FCCA836B5C26394FE10E958C9B62DE619E1',
            '(?is)2026-08-20 22:59:00 UTC.*COFF-i386.*i386.*32-bit.*FF10HgetName.*FF10HgetVer.*KERNEL32\.dll.*USER32\.dll.*GDI32\.dll.*d3d11\.dll',
            '(?is)branch.*index.*clean.*40.*dirty.*untracked.*shared.*preserved',
            '(?is)RT2.*pending.*deploy.*not run'
        )) {
            Assert-TextMatch $finalBody $pattern "final F8 UX completion handoff boundary missing: $pattern"
        }
        Assert-TextNotMatch $finalBody '(?is)current final governance record' 'final F8 UX completion must not remain the current authority after deployment'

        Assert-TextMatch $plan '(?is)> \*\*Historical completion record \(2026-08-20\):\*\*.*implementation.*offline verification.*deployment.*complete.*RT2.*pending.*historical.*must not be rerun wholesale' 'F8 UX plan completed-deploy/RT2-pending boundary missing'
        foreach ($taskNumber in 1..4) {
            $task = [regex]::Match($plan, "(?ms)^### Task ${taskNumber}:.*?(?=^### Task |\z)")
            Assert-True $task.Success "F8 UX Task ${taskNumber} block missing from plan"
            Assert-TextMatch $task.Value '- \[x\]' "F8 UX Task ${taskNumber} has no completed execution step"
            Assert-TextNotMatch $task.Value '- \[ \]' "F8 UX Task ${taskNumber} retains an unchecked execution step"
        }
        $task5 = [regex]::Match($plan, '(?ms)^### Task 5:.*\z')
        Assert-True $task5.Success 'F8 UX Task 5 block missing from plan'
        foreach ($stepNumber in 1..4) {
            Assert-TextMatch $task5.Value "(?m)^- \[x\] \*\*Step ${stepNumber}:" "F8 UX Task 5 Step ${stepNumber} must be completed"
        }

        $deploymentSectionTitle = 'F8 Runtime UX deployment execution record'
        $deploymentSectionMatches = [regex]::Matches($handoff, "(?m)^## .*?$([regex]::Escape($deploymentSectionTitle))\r?$")
        Assert-Equal 1 $deploymentSectionMatches.Count 'F8 UX deployment execution record must be unique'
        $deploymentSection = [regex]::Match($handoff, "(?ms)^## .*?$([regex]::Escape($deploymentSectionTitle))\r?\n(?<body>.*?)(?=^## |\z)")
        Assert-True $deploymentSection.Success 'F8 UX deployment execution record body missing'
        $deploymentBody = $deploymentSection.Groups['body'].Value
        foreach ($pattern in @(
            '(?is)supersedes.*entire.*Final F8 Runtime UX completion.*current.*authority.*pre-deploy.*no-deploy',
            '2026-08-20T23:37:01Z',
            '(?is)D:\\SteamLibrary\\steamapps\\common\\FINAL FANTASY FFX&FFX-2 HD Remaster\\modules\\ffx-hooks\.dll.*1,200,128.*BDE008E40C827A2306B36D2603308FCCA836B5C26394FE10E958C9B62DE619E1',
            '(?is)ffx-hooks\.dll\.backup-f8-ux-20260820T233701Z-998BA0D6.*1,198,592.*998BA0D6416C10DA98233DA210A3E47EDB7A8CE7ACD3E09587C958DAC79C4BF1',
            '(?is)Atomic File\.Replace.*pending.*absent.*FFX/editor.*closed.*no launch.*RT2.*not run.*Production',
            '(?is)744a6bc1b16212a91add6afe1978667eaada4b00.*origin/main.*git log.*later.*docs-record.*not self-pin'
        )) {
            Assert-TextMatch $deploymentBody $pattern "F8 UX deployment execution record boundary missing: $pattern"
        }
    }

    if ($Mutation -ne 'None') {
        Test-Case "requested mutation [$Mutation] must be discriminated" {
            Require-Library
            $result = Invoke-F8Rt0MutationProbe -Mutation $Mutation -FixtureRoot $script:FixtureRoot -ScriptPath $ScriptPath -ModulePath $ModulePath -HarnessPath $script:HarnessPath -ReadmePath $ReadmePath
            Write-Host "MUTATION: $Mutation child_exit=$($result.ExitCode) killed_by=[$($result.ExpectedFailure)]"
            Assert-True $result.Detected "mutation $Mutation survived"
        }
    }
} finally {
    if ($script:ModuleImported) {
        Remove-Module -Name 'run_f8_rt2_lib' -Force -ErrorAction SilentlyContinue
    }
    $cleanupPath = [System.IO.Path]::GetFullPath($script:FixtureRoot)
    $cleanupLeaf = Split-Path -Leaf $cleanupPath
    if (-not $cleanupPath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        $cleanupLeaf -ne $fixtureName -or
        $cleanupLeaf -notmatch '^f8r-[0-9a-f]{16}$') {
        throw "refusing unsafe RT0 cleanup: $cleanupPath"
    }
    if (Test-Path -LiteralPath $cleanupPath) {
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force
    }
}

Write-Host "F8 RT2 RT0: total=$($script:Passed + $script:Failed) passed=$($script:Passed) failed=$($script:Failed)"
foreach ($failure in $script:Failures) { Write-Host $failure }
if ($script:Failed -ne 0) { exit 1 }
Write-Host 'F8 RT2 RT0: PASS'
exit 0
