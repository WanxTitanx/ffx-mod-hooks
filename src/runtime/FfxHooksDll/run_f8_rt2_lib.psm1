Set-StrictMode -Version 2.0

# Maintenance boundary: Preflight and Verify validate a manual workflow; they never launch,
# stop, or deploy FFX. Provider injection through the existing bag is reserved for RT0 tests.
# SHA-256 sidecars provide unkeyed integrity, not authenticity, a signature, or Production proof.

$script:SupportedExecutableSha256 = '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
$script:ManifestSchema = 'ffx-hooks.f8-rt2-evidence/v1'
$script:VerifyManifestSchema = 'ffx-hooks.f8-rt2-verify-evidence/v1'
$script:SeymourMemoryEvidenceSchema = 'ffx-hooks.seymour-battle-memory/v1'
$script:SeymourCaptureSequence = @('before', 'on', 'afterSwitch', 'off', 'nextBattle')

function New-F8RuntimeLeafSpec {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Rva,
        [Parameter(Mandatory)][int]$Size
    )
    [pscustomobject]@{ Name = $Name; Rva = $Rva; Size = $Size }
}

function New-F8CaseSpec {
    param(
        [string]$Case,
        [string]$Canonical,
        [ValidateSet('ConfigPolled', 'RuntimeAcknowledged')]
        [string]$ApplyMode,
        [string]$AuthorityKey,
        [string]$LegacyKey,
        [string]$EnvironmentName,
        [string]$FlagName,
        [string]$DisableEnvironmentName,
        [string]$OffFlagName,
        [string]$GlobalOffFlagName,
        [string]$ManualScope = 'Toggle only the selected F8 row ON, observe it, then toggle it OFF and observe restoration.',
        [string]$ScalarKey,
        [int]$ScalarDefault = 0,
        [int]$ScalarMinimum = 0,
        [int]$ScalarMaximum = 0,
        [string]$RuntimeEvidenceSchema,
        [object[]]$RuntimeLeaves = @()
    )

    [pscustomobject]@{
        Case = $Case
        Canonical = $Canonical
        ApplyMode = $ApplyMode
        AuthorityKey = $AuthorityKey
        LegacyKey = $LegacyKey
        EnvironmentName = $EnvironmentName
        FlagName = $FlagName
        DisableEnvironmentName = $DisableEnvironmentName
        OffFlagName = $OffFlagName
        GlobalOffFlagName = $GlobalOffFlagName
        DefaultValue = $false
        ManualScope = $ManualScope
        ScalarKey = $ScalarKey
        ScalarDefault = $ScalarDefault
        ScalarMinimum = $ScalarMinimum
        ScalarMaximum = $ScalarMaximum
        RuntimeEvidenceSchema = $RuntimeEvidenceSchema
        RuntimeLeaves = @($RuntimeLeaves)
    }
}

function Get-F8Rt2CaseTable {
    @(
        (New-F8CaseSpec 'permanent_sensor' 'boosters.permanent_sensor' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'seymour_battle_roster' 'boosters.playable_seymour' `
            'RuntimeAcknowledged' `
            -ManualScope 'Battle roster only: never open Sphere Grid; snapshot persistent and local lists around one Switch, prove persistent exit cleanup, and verify all four lists clean at next battle init.' `
            -RuntimeEvidenceSchema $script:SeymourMemoryEvidenceSchema `
            -RuntimeLeaves @(
                (New-F8RuntimeLeafSpec 'persistentState' '0x00D307E8' 3),
                (New-F8RuntimeLeafSpec 'persistentAbility' '0x00D307EB' 17),
                (New-F8RuntimeLeafSpec 'localState' '0x00D2C895' 7),
                (New-F8RuntimeLeafSpec 'localAbility' '0x00D2C8A3' 17)
            )),
        (New-F8CaseSpec 'speed_hack' 'boosters.speed_hack' 'ConfigPolled'),
        (New-F8CaseSpec 'entire_party_earns_ap' 'boosters.entire_party_earns_ap' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'invincible_party' 'cheats.invincible_party' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'invincible_enemies' 'cheats.invincible_enemies' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'always_overdrive' 'cheats.always_overdrive' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'always_critical' 'cheats.always_critical' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'damage_99999' 'cheats.damage_value' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'always_rare_drop' 'cheats.always_rare_drop' 'RuntimeAcknowledged'),
        (New-F8CaseSpec 'ap_100x' 'cheats.ap_100x' 'RuntimeAcknowledged' `
            -ScalarKey 'cheats.ap_multiplier' -ScalarDefault 100 -ScalarMinimum 1 -ScalarMaximum 100 `
            -ManualScope 'Observe baseline AP, set one non-default multiplier, verify exact applied scalar, then verify OFF restoration.'),
        (New-F8CaseSpec 'gil_100x' 'cheats.gil_100x' 'RuntimeAcknowledged' `
            -ScalarKey 'cheats.gil_multiplier' -ScalarDefault 100 -ScalarMinimum 1 -ScalarMaximum 100 `
            -ManualScope 'Observe baseline Gil, set one non-default multiplier, verify exact applied scalar, then verify OFF restoration.'),
        (New-F8CaseSpec `
            'arena_plus_compose_f7' `
            'arena_plus.compose_f7' `
            'ConfigPolled' `
            'f8_authority.arena_plus_compose_f7' `
            'labs.arena_plus_compose_f7' `
            'FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7' `
            'arena_plus_compose_f7.flag' `
            'FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7' `
            $null `
            $null `
            'Config/menu gating only; do not launch Compose or mutate battle files.'),
        (New-F8CaseSpec 'dialog_skip' 'input.dialog_skip' 'ConfigPolled')
    )
}

function Get-F8CaseSpec {
    param([Parameter(Mandatory)][string]$Case)
    $matches = @(Get-F8Rt2CaseTable | Where-Object { $_.Case -ceq $Case })
    if ($matches.Count -ne 1) { throw "unknown or ambiguous F8 RT2 case: $Case" }
    return $matches[0]
}

function ConvertFrom-F8BoolText {
    param([AllowEmptyString()][string]$Text)
    if ($null -eq $Text) { return [pscustomobject]@{ Valid = $false; Value = $false } }
    if ($Text -ceq '1' -or $Text -ieq 'true' -or $Text -ieq 'yes' -or $Text -ieq 'on') {
        return [pscustomobject]@{ Valid = $true; Value = $true }
    }
    if ($Text -ceq '0' -or $Text -ieq 'false' -or $Text -ieq 'no' -or $Text -ieq 'off') {
        return [pscustomobject]@{ Valid = $true; Value = $false }
    }
    return [pscustomobject]@{ Valid = $false; Value = $false }
}

function ConvertFrom-F8IniBytes {
    param([Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes)
    if ($Bytes.Length -eq 0) { throw 'empty INI input is rejected by the runtime loader' }
    if ($Bytes.Length -gt 65535) { throw 'INI input exceeds the 65535-byte runtime limit' }
    $offset = 0
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        $offset = 3
    }
    $text = [System.Text.Encoding]::UTF8.GetString($Bytes, $offset, $Bytes.Length - $offset)
    $pairs = New-Object 'System.Collections.Generic.Dictionary[string,string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $section = ''
    $parsedPairCount = 0
    foreach ($rawLine in [regex]::Split($text, "`n")) {
        $line = $rawLine.Trim(' ', "`t", "`r")
        if ($line.Length -eq 0 -or $line[0] -eq '#' -or $line[0] -eq ';') { continue }

        if ($line[0] -eq '[') {
            $close = $line.IndexOf(']')
            if ($close -gt 1) {
                $candidate = $line.Substring(1, $close - 1).Trim(' ', "`t", "`r")
                if ($candidate.Length -gt 0) { $section = $candidate }
            }
            continue
        }

        $equals = $line.IndexOf('=')
        if ($equals -lt 0) { continue }
        $key = $line.Substring(0, $equals).Trim(' ', "`t", "`r")
        if ($key.Length -eq 0) { continue }
        $value = $line.Substring($equals + 1).Trim(' ', "`t", "`r")
        $flat = if ($section.Length -eq 0) { $key } else { "$section.$key" }
        # WHY: Preflight must reject every file that committed Config::Load rejects. The
        # runtime counts parsed rows before duplicate projection and bounds UTF-8 bytes.
        if ($parsedPairCount -ge 256) { throw 'INI parsed pair count exceeds the 256-pair runtime limit' }
        if ([System.Text.Encoding]::UTF8.GetByteCount($flat) -ge 128) { throw 'INI flat key reaches the 128-byte runtime limit' }
        if ([System.Text.Encoding]::UTF8.GetByteCount($value) -ge 512) { throw 'INI value reaches the 512-byte runtime limit' }
        ++$parsedPairCount
        if (-not $pairs.ContainsKey($flat)) { $pairs.Add($flat, $value) }
    }
    return $pairs
}

function Try-GetF8MapValue {
    param($Map, [string]$Key)
    if ($null -eq $Map -or [string]::IsNullOrEmpty($Key)) {
        return [pscustomobject]@{ Found = $false; Value = $null }
    }
    if ($Map -is [System.Collections.IDictionary]) {
        foreach ($existingKey in @($Map.Keys)) {
            if ([string]$existingKey -ieq $Key) {
                return [pscustomobject]@{ Found = $true; Value = $Map[$existingKey] }
            }
        }
    }
    if ($null -ne $Map.PSObject.Methods['ContainsKey'] -and $Map.ContainsKey($Key)) {
        return [pscustomobject]@{ Found = $true; Value = $Map[$Key] }
    }
    foreach ($property in @($Map.PSObject.Properties)) {
        if ($property.Name -ieq $Key) {
            return [pscustomobject]@{ Found = $true; Value = $property.Value }
        }
    }
    return [pscustomobject]@{ Found = $false; Value = $null }
}

function Try-GetF8BoolFromMap {
    param($Map, [string]$Key)
    $entry = Try-GetF8MapValue -Map $Map -Key $Key
    if (-not $entry.Found) { return [pscustomobject]@{ Found = $false; Valid = $false; Value = $false } }
    $parsed = ConvertFrom-F8BoolText -Text ([string]$entry.Value)
    return [pscustomobject]@{ Found = $true; Valid = $parsed.Valid; Value = $parsed.Value }
}

function Get-F8FlagLocations {
    param([string]$GameRoot, [string]$FlagName)
    if ([string]::IsNullOrEmpty($GameRoot) -or [string]::IsNullOrEmpty($FlagName)) { return @() }
    @(
        [pscustomobject]@{ Path = Join-Path $GameRoot "modules\$FlagName"; Source = 'LegacyFlagModules' },
        [pscustomobject]@{ Path = Join-Path $GameRoot "config\$FlagName"; Source = 'LegacyFlagConfig' },
        [pscustomobject]@{ Path = Join-Path $GameRoot "modules\config\$FlagName"; Source = 'LegacyFlagModulesConfig' },
        [pscustomobject]@{ Path = Join-Path $GameRoot $FlagName; Source = 'LegacyFlagRoot' }
    )
}

function Find-F8FlagSource {
    param(
        [string]$GameRoot,
        [string]$FlagName,
        [scriptblock]$FlagExistsProvider
    )
    if ([string]::IsNullOrEmpty($FlagName)) { return $null }
    if ($null -eq $FlagExistsProvider) {
        $FlagExistsProvider = { param($path) [System.IO.File]::Exists($path) }
    }
    foreach ($candidate in @(Get-F8FlagLocations -GameRoot $GameRoot -FlagName $FlagName)) {
        $exists = $false
        try { $exists = [bool](& $FlagExistsProvider $candidate.Path) } catch {
            throw "flag provider failed for [$($candidate.Path)]: $($_.Exception.Message)"
        }
        if ($exists) { return $candidate.Source }
    }
    return $null
}

function Resolve-F8BoolGate {
    param(
        [Parameter(Mandatory)]$Spec,
        [Parameter(Mandatory)]$Ini,
        $Environment = @{},
        [Parameter(Mandatory)][string]$GameRoot,
        [scriptblock]$FlagExistsProvider
    )

    if ([string]::IsNullOrEmpty([string]$Spec.Canonical)) {
        return [pscustomobject]@{ Value = [bool]$Spec.DefaultValue; Source = 'DefaultValue' }
    }

    if (-not [string]::IsNullOrEmpty([string]$Spec.DisableEnvironmentName)) {
        $disable = Try-GetF8BoolFromMap -Map $Environment -Key $Spec.DisableEnvironmentName
        if ($disable.Valid -and $disable.Value) {
            return [pscustomobject]@{ Value = $false; Source = 'DisableEnvironment' }
        }
    }

    foreach ($offName in @($Spec.OffFlagName, $Spec.GlobalOffFlagName)) {
        if ([string]::IsNullOrEmpty([string]$offName)) { continue }
        $offSource = Find-F8FlagSource -GameRoot $GameRoot -FlagName $offName -FlagExistsProvider $FlagExistsProvider
        if ($offSource) { return [pscustomobject]@{ Value = $false; Source = 'LegacyOffFlag' } }
    }

    if (-not [string]::IsNullOrEmpty([string]$Spec.EnvironmentName)) {
        $environmentValue = Try-GetF8BoolFromMap -Map $Environment -Key $Spec.EnvironmentName
        if ($environmentValue.Valid) {
            return [pscustomobject]@{ Value = [bool]$environmentValue.Value; Source = 'Environment' }
        }
    }

    if (-not [string]::IsNullOrEmpty([string]$Spec.AuthorityKey)) {
        $marker = Try-GetF8BoolFromMap -Map $Ini -Key $Spec.AuthorityKey
        $canonical = Try-GetF8BoolFromMap -Map $Ini -Key $Spec.Canonical
        if ($marker.Valid -and $marker.Value -and $canonical.Valid) {
            return [pscustomobject]@{ Value = [bool]$canonical.Value; Source = 'AuthoritativeCanonicalIni' }
        }
    }

    if (-not [string]::IsNullOrEmpty([string]$Spec.LegacyKey)) {
        $legacy = Try-GetF8BoolFromMap -Map $Ini -Key $Spec.LegacyKey
        if ($legacy.Valid) { return [pscustomobject]@{ Value = [bool]$legacy.Value; Source = 'LegacyIni' } }
    }

    if (-not [string]::IsNullOrEmpty([string]$Spec.FlagName)) {
        $flagSource = Find-F8FlagSource -GameRoot $GameRoot -FlagName $Spec.FlagName -FlagExistsProvider $FlagExistsProvider
        if ($flagSource) { return [pscustomobject]@{ Value = $true; Source = $flagSource } }
    }

    if ([string]::IsNullOrEmpty([string]$Spec.AuthorityKey)) {
        $canonical = Try-GetF8BoolFromMap -Map $Ini -Key $Spec.Canonical
        if ($canonical.Valid) { return [pscustomobject]@{ Value = [bool]$canonical.Value; Source = 'UnmarkedCanonicalIni' } }
    }

    return [pscustomobject]@{ Value = [bool]$Spec.DefaultValue; Source = 'DefaultValue' }
}

function Resolve-F8AllCases {
    param(
        [Parameter(Mandatory)]$Ini,
        $Environment = @{},
        [Parameter(Mandatory)][string]$GameRoot,
        [scriptblock]$FlagExistsProvider
    )
    foreach ($spec in @(Get-F8Rt2CaseTable)) {
        $resolution = Resolve-F8BoolGate -Spec $spec -Ini $Ini -Environment $Environment -GameRoot $GameRoot -FlagExistsProvider $FlagExistsProvider
        [pscustomobject]@{
            Case = $spec.Case
            Canonical = $spec.Canonical
            ApplyMode = $spec.ApplyMode
            Value = $resolution.Value
            Source = $resolution.Source
        }
    }
}

function Test-F8HigherAuthorityOnBlocker {
    param([Parameter(Mandatory)]$Spec, [Parameter(Mandatory)]$Resolution)
    if ($Spec.Case -cne 'arena_plus_compose_f7' -or [bool]$Resolution.Value) { return $false }
    return @('DisableEnvironment', 'LegacyOffFlag', 'Environment') -contains [string]$Resolution.Source
}

function Assert-F8ProcessesClosed {
    param(
        [scriptblock]$ProcessProvider,
        [string]$CheckName = 'process gate'
    )
    if ($null -eq $ProcessProvider) {
        $ProcessProvider = {
            @(Get-Process -ErrorAction Stop | Where-Object { $_.ProcessName -ieq 'FFX' -or $_.ProcessName -ieq 'FFXProjectEditor' })
        }
    }
    try { $processes = @(& $ProcessProvider) } catch {
        throw "$CheckName failed closed because process query failed: $($_.Exception.Message)"
    }
    foreach ($process in $processes) {
        $name = if ($process.PSObject.Properties.Name -contains 'ProcessName') { [string]$process.ProcessName } else { [string]$process.Name }
        if ($name -ieq 'FFX' -or $name -ieq 'FFXProjectEditor') {
            $id = if ($process.PSObject.Properties.Name -contains 'Id') { $process.Id } else { '?' }
            throw "$CheckName failed: $name is open (PID $id)"
        }
    }
    return $true
}

function Assert-F8DoubleProcessGate {
    param(
        [Parameter(Mandatory)][scriptblock]$ProcessProvider,
        [Parameter(Mandatory)][scriptblock]$Operation,
        [string]$CheckName = 'sensitive operation'
    )
    [void](Assert-F8ProcessesClosed -ProcessProvider $ProcessProvider -CheckName "$CheckName initial check")
    [void](Assert-F8ProcessesClosed -ProcessProvider $ProcessProvider -CheckName "$CheckName immediate recheck")
    return & $Operation
}

function Get-F8Sha256Hex {
    param([Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString('X2') }) -join '')
    } finally {
        $sha.Dispose()
    }
}

function Get-F8FileSha256Hex {
    param([Parameter(Mandatory)][string]$Path)
    $stream = New-Object System.IO.FileStream(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        ([System.IO.FileShare]::Read -bor [System.IO.FileShare]::Delete)
    )
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (($sha.ComputeHash($stream) | ForEach-Object { $_.ToString('X2') }) -join '')
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-F8ExactLeafEvidence {
    param(
        [Parameter(Mandatory)]$Paths,
        [string]$SupportedExeHash = $script:SupportedExecutableSha256,
        [scriptblock]$HashProvider
    )
    if ($null -eq $HashProvider) {
        $HashProvider = { param($path) Get-F8FileSha256Hex -Path $path }
    }

    $required = @('BuiltDll', 'Exe', 'InstalledDll', 'Ini')
    foreach ($key in $required) {
        $entry = Try-GetF8MapValue -Map $Paths -Key $key
        if (-not $entry.Found -or [string]::IsNullOrEmpty([string]$entry.Value)) {
            throw "$key exact path is missing"
        }
        $path = [string]$entry.Value
        if ([System.IO.Directory]::Exists($path)) { throw "$key must be a file, not a directory: $path" }
        if (-not [System.IO.File]::Exists($path)) { throw "$key exact file is missing: $path" }
    }

    $result = [ordered]@{}
    foreach ($key in @('BuiltDll', 'Exe', 'InstalledDll')) {
        $path = [string](Try-GetF8MapValue -Map $Paths -Key $key).Value
        try { $hash = [string](& $HashProvider $path) } catch {
            throw "$key SHA-256 failed for [$path]: $($_.Exception.Message)"
        }
        if ($hash -notmatch '^[0-9A-Fa-f]{64}$') { throw "$key returned an invalid SHA-256 value" }
        $result[$key] = [pscustomobject]@{
            Path = [System.IO.Path]::GetFullPath($path)
            Length = (New-Object System.IO.FileInfo($path)).Length
            Sha256 = $hash.ToUpperInvariant()
        }
    }

    # The INI is deliberately read once. Its snapshot and hash must derive from these same bytes.
    $iniPath = [string](Try-GetF8MapValue -Map $Paths -Key 'Ini').Value
    $iniBytes = [System.IO.File]::ReadAllBytes($iniPath)
    $result['Ini'] = [pscustomobject]@{
        Path = [System.IO.Path]::GetFullPath($iniPath)
        Length = $iniBytes.Length
        Sha256 = Get-F8Sha256Hex -Bytes $iniBytes
        Bytes = $iniBytes
    }

    if ($result.BuiltDll.Sha256 -cne $result.InstalledDll.Sha256) {
        throw 'built and installed DLL hashes differ; perform a separate manual deploy with FFX closed, then rerun Preflight'
    }
    if ($result.Exe.Sha256 -cne $SupportedExeHash.ToUpperInvariant()) {
        throw "unsupported FFX executable SHA-256: $($result.Exe.Sha256)"
    }
    return [pscustomobject]$result
}

function Test-F8DashboardEnabled {
    param([Parameter(Mandatory)]$Ini)
    $entry = Try-GetF8BoolFromMap -Map $Ini -Key 'dashboard.enabled'
    return [bool]($entry.Valid -and $entry.Value)
}

function Write-F8BytesCreateNew {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes)
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

function New-F8ConfigSnapshot {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$IniBytes,
        [Parameter(Mandatory)][string]$IniPath,
        [Parameter(Mandatory)][string]$EvidenceDirectory
    )
    if (-not [System.IO.Directory]::Exists($EvidenceDirectory)) {
        throw "evidence directory does not exist: $EvidenceDirectory"
    }
    $snapshotPath = Join-Path $EvidenceDirectory 'ffx-hooks.ini.snapshot.bin'
    Write-F8BytesCreateNew -Path $snapshotPath -Bytes $IniBytes
    $written = [System.IO.File]::ReadAllBytes($snapshotPath)
    $expectedHash = Get-F8Sha256Hex -Bytes $IniBytes
    $writtenHash = Get-F8Sha256Hex -Bytes $written
    if ($written.Length -ne $IniBytes.Length -or $writtenHash -cne $expectedHash) {
        throw 'byte-exact INI snapshot verification failed'
    }
    [pscustomobject]@{
        Path = [System.IO.Path]::GetFullPath($snapshotPath)
        SourcePath = [System.IO.Path]::GetFullPath($IniPath)
        Length = $written.Length
        Sha256 = $writtenHash
    }
}

function Initialize-F8MoveFileEx {
    if ($null -ne ('F8Rt2NativeMethods' -as [type])) { return }
    $source = @'
using System;
using System.Runtime.InteropServices;

public static class F8Rt2NativeMethods
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool MoveFileExW(string existingFileName, string newFileName, uint flags);
}
'@
    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function Restore-F8ConfigSnapshot {
    param(
        [Parameter(Mandatory)][string]$SnapshotPath,
        [Parameter(Mandatory)][string]$DestinationPath,
        [Parameter(Mandatory)][string]$ExpectedSha256,
        [Parameter(Mandatory)][long]$ExpectedLength,
        [Parameter(Mandatory)][scriptblock]$BeforeReplaceProvider,
        [scriptblock]$MoveProvider
    )
    if (-not [System.IO.File]::Exists($SnapshotPath)) { throw "snapshot file is missing: $SnapshotPath" }
    if (-not [System.IO.File]::Exists($DestinationPath)) { throw "destination INI file is missing: $DestinationPath" }

    $snapshotBytes = [System.IO.File]::ReadAllBytes($SnapshotPath)
    $snapshotHash = Get-F8Sha256Hex -Bytes $snapshotBytes
    if ($snapshotBytes.Length -ne $ExpectedLength -or $snapshotHash -cne $ExpectedSha256.ToUpperInvariant()) {
        throw 'snapshot integrity check failed before restoration'
    }

    $destinationDirectory = Split-Path -Parent ([System.IO.Path]::GetFullPath($DestinationPath))
    $destinationLeaf = Split-Path -Leaf $DestinationPath
    $temporaryPath = Join-Path $destinationDirectory (".$destinationLeaf.restore.$([Guid]::NewGuid().ToString('N')).tmp")
    $moved = $false
    try {
        Write-F8BytesCreateNew -Path $temporaryPath -Bytes $snapshotBytes
        if ($null -eq $MoveProvider) {
            Initialize-F8MoveFileEx
            $MoveProvider = {
                param($temporary, $destination)
                [F8Rt2NativeMethods]::MoveFileExW($temporary, $destination, [uint32](0x1 -bor 0x8))
            }
        }
        # The mandatory gate runs after CreateNew + Flush(true) and MoveFileExW setup, immediately
        # before replacement. A failed process recheck therefore leaves the destination unchanged.
        try { $beforeReplacePassed = [bool](& $BeforeReplaceProvider $temporaryPath $DestinationPath) } catch {
            throw "before-replace safety gate failed: $($_.Exception.Message)"
        }
        if (-not $beforeReplacePassed) { throw 'before-replace safety gate failed closed' }
        try { $moved = [bool](& $MoveProvider $temporaryPath $DestinationPath) } catch {
            throw "atomic MoveFileExW replacement failed: $($_.Exception.Message)"
        }
        if (-not $moved) {
            $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "atomic MoveFileExW replacement failed (Win32=$nativeError)"
        }

        $restoredBytes = [System.IO.File]::ReadAllBytes($DestinationPath)
        $restoredHash = Get-F8Sha256Hex -Bytes $restoredBytes
        if ($restoredBytes.Length -ne $ExpectedLength -or $restoredHash -cne $ExpectedSha256.ToUpperInvariant()) {
            throw 'restored INI byte/length/SHA-256 verification failed'
        }
        return [pscustomobject]@{
            Restored = $true
            DestinationPath = [System.IO.Path]::GetFullPath($DestinationPath)
            Length = $restoredBytes.Length
            Sha256 = $restoredHash
        }
    } finally {
        if ([System.IO.File]::Exists($temporaryPath)) {
            [System.IO.File]::Delete($temporaryPath)
        }
    }
}

function Test-F8RestoreAuthorization {
    param([switch]$RestoreConfigSnapshot, [Parameter(Mandatory)][string]$SnapshotPath)
    [pscustomobject]@{
        Authorized = [bool]$RestoreConfigSnapshot.IsPresent
        SnapshotPath = [System.IO.Path]::GetFullPath($SnapshotPath)
    }
}

function Test-F8ManifestSnapshotIntegrity {
    param(
        [Parameter(Mandatory)]$Manifest,
        [Parameter(Mandatory)][string]$EvidenceDirectory
    )
    $snapshotEntry = Try-GetF8MapValue -Map $Manifest -Key 'snapshot'
    if (-not $snapshotEntry.Found) { throw 'manifest snapshot fields are missing' }
    $snapshot = $snapshotEntry.Value
    $fields = @{}
    foreach ($field in @('path', 'sha256', 'length')) {
        $entry = Try-GetF8MapValue -Map $snapshot -Key $field
        if (-not $entry.Found) { throw "manifest snapshot.$field is missing" }
        $fields[$field] = $entry.Value
    }
    $path = [string]$fields.path
    $expectedHash = ([string]$fields.sha256).ToUpperInvariant()
    $expectedLength = [long]$fields.length
    $resolvedEvidenceDirectory = [System.IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\')
    $resolvedSnapshotPath = [System.IO.Path]::GetFullPath($path)
    if ((Split-Path -Leaf $resolvedSnapshotPath) -cne 'ffx-hooks.ini.snapshot.bin') {
        throw 'manifest snapshot path must use the exact ffx-hooks.ini.snapshot.bin leaf'
    }
    $snapshotParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $resolvedSnapshotPath)).TrimEnd('\')
    if (-not $snapshotParent.Equals($resolvedEvidenceDirectory, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'manifest snapshot path is outside its exact evidence directory'
    }

    $manifestPaths = Get-F8RequiredValue -Object $Manifest -Key 'paths' -Context 'manifest'
    $manifestIni = Get-F8RequiredValue -Object $manifestPaths -Key 'ini' -Context 'manifest paths'
    $iniHash = ([string](Get-F8RequiredValue -Object $manifestIni -Key 'sha256' -Context 'manifest paths.ini')).ToUpperInvariant()
    $iniLength = [long](Get-F8RequiredValue -Object $manifestIni -Key 'length' -Context 'manifest paths.ini')
    if ($expectedHash -notmatch '^[0-9A-F]{64}$' -or $iniHash -notmatch '^[0-9A-F]{64}$' -or
        $expectedLength -lt 0 -or $iniLength -lt 0) {
        throw 'manifest snapshot/paths.ini byte identity is invalid'
    }
    # WHY: this unkeyed cross-record check catches accidental or single-record drift before
    # replacement. It establishes internal consistency, not authenticity against coordinated edits.
    if ($expectedHash -cne $iniHash -or $expectedLength -ne $iniLength) {
        throw 'manifest snapshot byte identity does not match manifest paths.ini'
    }
    if (-not [System.IO.File]::Exists($path)) { throw "manifest snapshot file is missing: $path" }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $hash = Get-F8Sha256Hex -Bytes $bytes
    if ($bytes.Length -ne $expectedLength -or $hash -cne $expectedHash) {
        throw 'manifest snapshot integrity check failed'
    }
    return $true
}

function Assert-F8ManifestShape {
    param([Parameter(Mandatory)]$Manifest, [string]$EvidenceDirectory)
    $fields = @{}
    foreach ($field in @('schema', 'case', 'sessionId', 'canonical', 'applyMode', 'preflightUtc', 'paths', 'snapshot')) {
        $entry = Try-GetF8MapValue -Map $Manifest -Key $field
        if (-not $entry.Found) { throw "manifest field is missing: $field" }
        $fields[$field] = $entry.Value
    }
    if ([string]$fields.schema -cne $script:ManifestSchema) { throw "unsupported manifest schema: $($fields.schema)" }
    if ([string]::IsNullOrEmpty([string]$fields.case)) { throw 'manifest case is empty' }
    if ([string]::IsNullOrEmpty([string]$fields.sessionId)) { throw 'manifest session is empty' }
    if ([string]$fields.applyMode -notin @('ConfigPolled', 'RuntimeAcknowledged')) {
        throw "invalid manifest apply mode: $($fields.applyMode)"
    }
    if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) { throw 'manifest evidence directory is required' }
    [void](Test-F8ManifestSnapshotIntegrity -Manifest $Manifest -EvidenceDirectory $EvidenceDirectory)
}

function Write-F8IntegrityManifest {
    param(
        [Parameter(Mandatory)]$Manifest,
        [Parameter(Mandatory)][string]$EvidenceDirectory
    )
    if (-not [System.IO.Directory]::Exists($EvidenceDirectory)) {
        throw "evidence directory does not exist: $EvidenceDirectory"
    }
    Assert-F8ManifestShape -Manifest $Manifest -EvidenceDirectory $EvidenceDirectory
    $manifestPath = Join-Path $EvidenceDirectory 'manifest.json'
    $hashPath = Join-Path $EvidenceDirectory 'manifest.sha256'
    $json = ConvertTo-Json -InputObject $Manifest -Depth 16 -Compress
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $bytes = $utf8.GetBytes($json)
    $hash = Get-F8Sha256Hex -Bytes $bytes
    Write-F8BytesCreateNew -Path $manifestPath -Bytes $bytes
    Write-F8BytesCreateNew -Path $hashPath -Bytes ([System.Text.Encoding]::ASCII.GetBytes($hash))
    [pscustomobject]@{
        ManifestPath = [System.IO.Path]::GetFullPath($manifestPath)
        HashPath = [System.IO.Path]::GetFullPath($hashPath)
        Sha256 = $hash
    }
}

function New-F8FileIntegrityRecordFromBytes {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes
    )
    [pscustomobject][ordered]@{
        path = [System.IO.Path]::GetFullPath($Path)
        length = [long]$Bytes.Length
        sha256 = Get-F8Sha256Hex -Bytes $Bytes
    }
}

function Read-F8IntegrityManifestEnvelope {
    param(
        [Parameter(Mandatory)][string]$EvidenceDirectory,
        [Parameter(Mandatory)][string]$ExpectedCase,
        [Parameter(Mandatory)][string]$ExpectedSessionId
    )
    $manifestPath = Join-Path $EvidenceDirectory 'manifest.json'
    $hashPath = Join-Path $EvidenceDirectory 'manifest.sha256'
    if (-not [System.IO.File]::Exists($manifestPath) -or -not [System.IO.File]::Exists($hashPath)) {
        throw 'manifest or manifest SHA-256 sidecar is missing'
    }
    $manifestBytes = [System.IO.File]::ReadAllBytes($manifestPath)
    $sidecarBytes = [System.IO.File]::ReadAllBytes($hashPath)
    $recordedHash = [System.Text.Encoding]::ASCII.GetString($sidecarBytes)
    if ($recordedHash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'manifest integrity sidecar is invalid' }
    $actualHash = Get-F8Sha256Hex -Bytes $manifestBytes
    if ($actualHash -cne $recordedHash.ToUpperInvariant()) { throw 'manifest SHA-256 integrity check failed' }
    try {
        $manifest = [System.Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "manifest JSON parse failed: $($_.Exception.Message)"
    }
    Assert-F8ManifestShape -Manifest $manifest -EvidenceDirectory $EvidenceDirectory
    if ([string]$manifest.case -cne $ExpectedCase) {
        throw "manifest case mismatch: expected $ExpectedCase, found $($manifest.case)"
    }
    if ([string]$manifest.sessionId -cne $ExpectedSessionId) {
        throw "manifest session mismatch: expected $ExpectedSessionId, found $($manifest.sessionId)"
    }
    # WHY: parsing, sidecar validation, and both initial pins must share these exact arrays. A
    # later path read could pin a bundle different from the manifest object Verify actually uses.
    return [pscustomobject]@{
        Manifest = $manifest
        ManifestRecord = New-F8FileIntegrityRecordFromBytes -Path $manifestPath -Bytes $manifestBytes
        SidecarRecord = New-F8FileIntegrityRecordFromBytes -Path $hashPath -Bytes $sidecarBytes
    }
}

function Read-F8IntegrityManifest {
    param(
        [Parameter(Mandatory)][string]$EvidenceDirectory,
        [Parameter(Mandatory)][string]$ExpectedCase,
        [Parameter(Mandatory)][string]$ExpectedSessionId
    )
    $envelope = Read-F8IntegrityManifestEnvelope `
        -EvidenceDirectory $EvidenceDirectory `
        -ExpectedCase $ExpectedCase `
        -ExpectedSessionId $ExpectedSessionId
    return $envelope.Manifest
}

function Resolve-F8VerifyEvidenceDirectory {
    param(
        [string]$EvidenceDirectory,
        [string]$EvidenceRoot,
        [string]$ExpectedCase,
        [string]$ExpectedSessionId
    )
    if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        throw 'Verify requires the exact EvidenceDirectory; selecting latest evidence is forbidden'
    }
    if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
        throw 'Verify requires a resolved EvidenceRoot'
    }
    $resolved = [System.IO.Path]::GetFullPath($EvidenceDirectory)
    if (-not [System.IO.Directory]::Exists($resolved)) { throw "exact EvidenceDirectory does not exist: $resolved" }
    $root = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
    if (-not [System.IO.Directory]::Exists($root)) { throw "resolved EvidenceRoot does not exist: $root" }
    $parent = [System.IO.Path]::GetFullPath((Split-Path -Parent $resolved)).TrimEnd('\')
    if (-not $parent.Equals($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "EvidenceDirectory must be a direct strict child of EvidenceRoot: $resolved"
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedCase)) {
        $leaf = Split-Path -Leaf $resolved
        $pattern = '^\d{8}T\d{9}Z_' + [regex]::Escape($ExpectedCase) + '_([0-9a-f]{32})$'
        $match = [regex]::Match($leaf, $pattern, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if (-not $match.Success) {
            throw 'EvidenceDirectory leaf must exactly match <timestamp>_<case>_<lowercase-session>'
        }
        if (-not [string]::IsNullOrWhiteSpace($ExpectedSessionId) -and $match.Groups[1].Value -cne $ExpectedSessionId) {
            throw 'EvidenceDirectory leaf session does not match the expected Preflight session'
        }
    }
    return $resolved
}

function Read-F8OpenCounter {
    param([Parameter(Mandatory)][string]$CounterPath)
    if (-not [System.IO.File]::Exists($CounterPath)) {
        return [pscustomobject]@{ Existed = $false; Value = 0 }
    }
    $bytes = [System.IO.File]::ReadAllBytes($CounterPath)
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    if ($text -notmatch '^\d+$') { throw "log open counter is malformed: [$text]" }
    $value = 0
    if (-not [int]::TryParse($text, [ref]$value) -or $value -lt 0 -or $value -gt 9) {
        throw "log open counter is outside 0..9: $text"
    }
    return [pscustomobject]@{ Existed = $true; Value = $value }
}

function Get-F8LogBoundary {
    param(
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][string]$CounterPath
    )
    if (-not [System.IO.File]::Exists($LogPath)) { throw "hook log is missing: $LogPath" }
    $prefix = [System.IO.File]::ReadAllBytes($LogPath)
    $counter = Read-F8OpenCounter -CounterPath $CounterPath
    if ($counter.Value -ge 9) {
        throw "log open counter=$($counter.Value) would rotate the log on the next open"
    }
    [pscustomobject]@{
        LogPath = [System.IO.Path]::GetFullPath($LogPath)
        CounterPath = [System.IO.Path]::GetFullPath($CounterPath)
        StartLength = [long]$prefix.Length
        PrefixSha256 = Get-F8Sha256Hex -Bytes $prefix
        OpenCounter = [int]$counter.Value
        CounterExisted = [bool]$counter.Existed
    }
}

function Read-F8StreamBytes {
    param([Parameter(Mandatory)][System.IO.FileStream]$Stream, [Parameter(Mandatory)][int]$Length)
    $bytes = New-Object byte[] $Length
    $offset = 0
    while ($offset -lt $Length) {
        $read = $Stream.Read($bytes, $offset, $Length - $offset)
        if ($read -le 0) { throw "unexpected end of file after $offset of $Length bytes" }
        $offset += $read
    }
    return $bytes
}

function Read-F8LogSlice {
    param(
        [Parameter(Mandatory)][string]$LogPath,
        [Parameter(Mandatory)][long]$StartLength,
        [Parameter(Mandatory)][string]$PrefixSha256
    )
    if ($StartLength -lt 0 -or $StartLength -gt [int]::MaxValue) { throw "invalid log start length: $StartLength" }
    $stream = New-Object System.IO.FileStream(
        $LogPath,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    )
    try {
        if ($stream.Length -lt $StartLength) {
            throw "log was truncated or rotated: current length $($stream.Length) < start length $StartLength"
        }
        $prefix = if ($StartLength -eq 0) { [byte[]]@() } else { Read-F8StreamBytes -Stream $stream -Length ([int]$StartLength) }
        $actualPrefixHash = Get-F8Sha256Hex -Bytes $prefix
        if ($actualPrefixHash -cne $PrefixSha256.ToUpperInvariant()) {
            throw 'log prefix SHA-256 mismatch; rotation or in-place modification detected'
        }
        $sliceLength = [int]($stream.Length - $StartLength)
        if ($sliceLength -eq 0) {
            return [pscustomobject]@{
                Bytes = [byte[]]@()
                Length = 0
                Sha256 = Get-F8Sha256Hex -Bytes ([byte[]]@())
            }
        }
        $slice = Read-F8StreamBytes -Stream $stream -Length $sliceLength
        return [pscustomobject]@{
            Bytes = $slice
            Length = $slice.Length
            Sha256 = Get-F8Sha256Hex -Bytes $slice
        }
    } finally {
        $stream.Dispose()
    }
}

function Test-F8LogCounterTransition {
    param(
        [Parameter(Mandatory)][int]$StartCounter,
        [Parameter(Mandatory)][int]$CurrentCounter,
        [switch]$StartCounterExisted
    )
    $expected = if ($StartCounterExisted.IsPresent) { $StartCounter + 1 } else { 1 }
    if ($expected -ge 10) { throw 'recorded counter would imply a log rotation' }
    if ($CurrentCounter -ne $expected) {
        throw "unexpected log session counter drift: expected $expected, found $CurrentCounter"
    }
    return $true
}

function Find-F8LogLineIndex {
    param([string[]]$Lines, [string]$Pattern, [int]$After = -1)
    for ($i = $After + 1; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -cmatch $Pattern) { return $i }
    }
    return -1
}

function Test-F8LogEvidence {
    param(
        [Parameter(Mandatory)]$CaseSpec,
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$SliceBytes,
        [switch]$ObservedApplied,
        [switch]$ObservedRestored
    )
    if (-not $ObservedApplied.IsPresent) { throw 'manual observation attestation ObservedApplied is required' }
    if (-not $ObservedRestored.IsPresent) { throw 'manual observation attestation ObservedRestored is required' }

    $text = [System.Text.Encoding]::UTF8.GetString($SliceBytes)
    if ($text -match '(?i)(crash|exception|access[ _-]?violation|conflict|restore\s*pending|restorepending)') {
        throw 'unsafe crash/exception/access-violation/conflict/restore-pending token in the new log slice'
    }
    $lines = @([regex]::Split($text, '\r?\n') | Where-Object { $_.Length -gt 0 })
    $commonPattern = '^\[ffx-hooks\] F8 catalog rows=37 live=14 restart=16 not_wired=7$'
    $commonMatches = @($lines | Where-Object { $_ -cmatch $commonPattern })
    if ($commonMatches.Count -ne 1) {
        throw "common catalog anchor/session count must be exactly one; found $($commonMatches.Count)"
    }

    $canonical = [string]$CaseSpec.Canonical
    $selected = [regex]::Escape($canonical)
    $isScalarCase = -not [string]::IsNullOrEmpty([string]$CaseSpec.ScalarKey)
    $expectedSource = if ($CaseSpec.Case -ceq 'arena_plus_compose_f7') { 'AuthoritativeCanonicalIni' } else { 'UnmarkedCanonicalIni' }
    $editPattern = '^\[ffx-hooks\] F8 edit key=([^ ]+) edit=(.*?) requested=([01]) effective=([01]) source=([^ ]+)$'
    $scalarEditPattern = '^\[ffx-hooks\] F8 scalar edit key=([^ ]+) edit=(.*?) requested=(-?[0-9]+) configured=(-?[0-9]+)$'
    $runtimeFailurePattern = '^\[f8-runtime\]\s+key=([^ ]+).*?\bfailure=([^\s]+)(?:\s|$)'
    $scalarRuntimePattern = "^\[f8-runtime\] key=$selected effective=([01]) source=$expectedSource state=([a-z]+) gate=([0-9A-F]{2}) scalar=(none|[0-9]+)$"
    $selectedRuntimePrefix = "[f8-runtime] key=$canonical "
    $scalarEdits = @()
    $selectedEdits = @()
    $selectedScalarRuntime = @()
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; ++$lineIndex) {
        $line = $lines[$lineIndex]
        if ($line.StartsWith('[ffx-hooks] F8 edit ', [System.StringComparison]::Ordinal)) {
            $editMatch = [regex]::Match($line, $editPattern, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
            if (-not $editMatch.Success) { throw 'malformed structured F8 edit record in the new log slice' }
            $editKey = $editMatch.Groups[1].Value
            $editCode = $editMatch.Groups[2].Value
            $requested = $editMatch.Groups[3].Value
            # WHY: only SAVED proves durable intent. Rejection, persistence failure, and unknown
            # outcomes cannot coexist with anchors that authorize a vanilla-restoration verdict.
            if ($editCode -cne 'SAVED') {
                throw "non-SAVED F8 edit record found for ${editKey}: $editCode"
            }
            if ($requested -ceq '1' -and $editKey -cne [string]$CaseSpec.Canonical) {
                throw "other target ON request found: $editKey"
            }
            if ($editKey -ceq $canonical) {
                $selectedEdits += [pscustomobject]@{
                    Index = $lineIndex
                    Requested = $requested
                    Effective = $editMatch.Groups[4].Value
                    Source = $editMatch.Groups[5].Value
                }
            }
        }

        if ($line.StartsWith('[ffx-hooks] F8 scalar edit ', [System.StringComparison]::Ordinal)) {
            $scalarMatch = [regex]::Match(
                $line,
                $scalarEditPattern,
                [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
            )
            if (-not $scalarMatch.Success) {
                throw 'malformed structured F8 scalar edit record in the new log slice'
            }
            [int]$requestedScalar = 0
            [int]$configuredScalar = 0
            if (-not [int]::TryParse(
                    $scalarMatch.Groups[3].Value,
                    [System.Globalization.NumberStyles]::AllowLeadingSign,
                    [System.Globalization.CultureInfo]::InvariantCulture,
                    [ref]$requestedScalar
                ) -or
                -not [int]::TryParse(
                    $scalarMatch.Groups[4].Value,
                    [System.Globalization.NumberStyles]::AllowLeadingSign,
                    [System.Globalization.CultureInfo]::InvariantCulture,
                    [ref]$configuredScalar
                )) {
                throw 'F8 scalar edit value is outside the signed 32-bit log contract'
            }
            $scalarEdits += [pscustomobject]@{
                Index = $lineIndex
                Key = $scalarMatch.Groups[1].Value
                Edit = $scalarMatch.Groups[2].Value
                Requested = $requestedScalar
                Configured = $configuredScalar
            }
        }

        $failureMatch = [regex]::Match(
            $line,
            $runtimeFailurePattern,
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase -bor [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
        )
        if ($failureMatch.Success -and $failureMatch.Groups[2].Value -ine 'none') {
            throw "unsafe runtime failure record for $($failureMatch.Groups[1].Value): $($failureMatch.Groups[2].Value)"
        }

        if ($isScalarCase -and $line.StartsWith($selectedRuntimePrefix, [System.StringComparison]::Ordinal)) {
            $runtimeMatch = [regex]::Match(
                $line,
                $scalarRuntimePattern,
                [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
            )
            if (-not $runtimeMatch.Success) {
                throw 'malformed or contradictory selected scalar runtime record in the new log slice'
            }
            $selectedScalarRuntime += [pscustomobject]@{
                Index = $lineIndex
                Effective = $runtimeMatch.Groups[1].Value
                State = $runtimeMatch.Groups[2].Value
                Gate = $runtimeMatch.Groups[3].Value
                Scalar = $runtimeMatch.Groups[4].Value
            }
        }

        $appliedMatch = [regex]::Match($line, '^\[f8-runtime\] key=([^ ]+).*\bstate=applied\b', [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if ($appliedMatch.Success) {
            if ($appliedMatch.Groups[1].Value -cne [string]$CaseSpec.Canonical) {
                throw "other target applied state found: $($appliedMatch.Groups[1].Value)"
            }
        }
    }

    if (-not $isScalarCase -and $scalarEdits.Count -ne 0) {
        throw 'unexpected scalar edit record for a non-scalar RT2 case'
    }

    $commonIndex = Find-F8LogLineIndex -Lines $lines -Pattern $commonPattern
    $scalarEditIndex = -1
    $appliedScalar = 0
    if ($isScalarCase) {
        if ($scalarEdits.Count -ne 1) {
            throw "scalar reward evidence requires exactly one selected scalar edit; found $($scalarEdits.Count)"
        }
        $scalarEdit = $scalarEdits[0]
        if ([string]$scalarEdit.Key -cne [string]$CaseSpec.ScalarKey) {
            throw "other scalar key found: $($scalarEdit.Key)"
        }
        if ([string]$scalarEdit.Edit -cne 'SAVED') {
            throw "non-SAVED F8 scalar edit record found for $($scalarEdit.Key): $($scalarEdit.Edit)"
        }
        if ([int]$scalarEdit.Requested -ne [int]$scalarEdit.Configured) {
            throw 'scalar configured value does not match the requested value'
        }
        $appliedScalar = [int]$scalarEdit.Configured
        if ($appliedScalar -lt [int]$CaseSpec.ScalarMinimum -or
            $appliedScalar -gt [int]$CaseSpec.ScalarMaximum) {
            throw "selected scalar is outside the admitted range $($CaseSpec.ScalarMinimum)..$($CaseSpec.ScalarMaximum)"
        }
        if ($appliedScalar -eq [int]$CaseSpec.ScalarDefault) {
            throw 'selected scalar must be non-default so RT2 exercises live data reconfiguration'
        }
        $scalarEditIndex = [int]$scalarEdit.Index
    }

    $selectedOn = @($selectedEdits | Where-Object { $_.Requested -ceq '1' })
    if ($selectedOn.Count -ne 1) {
        throw "selected ON request anchor count must be exactly one; found $($selectedOn.Count)"
    }
    if ($selectedOn[0].Effective -cne '1') {
        throw 'selected ON request did not report effective=1'
    }
    if ($selectedOn[0].Source -cne $expectedSource) {
        throw "selected ON source mismatch; expected $expectedSource"
    }

    $selectedOff = @($selectedEdits | Where-Object { $_.Requested -ceq '0' })
    if ($selectedOff.Count -ne 1) {
        throw "selected OFF request anchor count must be exactly one; found $($selectedOff.Count)"
    }
    if ($selectedOff[0].Effective -cne '0') {
        throw 'selected OFF request did not report effective=0'
    }
    if ($selectedOff[0].Source -cne $expectedSource) {
        throw "selected OFF source mismatch; expected $expectedSource"
    }

    $onIndex = [int]$selectedOn[0].Index
    $offIndex = [int]$selectedOff[0].Index
    if ($onIndex -le $commonIndex) { throw 'ordered selected ON anchor is missing after the common anchor' }
    if ($offIndex -le $onIndex) { throw 'ordered selected OFF anchor is missing after ON' }
    if ($isScalarCase -and ($scalarEditIndex -le $commonIndex -or $scalarEditIndex -ge $onIndex)) {
        throw 'ordered selected scalar edit must occur after the common anchor and before ON'
    }

    $applyIndex = $onIndex
    $restoreIndex = $offIndex
    if ($isScalarCase) {
        $beforeOn = @($selectedScalarRuntime | Where-Object { $_.Index -gt $commonIndex -and $_.Index -lt $onIndex })
        $atOrBeforeCommon = @($selectedScalarRuntime | Where-Object { $_.Index -le $commonIndex })
        if ($atOrBeforeCommon.Count -ne 0) {
            throw 'selected scalar runtime record appears before the protocol common anchor'
        }
        foreach ($record in $beforeOn) {
            if ($record.Effective -cne '0' -or $record.State -cne 'restored' -or
                $record.Gate -cne '00' -or $record.Scalar -cne 'none') {
                throw 'only truthful baseline restored gate=00 scalar=none records are allowed before ON'
            }
        }
        $onWindow = @($selectedScalarRuntime | Where-Object { $_.Index -gt $onIndex -and $_.Index -lt $offIndex })
        if ($onWindow.Count -ne 1) {
            throw "scalar ON window requires exactly one selected runtime record; found $($onWindow.Count)"
        }
        $applied = $onWindow[0]
        if ($applied.Effective -cne '1' -or $applied.State -cne 'applied' -or
            $applied.Gate -cne '01' -or $applied.Scalar -cne [string]$appliedScalar) {
            throw 'ordered runtime applied gate/scalar readback does not match the selected scalar'
        }
        $applyIndex = [int]$applied.Index

        $afterOff = @($selectedScalarRuntime | Where-Object { $_.Index -gt $offIndex })
        if ($afterOff.Count -ne 1) {
            throw "scalar OFF window requires exactly one selected runtime restoration; found $($afterOff.Count)"
        }
        $restored = $afterOff[0]
        if ($restored.Effective -cne '0' -or $restored.State -cne 'restored' -or
            $restored.Gate -cne '00' -or $restored.Scalar -cne 'none') {
            throw 'ordered runtime restored gate=00 scalar=none record is missing'
        }
        $restoreIndex = [int]$restored.Index
    } elseif ($CaseSpec.ApplyMode -ceq 'RuntimeAcknowledged') {
        $applyPattern = "^\[f8-runtime\] key=$selected .*\beffective=1\b.*\bsource=$expectedSource\b.*\bstate=applied\b"
        $applyIndex = Find-F8LogLineIndex -Lines $lines -Pattern $applyPattern -After $onIndex
        if ($applyIndex -lt 0 -or $applyIndex -ge $offIndex) {
            throw 'ordered runtime applied anchor is missing before OFF'
        }
        $restorePattern = "^\[f8-runtime\] key=$selected .*\beffective=0\b.*\bsource=$expectedSource\b.*\bstate=restored\b"
        $restoreIndex = Find-F8LogLineIndex -Lines $lines -Pattern $restorePattern -After $offIndex
        if ($restoreIndex -lt 0) { throw 'ordered runtime restored anchor is missing' }
    }

    [pscustomobject]@{
        Valid = $true
        ApplyMode = [string]$CaseSpec.ApplyMode
        CommonIndex = $commonIndex
        OnIndex = $onIndex
        ApplyIndex = $applyIndex
        OffIndex = $offIndex
        RestoreIndex = $restoreIndex
        ScalarEditIndex = $scalarEditIndex
        AppliedScalar = $appliedScalar
    }
}

function Resolve-F8StrictScalarConfiguration {
    param(
        [Parameter(Mandatory)]$Ini,
        [Parameter(Mandatory)]$Spec
    )
    $key = [string]$Spec.ScalarKey
    if ([string]::IsNullOrEmpty($key)) { throw 'scalar configuration resolver requires a scalar case spec' }
    $entry = Try-GetF8MapValue -Map $Ini -Key $key
    if (-not $entry.Found) {
        return [pscustomobject]@{ Key = $key; Present = $false; Value = [int]$Spec.ScalarDefault; Source = 'MissingDefault' }
    }

    $text = [string]$entry.Value
    [int]$value = 0
    $digitsOnly = $text -cmatch '^[0-9]+$'
    $int32Safe = $digitsOnly -and [int]::TryParse(
        $text,
        [System.Globalization.NumberStyles]::None,
        [System.Globalization.CultureInfo]::InvariantCulture,
        [ref]$value
    )
    if (-not $int32Safe -or $value -lt [int]$Spec.ScalarMinimum -or
        $value -gt [int]$Spec.ScalarMaximum) {
        throw "invalid reward scalar [$key]=$text; repair it with digits-only decimal $($Spec.ScalarMinimum)..$($Spec.ScalarMaximum), or remove the key to restore default $($Spec.ScalarDefault)"
    }
    return [pscustomobject]@{ Key = $key; Present = $true; Value = $value; Source = 'CanonicalIni' }
}

function Resolve-F8AllRewardScalarConfigurations {
    param([Parameter(Mandatory)]$Ini)
    $resolved = New-Object System.Collections.Generic.List[object]
    $errors = New-Object System.Collections.Generic.List[string]
    foreach ($spec in @(Get-F8Rt2CaseTable | Where-Object { -not [string]::IsNullOrEmpty([string]$_.ScalarKey) })) {
        try {
            $resolved.Add((Resolve-F8StrictScalarConfiguration -Ini $Ini -Spec $spec))
        } catch {
            $errors.Add($_.Exception.Message)
        }
    }
    # WHY: every Preflight validates both AP and Gil regardless of the selected case. Collecting
    # both diagnostics prevents one malformed key from hiding the repair needed for its peer.
    if ($errors.Count -ne 0) { throw ($errors -join '; ') }
    return $resolved.ToArray()
}

function Test-F8FinalVerdict {
    param(
        [Parameter(Mandatory)]$Checks,
        [string[]]$AdditionalRequired = @()
    )
    $required = @(
        'ObservedApplied',
        'ObservedRestored',
        'LogValid',
        'LogPrefixValid',
        'SnapshotBytesValid',
        'BuiltDllHashUnchanged',
        'InstalledDllHashUnchanged',
        'ExecutableHashUnchanged',
        'IniHashRestored',
        'AllFourteenOff',
        'RestorationVerified'
    )
    $required += @($AdditionalRequired)
    foreach ($key in $required) {
        $entry = Try-GetF8MapValue -Map $Checks -Key $key
        if (-not $entry.Found -or -not [bool]$entry.Value) { return $false }
    }
    return $true
}

function Get-F8RequiredValue {
    param([Parameter(Mandatory)]$Object, [Parameter(Mandatory)][string]$Key, [string]$Context = 'object')
    $entry = Try-GetF8MapValue -Map $Object -Key $Key
    if (-not $entry.Found) { throw "$Context field is missing: $Key" }
    return $entry.Value
}

function Get-F8ProviderValue {
    param($Providers, [string]$Name, $DefaultValue = $null)
    $entry = Try-GetF8MapValue -Map $Providers -Key $Name
    if ($entry.Found) { return $entry.Value }
    return $DefaultValue
}

function Get-F8DefaultPathSet {
    $gameRoot = 'D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster'
    $temporary = [System.IO.Path]::GetTempPath()
    [ordered]@{
        BuiltDll = 'C:\Users\wande\Documents\ffx-hooks\src\runtime\FfxHooksDll\bin\Release\ffx-hooks.dll'
        Exe = Join-Path $gameRoot 'FFX.exe'
        InstalledDll = Join-Path $gameRoot 'modules\ffx-hooks.dll'
        Ini = Join-Path $gameRoot '_isolated\ffx-hooks.ini'
        Log = Join-Path $temporary 'ffx-hooks.log'
        Counter = Join-Path $temporary 'ffx-hooks.log.cnt'
        GameRoot = $gameRoot
    }
}

function Get-F8ProcessEnvironment {
    $result = @{}
    foreach ($spec in @(Get-F8Rt2CaseTable)) {
        foreach ($name in @($spec.DisableEnvironmentName, $spec.EnvironmentName)) {
            if ([string]::IsNullOrEmpty([string]$name) -or $result.ContainsKey($name)) { continue }
            $value = [Environment]::GetEnvironmentVariable($name, [EnvironmentVariableTarget]::Process)
            if ($null -ne $value) { $result[$name] = $value }
        }
    }
    return $result
}

function Get-F8DefaultEvidenceRoot {
    param([Parameter(Mandatory)][string]$ScriptRoot)
    if ([string]::IsNullOrWhiteSpace($ScriptRoot)) { throw 'ScriptRoot is required to derive the default EvidenceRoot' }
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot '..\..\..'))
    $root = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'work\f8_rt2')).TrimEnd('\')
    $volumeRoot = [System.IO.Path]::GetPathRoot($root).TrimEnd('\')
    if ($root -ieq $volumeRoot) { throw "default evidence root may not be a filesystem root: $root" }
    return $root
}

function New-F8EvidenceDirectory {
    param(
        [Parameter(Mandatory)][string]$EvidenceRoot,
        [Parameter(Mandatory)][string]$Case,
        [Parameter(Mandatory)][DateTimeOffset]$Now,
        [Parameter(Mandatory)][string]$SessionId
    )
    $root = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
    $volumeRoot = [System.IO.Path]::GetPathRoot($root).TrimEnd('\')
    if ($root -ieq $volumeRoot) { throw "evidence root may not be a filesystem root: $root" }
    [System.IO.Directory]::CreateDirectory($root) | Out-Null
    $stamp = $Now.UtcDateTime.ToString('yyyyMMddTHHmmssfffZ')
    $leaf = "${stamp}_${Case}_${SessionId}"
    $directory = [System.IO.Path]::GetFullPath((Join-Path $root $leaf))
    if (-not ($directory + '\').StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "unsafe evidence directory resolution: $directory"
    }
    if ([System.IO.Directory]::Exists($directory) -or [System.IO.File]::Exists($directory)) {
        throw "unique evidence directory already exists: $directory"
    }
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    return $directory
}

function ConvertTo-F8PathManifestRecord {
    param([Parameter(Mandatory)]$Evidence)
    [ordered]@{
        path = [string]$Evidence.Path
        length = [long]$Evidence.Length
        sha256 = [string]$Evidence.Sha256
    }
}

function Get-F8ManualSteps {
    param([Parameter(Mandatory)]$Spec)
    if ($Spec.Case -ceq 'seymour_battle_roster') {
        return @(
            'Launch FFX manually with the disposable save and do not open Sphere Grid.',
            'Record the runtime module base and snapshot all four declared leaves as before while the row is OFF.',
            'Toggle only Playable Seymour ON, enter one battle, snapshot all four leaves as on, observe Seymour selection, and complete one controlled turn.',
            'Perform exactly one legitimate Switch and snapshot all four leaves as afterSwitch to prove membership-preserving order change.',
            'Toggle the row OFF, leave through the normal battle exit, and snapshot all four leaves as off; persistent lists must be restored while local lists may retain the current-battle image until reinitialization.',
            'Enter the next battle, snapshot all four leaves as nextBattle, and confirm no reintroduction of actor ID 7.',
            'Write the exact schema JSON, pass it through SeymourEvidencePath, then close FFX manually before Verify; every snapshot and log anchor remains unproven until validation succeeds.'
        )
    }
    if ($Spec.Case -ceq 'arena_plus_compose_f7') {
        return @(
            'Launch FFX manually with the disposable save.',
            'Open F8 > Arena+ and toggle only Arena+ Compose F7 ON; do not open Compose or mutate battle files.',
            'Observe the row effective state, then toggle the same row OFF and observe restoration.',
            'Close FFX manually before Verify.'
        )
    }
    if (-not [string]::IsNullOrEmpty([string]$Spec.ScalarKey)) {
        return @(
            'Launch FFX manually with the disposable save.',
            "Record the baseline reward for [$($Spec.Canonical)] while the row is OFF.",
            "Open F8 > Cheats, set one non-default Rate from $($Spec.ScalarMinimum)x through $($Spec.ScalarMaximum)x, and Confirm the save.",
            'Toggle only the matching reward row ON and observe the exact applied non-default Rate.',
            'Toggle the same reward row OFF and observe gate restoration to the baseline behavior.',
            'Close FFX manually before Verify.'
        )
    }
    @(
        'Launch FFX manually with the disposable save.',
        "Open F8 and toggle only [$($Spec.Canonical)] ON; observe the selected behavior.",
        'Toggle the same row OFF and observe restoration.',
        'Close FFX manually before Verify.'
    )
}

function Test-F8BytesEqual {
    param([byte[]]$Left, [byte[]]$Right)
    if ($null -eq $Left -or $null -eq $Right -or $Left.Length -ne $Right.Length) { return $false }
    for ($i = 0; $i -lt $Left.Length; ++$i) {
        if ($Left[$i] -ne $Right[$i]) { return $false }
    }
    return $true
}

function Assert-F8ExactObjectProperties {
    param(
        [Parameter(Mandatory)]$Object,
        [Parameter(Mandatory)][string[]]$Expected,
        [Parameter(Mandatory)][string]$Context
    )
    if ($null -eq $Object -or $Object -isnot [System.Management.Automation.PSCustomObject]) {
        throw "$Context must be a JSON object"
    }
    $actual = @($Object.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Expected.Count) {
        throw "$Context property set must be exact: $($Expected -join ', ')"
    }
    foreach ($name in $Expected) {
        if ($actual -cnotcontains $name) { throw "$Context field is missing or mis-cased: $name" }
    }
    foreach ($name in $actual) {
        if ($Expected -cnotcontains $name) { throw "$Context has an unexpected or ambiguous field: $name" }
    }
}

function ConvertFrom-F8StrictAddress {
    param([Parameter(Mandatory)]$Value, [Parameter(Mandatory)][string]$Context)
    if ($Value -isnot [string] -or [string]$Value -cnotmatch '^0x[0-9A-F]{8}$') {
        throw "$Context must be an unambiguous uppercase 32-bit hexadecimal address"
    }
    try { return [Convert]::ToUInt64(([string]$Value).Substring(2), 16) } catch {
        throw "$Context is outside the 32-bit address range"
    }
}

function ConvertFrom-F8SnapshotBytes {
    param(
        [Parameter(Mandatory)]$Value,
        [Parameter(Mandatory)][int]$ExpectedSize,
        [Parameter(Mandatory)][string]$Context
    )
    if ($Value -isnot [string]) { throw "$Context raw-memory snapshot must be a hex byte string" }
    $pattern = if ($ExpectedSize -eq 1) { '^[0-9A-F]{2}$' } else { '^[0-9A-F]{2}(?: [0-9A-F]{2}){' + ($ExpectedSize - 1) + '}$' }
    if ([string]$Value -cnotmatch $pattern) {
        throw "$Context raw-memory snapshot must contain exactly $ExpectedSize unambiguous uppercase hex bytes"
    }
    $result = New-Object byte[] $ExpectedSize
    $tokens = ([string]$Value).Split(' ')
    for ($i = 0; $i -lt $ExpectedSize; ++$i) { $result[$i] = [Convert]::ToByte($tokens[$i], 16) }
    return $result
}

function Join-F8SeymourLeafBytes {
    param(
        [Parameter(Mandatory)]$DecodedLeaves,
        [Parameter(Mandatory)][string[]]$Names,
        [Parameter(Mandatory)][string]$Phase
    )
    $joined = New-Object 'System.Collections.Generic.List[byte]'
    foreach ($name in $Names) {
        foreach ($value in [byte[]]$DecodedLeaves[$name][$Phase]) { $joined.Add($value) }
    }
    return $joined.ToArray()
}

function Test-F8ByteMultisetEqual {
    param([Parameter(Mandatory)][byte[]]$Left, [Parameter(Mandatory)][byte[]]$Right)
    if ($Left.Length -ne $Right.Length) { return $false }
    $counts = New-Object int[] 256
    foreach ($value in $Left) { ++$counts[[int]$value] }
    foreach ($value in $Right) { --$counts[[int]$value] }
    foreach ($count in $counts) { if ($count -ne 0) { return $false } }
    return $true
}

function Test-F8SingleFfToSeymourReplacement {
    param([Parameter(Mandatory)][byte[]]$Baseline, [Parameter(Mandatory)][byte[]]$Applied)
    if ($Baseline.Length -ne $Applied.Length) { return $false }
    $counts = New-Object int[] 256
    foreach ($value in $Applied) { ++$counts[[int]$value] }
    foreach ($value in $Baseline) { --$counts[[int]$value] }
    for ($i = 0; $i -lt $counts.Length; ++$i) {
        $expected = if ($i -eq 7) { 1 } elseif ($i -eq 0xFF) { -1 } else { 0 }
        if ($counts[$i] -ne $expected) { return $false }
    }
    return $true
}

function Assert-F8NoDuplicateJsonProperties {
    param([Parameter(Mandatory)][string]$Text)

    # Windows PowerShell keeps the last duplicate JSON key. Decode property strings first so an
    # escaped duplicate cannot hide an earlier address, phase, or observation record.
    $scopes = New-Object System.Collections.ArrayList
    for ($index = 0; $index -lt $Text.Length; ++$index) {
        $character = $Text[$index]
        if ($character -eq '{') {
            $scope = New-Object 'System.Collections.Generic.Dictionary[string,bool]' ([System.StringComparer]::Ordinal)
            [void]$scopes.Add($scope)
            continue
        }
        if ($character -eq '}') {
            if ($scopes.Count -eq 0) { throw 'JSON object scope is unbalanced' }
            $scopes.RemoveAt($scopes.Count - 1)
            continue
        }
        if ($character -ne '"') { continue }

        $decoded = New-Object System.Text.StringBuilder
        $closed = $false
        while (++$index -lt $Text.Length) {
            $character = $Text[$index]
            if ($character -eq '"') { $closed = $true; break }
            if ($character -ne '\') { [void]$decoded.Append($character); continue }
            if (++$index -ge $Text.Length) { throw 'JSON string ends in an incomplete escape' }
            $escape = $Text[$index]
            switch ($escape) {
                '"' { [void]$decoded.Append('"') }
                '\' { [void]$decoded.Append('\') }
                '/' { [void]$decoded.Append('/') }
                'b' { [void]$decoded.Append([char]0x08) }
                'f' { [void]$decoded.Append([char]0x0C) }
                'n' { [void]$decoded.Append([char]0x0A) }
                'r' { [void]$decoded.Append([char]0x0D) }
                't' { [void]$decoded.Append([char]0x09) }
                'u' {
                    if ($index + 4 -ge $Text.Length) { throw 'JSON Unicode escape is incomplete' }
                    $hex = $Text.Substring($index + 1, 4)
                    if ($hex -cnotmatch '^[0-9A-Fa-f]{4}$') { throw 'JSON Unicode escape is invalid' }
                    [void]$decoded.Append([char][Convert]::ToUInt16($hex, 16))
                    $index += 4
                }
                default { throw "JSON escape is invalid: $escape" }
            }
        }
        if (-not $closed) { throw 'JSON string is incomplete' }
        $lookahead = $index + 1
        while ($lookahead -lt $Text.Length -and [char]::IsWhiteSpace($Text[$lookahead])) {
            ++$lookahead
        }
        if ($lookahead -ge $Text.Length -or $Text[$lookahead] -ne ':') { continue }
        if ($scopes.Count -eq 0) { throw 'JSON property appeared outside an object' }
        $name = $decoded.ToString()
        $scope = $scopes[$scopes.Count - 1]
        if ($scope.ContainsKey($name)) { throw "duplicate JSON property is ambiguous: $name" }
        $scope.Add($name, $true)
    }
    if ($scopes.Count -ne 0) { throw 'JSON object scope is incomplete' }
}

function Read-F8SeymourMemoryEvidence {
    param([Parameter(Mandatory)][string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'SeymourEvidencePath is required for raw-memory evidence' }
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not [System.IO.File]::Exists($resolved)) { throw "Seymour raw-memory evidence file is missing: $resolved" }
    $bytes = [System.IO.File]::ReadAllBytes($resolved)
    if ($bytes.Length -eq 0 -or $bytes.Length -gt 65535) {
        throw 'Seymour raw-memory evidence must be nonempty and no larger than 65,535 bytes'
    }
    try {
        $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
        $text = $utf8.GetString($bytes)
        Assert-F8NoDuplicateJsonProperties -Text $text
        $root = $text | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "Seymour raw-memory evidence is not strict UTF-8 JSON: $($_.Exception.Message)"
    }

    Assert-F8ExactObjectProperties -Object $root -Expected @('schema', 'executable', 'captureSequence', 'leaves', 'observations') -Context 'Seymour evidence'
    if ([string]$root.schema -cne $script:SeymourMemoryEvidenceSchema) { throw 'unsupported Seymour raw-memory evidence schema' }
    Assert-F8ExactObjectProperties -Object $root.executable -Expected @('sha256', 'moduleBase') -Context 'Seymour executable identity'
    if ($root.executable.sha256 -isnot [string] -or [string]$root.executable.sha256 -cne $script:SupportedExecutableSha256) {
        throw 'Seymour evidence executable SHA-256 does not match the supported profile'
    }
    [uint64]$moduleBase = ConvertFrom-F8StrictAddress -Value $root.executable.moduleBase -Context 'Seymour module base'
    if ($moduleBase -eq 0 -or ($moduleBase % 0x10000) -ne 0) {
        throw 'Seymour module base must be a nonzero 64-KiB-aligned runtime image base'
    }

    if ($root.captureSequence -isnot [System.Array]) { throw 'Seymour captureSequence must be an explicit five-phase array' }
    $captureSequence = @($root.captureSequence)
    if ($captureSequence.Count -ne $script:SeymourCaptureSequence.Count) { throw 'Seymour captureSequence must contain exactly five phases' }
    for ($i = 0; $i -lt $script:SeymourCaptureSequence.Count; ++$i) {
        if ($captureSequence[$i] -isnot [string] -or [string]$captureSequence[$i] -cne $script:SeymourCaptureSequence[$i]) {
            throw "Seymour captureSequence phase $i is missing, reordered, or ambiguous"
        }
    }

    if ($root.leaves -isnot [System.Array]) { throw 'Seymour leaves must be an explicit four-record array' }
    $leaves = @($root.leaves)
    $spec = Get-F8CaseSpec -Case 'seymour_battle_roster'
    if ($leaves.Count -ne $spec.RuntimeLeaves.Count) { throw 'Seymour evidence must contain exactly four declared memory leaves' }
    $decodedLeaves = New-Object 'System.Collections.Generic.Dictionary[string,object]' ([System.StringComparer]::Ordinal)
    $normalizedLeaves = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $spec.RuntimeLeaves.Count; ++$i) {
        $expectedLeaf = $spec.RuntimeLeaves[$i]
        $leaf = $leaves[$i]
        $context = "Seymour leaf[$i]"
        Assert-F8ExactObjectProperties -Object $leaf -Expected @('name', 'rva', 'runtimeVa', 'size', 'snapshots') -Context $context
        if ($leaf.name -isnot [string] -or [string]$leaf.name -cne [string]$expectedLeaf.Name) { throw "$context name/order is ambiguous" }
        if ($decodedLeaves.ContainsKey([string]$leaf.name)) { throw "$context duplicates a declared leaf" }
        if ($leaf.rva -isnot [string] -or [string]$leaf.rva -cne [string]$expectedLeaf.Rva) { throw "$context RVA does not match the declared leaf" }
        [uint64]$rva = ConvertFrom-F8StrictAddress -Value $leaf.rva -Context "$context RVA"
        [uint64]$runtime = $moduleBase + $rva
        if ($runtime -gt [uint32]::MaxValue) { throw "$context computed runtime VA exceeds the 32-bit address space" }
        $expectedRuntimeVa = '0x{0:X8}' -f $runtime
        if ($leaf.runtimeVa -isnot [string] -or [string]$leaf.runtimeVa -cne $expectedRuntimeVa) {
            throw "$context runtime VA must equal module base plus RVA ($expectedRuntimeVa)"
        }
        if ($leaf.size -is [bool] -or [string]$leaf.size -cne [string][int]$expectedLeaf.Size) { throw "$context size is not the exact declared byte width" }
        Assert-F8ExactObjectProperties -Object $leaf.snapshots -Expected $script:SeymourCaptureSequence -Context "$context snapshots"
        $decoded = [ordered]@{}
        $normalizedSnapshots = [ordered]@{}
        foreach ($phase in $script:SeymourCaptureSequence) {
            $value = Get-F8RequiredValue -Object $leaf.snapshots -Key $phase -Context "$context snapshots"
            $decoded[$phase] = ConvertFrom-F8SnapshotBytes -Value $value -ExpectedSize ([int]$expectedLeaf.Size) -Context "$context $phase"
            $normalizedSnapshots[$phase] = [string]$value
        }
        $decodedLeaves.Add([string]$leaf.name, $decoded)
        $normalizedLeaves.Add([ordered]@{
            name = [string]$leaf.name
            rva = [string]$leaf.rva
            runtimeVa = [string]$leaf.runtimeVa
            size = [int]$expectedLeaf.Size
            snapshots = $normalizedSnapshots
        })
    }

    $observationNames = @(
        'seymourSelected', 'controlledTurnCompleted', 'switchCompleted', 'normalExitCompleted',
        'nextBattleEntered', 'nextBattleNoReintroduction', 'sphereGridNotOpened'
    )
    Assert-F8ExactObjectProperties -Object $root.observations -Expected $observationNames -Context 'Seymour observations'
    $normalizedObservations = [ordered]@{}
    foreach ($name in $observationNames) {
        $value = Get-F8RequiredValue -Object $root.observations -Key $name -Context 'Seymour observations'
        if ($value -isnot [bool] -or -not [bool]$value) { throw "Seymour observation $name must be the explicit boolean true" }
        $normalizedObservations[$name] = $true
    }

    $persistentNames = @('persistentState', 'persistentAbility')
    $localNames = @('localState', 'localAbility')
    [byte[]]$persistentBefore = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $persistentNames -Phase 'before'
    [byte[]]$localBefore = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $localNames -Phase 'before'
    if ($persistentBefore -contains 7 -or $localBefore -contains 7) { throw 'Seymour before snapshots already contain actor ID 7' }
    if ($persistentBefore -notcontains 0xFF -or $localBefore -notcontains 0xFF) { throw 'Seymour before snapshots do not contain a free 0xFF slot' }

    [byte[]]$persistentOn = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $persistentNames -Phase 'on'
    [byte[]]$persistentAfterSwitch = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $persistentNames -Phase 'afterSwitch'
    if (-not (Test-F8BytesEqual $persistentBefore $persistentOn) -or -not (Test-F8BytesEqual $persistentBefore $persistentAfterSwitch)) {
        throw 'Seymour persistent ON/after-Switch snapshots must remain byte-exactly clean after entry cleanup'
    }
    [byte[]]$localOn = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $localNames -Phase 'on'
    if (-not (Test-F8SingleFfToSeymourReplacement -Baseline $localBefore -Applied $localOn)) {
        throw 'Seymour ON local snapshots must replace exactly one 0xFF with actor ID 7'
    }
    [byte[]]$localAfterSwitch = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $localNames -Phase 'afterSwitch'
    if (-not (Test-F8ByteMultisetEqual $localOn $localAfterSwitch) -or (Test-F8BytesEqual $localOn $localAfterSwitch)) {
        throw 'Seymour after-Switch local snapshots must preserve membership in a different legitimate order'
    }
    if (@($localAfterSwitch | Where-Object { $_ -eq 7 }).Count -ne 1) { throw 'Seymour after-Switch snapshots must contain exactly one actor ID 7' }

    [byte[]]$persistentOff = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $persistentNames -Phase 'off'
    [byte[]]$localOff = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $localNames -Phase 'off'
    [byte[]]$persistentNext = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $persistentNames -Phase 'nextBattle'
    [byte[]]$localNext = Join-F8SeymourLeafBytes -DecodedLeaves $decodedLeaves -Names $localNames -Phase 'nextBattle'
    if ($persistentOff -contains 7 -or -not (Test-F8ByteMultisetEqual $persistentOff $persistentBefore)) {
        throw 'Seymour OFF persistent snapshots do not prove restored baseline membership without actor ID 7'
    }

    # WHY: official exit sync can leave battle-local buffers at their last owned image until the
    # next battle initialization. OFF records that state without claiming or forcing raw cleanup.
    $localOffClean = $localOff -notcontains 7 -and (Test-F8ByteMultisetEqual $localOff $localBefore)
    $localOffRetained = Test-F8SingleFfToSeymourReplacement -Baseline $localBefore -Applied $localOff
    if (-not $localOffClean -and -not $localOffRetained) {
        throw 'Seymour OFF local snapshots are neither clean baseline membership nor the owned current-battle retained membership'
    }
    $localOffDisposition = if ($localOffRetained) { 'restore-pending-current-battle-retained' } else { 'clean' }

    foreach ($phaseRecord in @(
        [pscustomobject]@{ Name = 'next-battle persistent'; Value = $persistentNext; Baseline = $persistentBefore },
        [pscustomobject]@{ Name = 'next-battle local'; Value = $localNext; Baseline = $localBefore }
    )) {
        if ([byte[]]$phaseRecord.Value -contains 7 -or -not (Test-F8ByteMultisetEqual ([byte[]]$phaseRecord.Value) ([byte[]]$phaseRecord.Baseline))) {
            throw "Seymour $($phaseRecord.Name) snapshots do not prove clean baseline membership without reintroduction"
        }
    }

    return [pscustomobject][ordered]@{
        Valid = $true
        Schema = $script:SeymourMemoryEvidenceSchema
        Executable = [ordered]@{ Sha256 = $script:SupportedExecutableSha256; ModuleBase = [string]$root.executable.moduleBase }
        CaptureSequence = @($script:SeymourCaptureSequence)
        Leaves = $normalizedLeaves.ToArray()
        Observations = $normalizedObservations
        Lifecycle = [ordered]@{
            persistentOff = 'restored'
            localOff = $localOffDisposition
            nextBattle = 'clean-no-reintroduction'
        }
        Source = [ordered]@{ Path = $resolved; Length = [long]$bytes.Length; Sha256 = Get-F8Sha256Hex -Bytes $bytes }
        Boundary = 'Validated raw-memory observations for this one manual case; not a signed trace, RT2 promotion, Sphere Grid support, or Production proof.'
    }
}

function Write-F8JsonArtifact {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 16
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    Write-F8BytesCreateNew -Path $Path -Bytes $utf8.GetBytes($json)
}

function Get-F8FileIntegrityRecord {
    param([Parameter(Mandatory)][string]$Path)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not [System.IO.File]::Exists($resolved)) { throw "integrity artifact is missing: $resolved" }
    $bytes = [System.IO.File]::ReadAllBytes($resolved)
    return [ordered]@{
        path = $resolved
        length = [long]$bytes.Length
        sha256 = Get-F8Sha256Hex -Bytes $bytes
    }
}

function Assert-F8FileIntegrityRecord {
    param(
        [Parameter(Mandatory)]$Record,
        [Parameter(Mandatory)][string]$ExpectedPath,
        [Parameter(Mandatory)][string]$Context
    )
    $recordedPath = [System.IO.Path]::GetFullPath([string](Get-F8RequiredValue -Object $Record -Key 'path' -Context $Context))
    $resolvedExpectedPath = [System.IO.Path]::GetFullPath($ExpectedPath)
    if (-not $recordedPath.Equals($resolvedExpectedPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context path mismatch"
    }
    $recordedLength = [long](Get-F8RequiredValue -Object $Record -Key 'length' -Context $Context)
    $recordedHash = ([string](Get-F8RequiredValue -Object $Record -Key 'sha256' -Context $Context)).ToUpperInvariant()
    if ($recordedLength -lt 0 -or $recordedHash -notmatch '^[0-9A-F]{64}$') {
        throw "$Context integrity record is invalid"
    }
    if (-not [System.IO.File]::Exists($resolvedExpectedPath)) { throw "$Context artifact is missing: $resolvedExpectedPath" }
    $bytes = [System.IO.File]::ReadAllBytes($resolvedExpectedPath)
    $actualHash = Get-F8Sha256Hex -Bytes $bytes
    if ($bytes.Length -ne $recordedLength -or $actualHash -cne $recordedHash) {
        throw "$Context artifact length/SHA-256 integrity check failed"
    }
    return $true
}

function Assert-F8PinnedPreflightIdentity {
    param(
        [Parameter(Mandatory)]$ManifestRecord,
        [Parameter(Mandatory)]$SidecarRecord,
        [Parameter(Mandatory)][string]$PreflightManifestPath
    )
    $manifestPath = [System.IO.Path]::GetFullPath($PreflightManifestPath)
    if ((Split-Path -Leaf $manifestPath) -cne 'manifest.json') {
        throw 'pinned Preflight manifest path must use the exact manifest.json leaf'
    }
    $sidecarPath = Join-Path (Split-Path -Parent $manifestPath) 'manifest.sha256'
    $recordsToValidate = @(
        [pscustomobject]@{ Record = $ManifestRecord; Path = $manifestPath; Context = 'pinned Preflight manifest' },
        [pscustomobject]@{ Record = $SidecarRecord; Path = $sidecarPath; Context = 'pinned Preflight manifest sidecar' }
    )
    foreach ($entry in $recordsToValidate) {
        try {
            [void](Assert-F8FileIntegrityRecord -Record $entry.Record -ExpectedPath $entry.Path -Context $entry.Context)
        } catch {
            throw "$($entry.Context) identity changed after initial Verify validation: $($_.Exception.Message)"
        }
    }
    return $true
}

function Read-F8VerifyIntegrityManifest {
    param(
        [Parameter(Mandatory)][string]$VerifyEvidenceDirectory,
        [Parameter(Mandatory)][string]$PreflightManifestPath,
        [Parameter(Mandatory)][string]$ExpectedCase,
        [Parameter(Mandatory)][string]$ExpectedSessionId
    )
    $directory = [System.IO.Path]::GetFullPath($VerifyEvidenceDirectory).TrimEnd('\')
    if (-not [System.IO.Directory]::Exists($directory)) { throw "Verify evidence directory is missing: $directory" }
    $manifestPath = Join-Path $directory 'verify-manifest.json'
    $sidecarPath = Join-Path $directory 'verify-manifest.sha256'
    if (-not [System.IO.File]::Exists($manifestPath) -or -not [System.IO.File]::Exists($sidecarPath)) {
        throw 'final Verify manifest or sidecar is missing'
    }
    $manifestBytes = [System.IO.File]::ReadAllBytes($manifestPath)
    $recordedManifestHash = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($sidecarPath))
    if ($recordedManifestHash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'final Verify manifest sidecar is invalid' }
    $actualManifestHash = Get-F8Sha256Hex -Bytes $manifestBytes
    if ($actualManifestHash -cne $recordedManifestHash.ToUpperInvariant()) {
        throw 'final Verify manifest SHA-256 integrity check failed'
    }
    try {
        $manifest = [System.Text.Encoding]::UTF8.GetString($manifestBytes) | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "final Verify manifest JSON parse failed: $($_.Exception.Message)"
    }
    if ([string](Get-F8RequiredValue -Object $manifest -Key 'schema' -Context 'final Verify manifest') -cne $script:VerifyManifestSchema) {
        throw 'unsupported final Verify manifest schema'
    }
    $verifySessionId = [string](Get-F8RequiredValue -Object $manifest -Key 'verifySessionId' -Context 'final Verify manifest')
    if ($verifySessionId -cnotmatch '^[0-9a-f]{32}$') { throw 'final Verify manifest session id is invalid' }
    $verifyLeafPattern = '^\d{8}T\d{9}Z_verify_' + [regex]::Escape($verifySessionId) + '$'
    if ((Split-Path -Leaf $directory) -cnotmatch $verifyLeafPattern) {
        throw 'final Verify evidence directory leaf/session mismatch'
    }

    $preflight = Get-F8RequiredValue -Object $manifest -Key 'preflight' -Context 'final Verify manifest'
    if (@($preflight.PSObject.Properties).Count -ne 4) {
        throw 'final Verify manifest preflight record set must be exactly case/sessionId/manifest/sidecar'
    }
    if ([string](Get-F8RequiredValue -Object $preflight -Key 'case' -Context 'final Verify preflight link') -cne $ExpectedCase -or
        [string](Get-F8RequiredValue -Object $preflight -Key 'sessionId' -Context 'final Verify preflight link') -cne $ExpectedSessionId) {
        throw 'final Verify manifest preflight case/session link mismatch'
    }
    [void](Assert-F8PinnedPreflightIdentity `
        -ManifestRecord (Get-F8RequiredValue -Object $preflight -Key 'manifest' -Context 'final Verify preflight link') `
        -SidecarRecord (Get-F8RequiredValue -Object $preflight -Key 'sidecar' -Context 'final Verify preflight link') `
        -PreflightManifestPath $PreflightManifestPath)

    $artifacts = Get-F8RequiredValue -Object $manifest -Key 'artifacts' -Context 'final Verify manifest'
    $artifactProperties = @($artifacts.PSObject.Properties)
    $expectedArtifacts = [ordered]@{
        logSlice = 'log-slice.bin'
        logSliceSha256 = 'log-slice.sha256'
        parsedLogVerdict = 'parsed-log-verdict.json'
        beforeAfterHashes = 'before-after-hashes.json'
        restorationVerdict = 'restoration-verdict.json'
    }
    if ($ExpectedCase -ceq 'seymour_battle_roster') {
        $expectedArtifacts['seymourMemoryEvidence'] = 'seymour-memory-evidence.json'
    }
    if ($artifactProperties.Count -ne $expectedArtifacts.Count) {
        throw 'final Verify manifest artifact set is incomplete or ambiguous'
    }
    foreach ($key in $expectedArtifacts.Keys) {
        [void](Assert-F8FileIntegrityRecord `
            -Record (Get-F8RequiredValue -Object $artifacts -Key $key -Context 'final Verify artifacts') `
            -ExpectedPath (Join-Path $directory $expectedArtifacts[$key]) `
            -Context "final Verify artifact $key")
    }
    return $manifest
}

function Write-F8VerifyIntegrityManifest {
    param(
        [Parameter(Mandatory)][string]$VerifyEvidenceDirectory,
        [Parameter(Mandatory)][string]$VerifySessionId,
        [Parameter(Mandatory)][DateTimeOffset]$VerifiedAt,
        [Parameter(Mandatory)][string]$PreflightCase,
        [Parameter(Mandatory)][string]$PreflightSessionId,
        [Parameter(Mandatory)][string]$PreflightManifestPath,
        [Parameter(Mandatory)]$PreflightManifestRecord,
        [Parameter(Mandatory)]$PreflightSidecarRecord,
        [scriptblock]$BeforeFinalEvidenceProvider
    )
    $directory = [System.IO.Path]::GetFullPath($VerifyEvidenceDirectory).TrimEnd('\')
    $expectedArtifacts = [ordered]@{
        logSlice = 'log-slice.bin'
        logSliceSha256 = 'log-slice.sha256'
        parsedLogVerdict = 'parsed-log-verdict.json'
        beforeAfterHashes = 'before-after-hashes.json'
        restorationVerdict = 'restoration-verdict.json'
    }
    if ($PreflightCase -ceq 'seymour_battle_roster') {
        $expectedArtifacts['seymourMemoryEvidence'] = 'seymour-memory-evidence.json'
    }
    $artifactRecords = [ordered]@{}
    foreach ($key in $expectedArtifacts.Keys) {
        $artifactRecords[$key] = Get-F8FileIntegrityRecord -Path (Join-Path $directory $expectedArtifacts[$key])
    }
    $preflightSidecarPath = Join-Path (Split-Path -Parent ([System.IO.Path]::GetFullPath($PreflightManifestPath))) 'manifest.sha256'
    if ($null -ne $BeforeFinalEvidenceProvider) {
        try {
            $providerPassed = [bool](& $BeforeFinalEvidenceProvider $PreflightManifestPath $preflightSidecarPath $directory)
        } catch {
            throw "before-final-evidence provider failed: $($_.Exception.Message)"
        }
        if (-not $providerPassed) { throw 'before-final-evidence provider failed closed' }
    }
    # WHY: these records were pinned immediately after the initial manifest read. Rechecking both
    # now closes in-process drift before any final manifest or sidecar can claim evidence success.
    [void](Assert-F8PinnedPreflightIdentity `
        -ManifestRecord $PreflightManifestRecord `
        -SidecarRecord $PreflightSidecarRecord `
        -PreflightManifestPath $PreflightManifestPath)
    $manifest = [ordered]@{
        schema = $script:VerifyManifestSchema
        verifySessionId = $VerifySessionId
        verifiedUtc = $VerifiedAt.UtcDateTime.ToString('o')
        preflight = [ordered]@{
            case = $PreflightCase
            sessionId = $PreflightSessionId
            manifest = $PreflightManifestRecord
            sidecar = $PreflightSidecarRecord
        }
        artifacts = $artifactRecords
        boundary = 'SHA-256 integrity evidence only; this sidecar is not a cryptographic signature or Production proof.'
    }
    $json = ConvertTo-Json -InputObject $manifest -Depth 20 -Compress
    $bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($json)
    $hash = Get-F8Sha256Hex -Bytes $bytes
    $manifestPath = Join-Path $directory 'verify-manifest.json'
    $sidecarPath = Join-Path $directory 'verify-manifest.sha256'
    # WHY: all covered artifacts must be durable before the manifest freezes their identities;
    # the sidecar is written last, then every record is read back before success is reported.
    Write-F8BytesCreateNew -Path $manifestPath -Bytes $bytes
    Write-F8BytesCreateNew -Path $sidecarPath -Bytes ([System.Text.Encoding]::ASCII.GetBytes($hash))
    [void](Read-F8VerifyIntegrityManifest `
        -VerifyEvidenceDirectory $directory `
        -PreflightManifestPath $PreflightManifestPath `
        -ExpectedCase $PreflightCase `
        -ExpectedSessionId $PreflightSessionId)
    return [pscustomobject]@{
        ManifestPath = [System.IO.Path]::GetFullPath($manifestPath)
        HashPath = [System.IO.Path]::GetFullPath($sidecarPath)
        Sha256 = $hash
        ReadbackValid = $true
    }
}

function Assert-F8ManifestPathIdentity {
    param([Parameter(Mandatory)]$ManifestPaths, [Parameter(Mandatory)]$Paths)
    $mapping = @{
        builtDll = 'BuiltDll'
        executable = 'Exe'
        installedDll = 'InstalledDll'
        ini = 'Ini'
    }
    foreach ($manifestKey in $mapping.Keys) {
        $record = Get-F8RequiredValue -Object $ManifestPaths -Key $manifestKey -Context 'manifest paths'
        $recordedPath = [System.IO.Path]::GetFullPath([string](Get-F8RequiredValue -Object $record -Key 'path' -Context "manifest paths.$manifestKey"))
        $actualPath = [System.IO.Path]::GetFullPath([string](Get-F8RequiredValue -Object $Paths -Key $mapping[$manifestKey] -Context 'path set'))
        if (-not $recordedPath.Equals($actualPath, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "manifest path mismatch for $manifestKey"
        }
    }
}

function Assert-F8UnchangedHash {
    param($ManifestRecord, $CurrentEvidence, [string]$Label)
    $recorded = ([string](Get-F8RequiredValue -Object $ManifestRecord -Key 'sha256' -Context $Label)).ToUpperInvariant()
    if ($recorded -cne ([string]$CurrentEvidence.Sha256).ToUpperInvariant()) {
        throw "$Label SHA-256 changed since Preflight"
    }
}

function Invoke-F8Rt2Protocol {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Case,
        [Parameter(Mandatory)][ValidateSet('Preflight', 'Verify')][string]$Phase,
        [Parameter(Mandatory)][switch]$DisposableSaveConfirmed,
        [Parameter(Mandatory)][switch]$EditorClosedConfirmed,
        [switch]$ObservedApplied,
        [switch]$ObservedRestored,
        [switch]$RestoreConfigSnapshot,
        [string]$SeymourEvidencePath,
        [string]$EvidenceDirectory,
        [string]$EvidenceRoot,
        [Parameter(Mandatory)][string]$ScriptRoot,
        $Paths,
        $Providers = @{}
    )
    if (-not $DisposableSaveConfirmed.IsPresent) {
        throw 'DisposableSaveConfirmed attestation must be explicitly true'
    }
    if (-not $EditorClosedConfirmed.IsPresent) {
        throw 'EditorClosedConfirmed attestation must be explicitly true'
    }
    if ($Phase -ceq 'Preflight' -and
        ($ObservedApplied.IsPresent -or $ObservedRestored.IsPresent -or $RestoreConfigSnapshot.IsPresent -or
         -not [string]::IsNullOrWhiteSpace($SeymourEvidencePath))) {
        throw 'Preflight rejects Verify-only ObservedApplied, ObservedRestored, RestoreConfigSnapshot, and SeymourEvidencePath inputs'
    }
    if ($Phase -ceq 'Verify' -and (-not $ObservedApplied.IsPresent -or -not $ObservedRestored.IsPresent)) {
        throw 'Verify requires both ObservedApplied and ObservedRestored attestations'
    }
    if ($Phase -ceq 'Verify' -and $Case -ceq 'seymour_battle_roster' -and [string]::IsNullOrWhiteSpace($SeymourEvidencePath)) {
        throw 'SeymourEvidencePath raw-memory evidence is required for Seymour Verify'
    }
    if ($Phase -ceq 'Verify' -and $Case -cne 'seymour_battle_roster' -and -not [string]::IsNullOrWhiteSpace($SeymourEvidencePath)) {
        throw 'SeymourEvidencePath is accepted only for the seymour_battle_roster case'
    }

    $spec = Get-F8CaseSpec -Case $Case
    $seymourMemoryEvidence = $null
    if ($Phase -ceq 'Verify' -and $Case -ceq 'seymour_battle_roster') {
        # Generic UI attestations cannot replace the independent five-phase raw-memory record.
        $seymourMemoryEvidence = Read-F8SeymourMemoryEvidence -Path $SeymourEvidencePath
    }
    if ($null -eq $Paths) { $Paths = Get-F8DefaultPathSet }
    # WHY: both phases reconstruct the same bounded default. A custom Preflight root can
    # therefore be consumed only when the operator repeats it explicitly on Verify.
    if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
        $EvidenceRoot = Get-F8DefaultEvidenceRoot -ScriptRoot $ScriptRoot
    }
    $EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')

    $processProvider = Get-F8ProviderValue -Providers $Providers -Name 'ProcessProvider'
    $hashProvider = Get-F8ProviderValue -Providers $Providers -Name 'HashProvider'
    $flagProvider = Get-F8ProviderValue -Providers $Providers -Name 'FlagExistsProvider'
    $environment = Get-F8ProviderValue -Providers $Providers -Name 'Environment' -DefaultValue (Get-F8ProcessEnvironment)
    $clockProvider = Get-F8ProviderValue -Providers $Providers -Name 'ClockProvider' -DefaultValue { [DateTimeOffset]::Now }
    $commitProvider = Get-F8ProviderValue -Providers $Providers -Name 'CommitProvider'
    $moveProvider = Get-F8ProviderValue -Providers $Providers -Name 'MoveProvider'
    $beforeFinalEvidenceProvider = Get-F8ProviderValue -Providers $Providers -Name 'BeforeFinalEvidenceProvider'
    $gameRoot = [string](Get-F8RequiredValue -Object $Paths -Key 'GameRoot' -Context 'path set')
    $logPath = [string](Get-F8RequiredValue -Object $Paths -Key 'Log' -Context 'path set')
    $counterPath = [string](Get-F8RequiredValue -Object $Paths -Key 'Counter' -Context 'path set')

    [void](Assert-F8ProcessesClosed -ProcessProvider $processProvider -CheckName "$Phase process gate")

    if ($Phase -ceq 'Preflight') {
        if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
            throw 'Preflight creates its own unique evidence directory; EvidenceDirectory is Verify-only'
        }
        $leafEvidence = Get-F8ExactLeafEvidence -Paths $Paths -HashProvider $hashProvider
        $ini = ConvertFrom-F8IniBytes -Bytes $leafEvidence.Ini.Bytes
        $scalarConfigurations = @(Resolve-F8AllRewardScalarConfigurations -Ini $ini)
        if (-not (Test-F8DashboardEnabled -Ini $ini)) { throw 'dashboard.enabled must resolve to true for Preflight' }
        $resolutions = @(Resolve-F8AllCases -Ini $ini -Environment $environment -GameRoot $gameRoot -FlagExistsProvider $flagProvider)
        $on = @($resolutions | Where-Object { $_.Value })
        if ($on.Count -ne 0) {
            throw "Preflight requires all 14 targets effective OFF; ON: $(@($on.Canonical) -join ', ')"
        }
        $selectedResolution = @($resolutions | Where-Object { $_.Case -ceq $Case })[0]
        if (Test-F8HigherAuthorityOnBlocker -Spec $spec -Resolution $selectedResolution) {
            throw "selected Compose case has a higher-authority future ON blocker: $($selectedResolution.Source)"
        }
        $logBoundary = Get-F8LogBoundary -LogPath $logPath -CounterPath $counterPath
        try { $now = [DateTimeOffset](& $clockProvider) } catch { throw "clock provider failed: $($_.Exception.Message)" }
        $sessionId = [Guid]::NewGuid().ToString('N')
        $createdEvidenceDirectory = New-F8EvidenceDirectory -EvidenceRoot $EvidenceRoot -Case $Case -Now $now -SessionId $sessionId

        if ($null -eq $commitProvider) {
            $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot '..\..\..'))
            $commitProvider = {
                $identity = & git -C $repoRoot rev-parse HEAD
                if ($LASTEXITCODE -ne 0 -or -not $identity) { throw 'git rev-parse HEAD failed' }
                return [string]$identity
            }.GetNewClosure()
        }
        try { $commitIdentity = [string](& $commitProvider) } catch {
            throw "commit identity provider failed: $($_.Exception.Message)"
        }
        if ($commitIdentity -notmatch '^[0-9A-Fa-f]{40}$') { throw "invalid commit identity: $commitIdentity" }

        # This is deliberately the last gate before the byte snapshot and manifest persistence.
        [void](Assert-F8ProcessesClosed -ProcessProvider $processProvider -CheckName 'Preflight snapshot immediate process recheck')
        $snapshot = New-F8ConfigSnapshot -IniBytes $leafEvidence.Ini.Bytes -IniPath $leafEvidence.Ini.Path -EvidenceDirectory $createdEvidenceDirectory

        $manifest = [ordered]@{
            schema = $script:ManifestSchema
            case = $Case
            sessionId = $sessionId
            canonical = $spec.Canonical
            applyMode = $spec.ApplyMode
            preflightUtc = $now.UtcDateTime.ToString('o')
            preflightLocal = $now.ToString('o')
            paths = [ordered]@{
                builtDll = ConvertTo-F8PathManifestRecord $leafEvidence.BuiltDll
                executable = ConvertTo-F8PathManifestRecord $leafEvidence.Exe
                installedDll = ConvertTo-F8PathManifestRecord $leafEvidence.InstalledDll
                ini = ConvertTo-F8PathManifestRecord $leafEvidence.Ini
            }
            snapshot = [ordered]@{
                path = $snapshot.Path
                sha256 = $snapshot.Sha256
                length = $snapshot.Length
            }
            resolutions = $resolutions
            scalarConfigurations = $scalarConfigurations
            manualSteps = @(Get-F8ManualSteps -Spec $spec)
            runtimeEvidence = [ordered]@{
                required = -not [string]::IsNullOrEmpty([string]$spec.RuntimeEvidenceSchema)
                schema = [string]$spec.RuntimeEvidenceSchema
                captureSequence = $(if ([string]::IsNullOrEmpty([string]$spec.RuntimeEvidenceSchema)) { @() } else { @($script:SeymourCaptureSequence) })
                leaves = @($spec.RuntimeLeaves | ForEach-Object {
                    [ordered]@{ name = [string]$_.Name; rva = [string]$_.Rva; size = [int]$_.Size }
                })
            }
            log = [ordered]@{
                path = $logBoundary.LogPath
                counterPath = $logBoundary.CounterPath
                startLength = $logBoundary.StartLength
                prefixSha256 = $logBoundary.PrefixSha256
                openCounter = $logBoundary.OpenCounter
                counterExisted = $logBoundary.CounterExisted
            }
            attestations = [ordered]@{
                disposableSaveConfirmed = $true
                editorClosedConfirmed = $true
            }
            tool = [ordered]@{
                script = [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot 'run_f8_rt2.ps1'))
                commit = $commitIdentity.ToLowerInvariant()
            }
            evidence = [ordered]@{
                root = $EvidenceRoot
                directory = $createdEvidenceDirectory
                boundary = 'Preflight is RT0/manual-protocol evidence only; it does not launch, stop, deploy, or prove RT2/Production.'
            }
        }
        $manifestWrite = Write-F8IntegrityManifest -Manifest $manifest -EvidenceDirectory $createdEvidenceDirectory
        return [pscustomobject]@{
            Phase = 'Preflight'
            Case = $Case
            SessionId = $sessionId
            EvidenceDirectory = $createdEvidenceDirectory
            SnapshotPath = $snapshot.Path
            ManifestPath = $manifestWrite.ManifestPath
            Resolutions = $resolutions
            ScalarConfigurations = $scalarConfigurations
            ManualSteps = $manifest.manualSteps
        }
    }

    $resolvedEvidenceDirectory = Resolve-F8VerifyEvidenceDirectory -EvidenceDirectory $EvidenceDirectory -EvidenceRoot $EvidenceRoot -ExpectedCase $Case
    $leaf = Split-Path -Leaf $resolvedEvidenceDirectory
    $leafPattern = '^\d{8}T\d{9}Z_' + [regex]::Escape($Case) + '_([0-9a-f]{32})$'
    $leafMatch = [regex]::Match($leaf, $leafPattern, [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)
    if (-not $leafMatch.Success) { throw 'EvidenceDirectory does not carry an exact lowercase session id' }
    $expectedSessionId = $leafMatch.Groups[1].Value
    $manifestEnvelope = Read-F8IntegrityManifestEnvelope -EvidenceDirectory $resolvedEvidenceDirectory -ExpectedCase $Case -ExpectedSessionId $expectedSessionId
    $manifest = $manifestEnvelope.Manifest
    $preflightManifestPin = $manifestEnvelope.ManifestRecord
    $preflightManifestSidecarPin = $manifestEnvelope.SidecarRecord
    $preflightManifestPath = [string]$preflightManifestPin.path
    if ([string](Get-F8RequiredValue $manifest 'canonical' 'manifest') -cne $spec.Canonical -or
        [string](Get-F8RequiredValue $manifest 'applyMode' 'manifest') -cne $spec.ApplyMode) {
        throw 'manifest case table identity mismatch'
    }
    $manifestEvidence = Get-F8RequiredValue $manifest 'evidence' 'manifest'
    $recordedEvidenceRoot = [System.IO.Path]::GetFullPath([string](Get-F8RequiredValue $manifestEvidence 'root' 'manifest evidence')).TrimEnd('\')
    if (-not $recordedEvidenceRoot.Equals($EvidenceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'manifest recorded EvidenceRoot does not match the resolved Verify EvidenceRoot'
    }
    $recordedEvidenceDirectory = [System.IO.Path]::GetFullPath([string](Get-F8RequiredValue $manifestEvidence 'directory' 'manifest evidence'))
    if (-not $recordedEvidenceDirectory.Equals($resolvedEvidenceDirectory, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'manifest evidence directory/session mismatch'
    }
    $manifestPaths = Get-F8RequiredValue $manifest 'paths' 'manifest'
    Assert-F8ManifestPathIdentity -ManifestPaths $manifestPaths -Paths $Paths

    $beforeEvidence = Get-F8ExactLeafEvidence -Paths $Paths -HashProvider $hashProvider
    $manifestBuilt = Get-F8RequiredValue $manifestPaths 'builtDll' 'manifest paths'
    $manifestExe = Get-F8RequiredValue $manifestPaths 'executable' 'manifest paths'
    $manifestInstalled = Get-F8RequiredValue $manifestPaths 'installedDll' 'manifest paths'
    $manifestIni = Get-F8RequiredValue $manifestPaths 'ini' 'manifest paths'
    Assert-F8UnchangedHash $manifestBuilt $beforeEvidence.BuiltDll 'built DLL'
    Assert-F8UnchangedHash $manifestInstalled $beforeEvidence.InstalledDll 'installed DLL'
    Assert-F8UnchangedHash $manifestExe $beforeEvidence.Exe 'FFX executable'

    $manifestLog = Get-F8RequiredValue $manifest 'log' 'manifest'
    $recordedLogPath = [string](Get-F8RequiredValue $manifestLog 'path' 'manifest log')
    $recordedCounterPath = [string](Get-F8RequiredValue $manifestLog 'counterPath' 'manifest log')
    if (-not [System.IO.Path]::GetFullPath($recordedLogPath).Equals([System.IO.Path]::GetFullPath($logPath), [System.StringComparison]::OrdinalIgnoreCase) -or
        -not [System.IO.Path]::GetFullPath($recordedCounterPath).Equals([System.IO.Path]::GetFullPath($counterPath), [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'manifest log path mismatch'
    }
    $slice = Read-F8LogSlice `
        -LogPath $logPath `
        -StartLength ([long](Get-F8RequiredValue $manifestLog 'startLength' 'manifest log')) `
        -PrefixSha256 ([string](Get-F8RequiredValue $manifestLog 'prefixSha256' 'manifest log'))
    $currentCounter = Read-F8OpenCounter -CounterPath $counterPath
    [void](Test-F8LogCounterTransition `
        -StartCounter ([int](Get-F8RequiredValue $manifestLog 'openCounter' 'manifest log')) `
        -CurrentCounter $currentCounter.Value `
        -StartCounterExisted:([bool](Get-F8RequiredValue $manifestLog 'counterExisted' 'manifest log')))
    $logVerdict = Test-F8LogEvidence -CaseSpec $spec -SliceBytes $slice.Bytes -ObservedApplied:$ObservedApplied.IsPresent -ObservedRestored:$ObservedRestored.IsPresent

    $snapshotRecord = Get-F8RequiredValue $manifest 'snapshot' 'manifest'
    $snapshotPath = [string](Get-F8RequiredValue $snapshotRecord 'path' 'manifest snapshot')
    if (-not $RestoreConfigSnapshot.IsPresent) {
        throw "RestoreConfigSnapshot is required before Verify can restore the byte-exact INI; snapshot path: $snapshotPath"
    }

    # Keep the early restore gate, then repeat after the flushed temp exists and immediately before
    # MoveFileExW. Both checks use the same fail-closed provider; neither trusts the UI checkbox.
    [void](Assert-F8ProcessesClosed -ProcessProvider $processProvider -CheckName 'Verify restore immediate process recheck')
    $beforeReplaceProcessGate = {
        param($temporaryPath, $destinationPath)
        return Assert-F8ProcessesClosed -ProcessProvider $processProvider -CheckName 'Verify atomic replace immediate process recheck'
    }.GetNewClosure()
    $restoration = Restore-F8ConfigSnapshot `
        -SnapshotPath $snapshotPath `
        -DestinationPath $beforeEvidence.Ini.Path `
        -ExpectedSha256 ([string](Get-F8RequiredValue $snapshotRecord 'sha256' 'manifest snapshot')) `
        -ExpectedLength ([long](Get-F8RequiredValue $snapshotRecord 'length' 'manifest snapshot')) `
        -BeforeReplaceProvider $beforeReplaceProcessGate `
        -MoveProvider $moveProvider

    $afterEvidence = Get-F8ExactLeafEvidence -Paths $Paths -HashProvider $hashProvider
    Assert-F8UnchangedHash $manifestBuilt $afterEvidence.BuiltDll 'built DLL after restore'
    Assert-F8UnchangedHash $manifestInstalled $afterEvidence.InstalledDll 'installed DLL after restore'
    Assert-F8UnchangedHash $manifestExe $afterEvidence.Exe 'FFX executable after restore'
    Assert-F8UnchangedHash $manifestIni $afterEvidence.Ini 'INI after restore'
    $snapshotBytes = [System.IO.File]::ReadAllBytes($snapshotPath)
    $snapshotBytesValid = Test-F8BytesEqual -Left $snapshotBytes -Right $afterEvidence.Ini.Bytes
    if (-not $snapshotBytesValid) { throw 'restored INI bytes differ from the Preflight snapshot' }
    $restoredIni = ConvertFrom-F8IniBytes -Bytes $afterEvidence.Ini.Bytes
    $afterResolutions = @(Resolve-F8AllCases -Ini $restoredIni -Environment $environment -GameRoot $gameRoot -FlagExistsProvider $flagProvider)
    $allFourteenOff = @($afterResolutions | Where-Object { $_.Value }).Count -eq 0 -and $afterResolutions.Count -eq 14
    if (-not $allFourteenOff) { throw 'restored resolver state is not all 14 effective OFF' }

    $checks = [ordered]@{
        ObservedApplied = $ObservedApplied.IsPresent
        ObservedRestored = $ObservedRestored.IsPresent
        LogValid = [bool]$logVerdict.Valid
        LogPrefixValid = $true
        SnapshotBytesValid = $snapshotBytesValid
        BuiltDllHashUnchanged = $afterEvidence.BuiltDll.Sha256 -ceq [string](Get-F8RequiredValue $manifestBuilt 'sha256' 'manifest built DLL')
        InstalledDllHashUnchanged = $afterEvidence.InstalledDll.Sha256 -ceq [string](Get-F8RequiredValue $manifestInstalled 'sha256' 'manifest installed DLL')
        ExecutableHashUnchanged = $afterEvidence.Exe.Sha256 -ceq [string](Get-F8RequiredValue $manifestExe 'sha256' 'manifest executable')
        IniHashRestored = $afterEvidence.Ini.Sha256 -ceq [string](Get-F8RequiredValue $manifestIni 'sha256' 'manifest INI')
        AllFourteenOff = $allFourteenOff
        RestorationVerified = [bool]$restoration.Restored
    }
    $additionalRequired = @()
    if ($Case -ceq 'seymour_battle_roster') {
        $checks['SeymourMemoryEvidenceValid'] = [bool]$seymourMemoryEvidence.Valid
        $additionalRequired = @('SeymourMemoryEvidenceValid')
    }
    $finalVerdict = Test-F8FinalVerdict -Checks $checks -AdditionalRequired $additionalRequired
    if (-not $finalVerdict) { throw 'final vanilla-restoration verdict is false' }

    $now = [DateTimeOffset](& $clockProvider)
    $verifySessionId = [Guid]::NewGuid().ToString('N')
    $verifyDirectory = New-F8EvidenceDirectory -EvidenceRoot $resolvedEvidenceDirectory -Case 'verify' -Now $now -SessionId $verifySessionId
    Write-F8BytesCreateNew -Path (Join-Path $verifyDirectory 'log-slice.bin') -Bytes $slice.Bytes
    Write-F8BytesCreateNew -Path (Join-Path $verifyDirectory 'log-slice.sha256') -Bytes ([System.Text.Encoding]::ASCII.GetBytes($slice.Sha256))
    Write-F8JsonArtifact -Path (Join-Path $verifyDirectory 'parsed-log-verdict.json') -Value $logVerdict
    Write-F8JsonArtifact -Path (Join-Path $verifyDirectory 'before-after-hashes.json') -Value ([ordered]@{
        before = [ordered]@{
            builtDll = $beforeEvidence.BuiltDll.Sha256
            executable = $beforeEvidence.Exe.Sha256
            installedDll = $beforeEvidence.InstalledDll.Sha256
            ini = $beforeEvidence.Ini.Sha256
        }
        after = [ordered]@{
            builtDll = $afterEvidence.BuiltDll.Sha256
            executable = $afterEvidence.Exe.Sha256
            installedDll = $afterEvidence.InstalledDll.Sha256
            ini = $afterEvidence.Ini.Sha256
        }
    })
    Write-F8JsonArtifact -Path (Join-Path $verifyDirectory 'restoration-verdict.json') -Value ([ordered]@{
        case = $Case
        sessionId = $expectedSessionId
        verifiedUtc = $now.UtcDateTime.ToString('o')
        checks = $checks
        finalVerdict = $finalVerdict
        scope = 'Hashed DLL/EXE/INI and byte snapshot plus human disposable-save attestation; not whole-game or Production proof.'
    })
    if ($Case -ceq 'seymour_battle_roster') {
        Write-F8JsonArtifact -Path (Join-Path $verifyDirectory 'seymour-memory-evidence.json') -Value $seymourMemoryEvidence
    }
    $verifyManifestWrite = Write-F8VerifyIntegrityManifest `
        -VerifyEvidenceDirectory $verifyDirectory `
        -VerifySessionId $verifySessionId `
        -VerifiedAt $now `
        -PreflightCase $Case `
        -PreflightSessionId $expectedSessionId `
        -PreflightManifestPath $preflightManifestPath `
        -PreflightManifestRecord $preflightManifestPin `
        -PreflightSidecarRecord $preflightManifestSidecarPin `
        -BeforeFinalEvidenceProvider $beforeFinalEvidenceProvider
    return [pscustomobject]@{
        Phase = 'Verify'
        Case = $Case
        SessionId = $expectedSessionId
        EvidenceDirectory = $resolvedEvidenceDirectory
        VerifyEvidenceDirectory = $verifyDirectory
        SnapshotPath = $snapshotPath
        Resolutions = $afterResolutions
        FinalVerdict = $finalVerdict
        Restoration = $restoration
        VerifyManifestPath = $verifyManifestWrite.ManifestPath
        VerifyManifestHashPath = $verifyManifestWrite.HashPath
        FinalEvidenceReadbackValid = $verifyManifestWrite.ReadbackValid
    }
}

Export-ModuleMember -Function @(
    'Get-F8Rt2CaseTable',
    'Get-F8CaseSpec',
    'Get-F8ManualSteps',
    'ConvertFrom-F8BoolText',
    'ConvertFrom-F8IniBytes',
    'Resolve-F8StrictScalarConfiguration',
    'Resolve-F8AllRewardScalarConfigurations',
    'Resolve-F8BoolGate',
    'Resolve-F8AllCases',
    'Test-F8HigherAuthorityOnBlocker',
    'Assert-F8ProcessesClosed',
    'Assert-F8DoubleProcessGate',
    'Get-F8ExactLeafEvidence',
    'Test-F8DashboardEnabled',
    'New-F8ConfigSnapshot',
    'Restore-F8ConfigSnapshot',
    'Test-F8RestoreAuthorization',
    'Write-F8IntegrityManifest',
    'Read-F8IntegrityManifest',
    'Read-F8VerifyIntegrityManifest',
    'Resolve-F8VerifyEvidenceDirectory',
    'Get-F8LogBoundary',
    'Read-F8LogSlice',
    'Test-F8LogCounterTransition',
    'Test-F8LogEvidence',
    'Test-F8FinalVerdict',
    'Read-F8SeymourMemoryEvidence',
    'Invoke-F8Rt2Protocol'
)
