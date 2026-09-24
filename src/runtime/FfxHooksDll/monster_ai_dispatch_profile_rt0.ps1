param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath
)

# Read-only RT0 proof for the one supported FFX.exe Monster AI dispatcher profile.
$ErrorActionPreference = 'Stop'
$expectedHash = '78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED'
$expectedLength = 10675712L
$expectedTimestamp = [uint32]0x55D2F3CC
$expectedImageBase = [uint32]0x00400000
$expectedImageSize = [uint32]0x0237D000
$dispatcherRva = [uint32]0x003AC9E0
$expectedReturns = @([uint32]0x003A454E, [uint32]0x003A4A60, [uint32]0x003A4B8C)
$expectedHighLow = @([uint32]0x003AC9E8, [uint32]0x003AC9F6, [uint32]0x003ACA06, [uint32]0x003ACA1E)
$expectedSignature = [byte[]]@(
    0x55,0x8B,0xEC,0x83,0xEC,0x10,0x83,0x3D,0x58,0x6A,0x13,0x01,0xFF,0x0F,0x84,0x20,
    0x01,0x00,0x00,0x0F,0xB6,0x05,0x68,0x6A,0x13,0x01,0x8B,0x55,0x08,0x3B,0xD0,0x0F,
    0x85,0x0E,0x01,0x00,0x00,0xA0,0x6B,0x6A,0x13,0x01,0x3C,0x04,0x0F,0x83,0x01,0x01,
    0x00,0x00,0x0F,0xB6,0xC8,0x66,0x8B,0x45,0x0C,0xC1,0xE1,0x04,0x81,0xC1,0x70,0x6A,
    0x13,0x01
)

$resolved = (Resolve-Path -LiteralPath $ExecutablePath).Path
$item = Get-Item -LiteralPath $resolved
if ($item.Length -ne $expectedLength) {
    throw "Unsupported FFX.exe length: $($item.Length)"
}
$hash = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
if ($hash -ne $expectedHash) { throw "Unsupported FFX.exe SHA-256: $hash" }
$bytes = [System.IO.File]::ReadAllBytes($resolved)

function Read-U16([int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 2 -gt $bytes.Length) { throw "U16 out of range: $Offset" }
    return [BitConverter]::ToUInt16($bytes, $Offset)
}
function Read-U32([int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $bytes.Length) { throw "U32 out of range: $Offset" }
    return [BitConverter]::ToUInt32($bytes, $Offset)
}

$pe = Read-U32 0x3C
if ((Read-U32 $pe) -ne [uint32]0x00004550) { throw 'Invalid PE signature' }
if ((Read-U16 ($pe + 4)) -ne [uint16]0x014C) { throw 'Supported executable must be I386' }
if ((Read-U32 ($pe + 8)) -ne $expectedTimestamp) { throw 'Supported PE timestamp changed' }
$sectionCount = Read-U16 ($pe + 6)
$optionalSize = Read-U16 ($pe + 20)
$optional = $pe + 24
if ((Read-U16 $optional) -ne [uint16]0x010B) { throw 'Supported executable must be PE32' }
if ((Read-U32 ($optional + 28)) -ne $expectedImageBase) { throw 'Preferred image base changed' }
if ((Read-U32 ($optional + 56)) -ne $expectedImageSize) { throw 'SizeOfImage changed' }

$sections = @()
$sectionTable = $optional + $optionalSize
for ($index = 0; $index -lt $sectionCount; ++$index) {
    $offset = $sectionTable + 40 * $index
    $sections += [pscustomobject]@{
        Name = [Text.Encoding]::ASCII.GetString($bytes, $offset, 8).Trim([char]0)
        VirtualSize = Read-U32 ($offset + 8)
        VirtualAddress = Read-U32 ($offset + 12)
        RawSize = Read-U32 ($offset + 16)
        RawPointer = Read-U32 ($offset + 20)
        Characteristics = Read-U32 ($offset + 36)
    }
}

function Convert-RvaToRaw([uint32]$Rva) {
    foreach ($section in $sections) {
        $extent = [Math]::Max([uint64]$section.VirtualSize, [uint64]$section.RawSize)
        if ($Rva -ge $section.VirtualAddress -and
            [uint64]$Rva -lt ([uint64]$section.VirtualAddress + $extent)) {
            return [int]($section.RawPointer + ($Rva - $section.VirtualAddress))
        }
    }
    throw ('Unmapped RVA 0x{0:X8}' -f $Rva)
}

$dispatcherRaw = Convert-RvaToRaw $dispatcherRva
for ($index = 0; $index -lt $expectedSignature.Length; ++$index) {
    if ($bytes[$dispatcherRaw + $index] -ne $expectedSignature[$index]) {
        throw ('Dispatcher signature mismatch at +0x{0:X2}' -f $index)
    }
}

$signatureMatches = @()
$callReturns = @()
foreach ($section in $sections | Where-Object { ($_.Characteristics -band [uint32]0x20000000) -ne 0 }) {
    if ($section.RawSize -ge $expectedSignature.Length) {
        for ($offset = 0; $offset -le $section.RawSize - $expectedSignature.Length; ++$offset) {
            $matches = $true
            for ($byteIndex = 0; $byteIndex -lt $expectedSignature.Length; ++$byteIndex) {
                if ($bytes[$section.RawPointer + $offset + $byteIndex] -ne
                    $expectedSignature[$byteIndex]) {
                    $matches = $false
                    break
                }
            }
            if ($matches) { $signatureMatches += [uint32]($section.VirtualAddress + $offset) }
        }
    }
    if ($section.RawSize -ge 5) {
        for ($offset = 0; $offset -le $section.RawSize - 5; ++$offset) {
            $raw = [int]$section.RawPointer + $offset
            if ($bytes[$raw] -ne 0xE8) { continue }
            $displacement = [BitConverter]::ToInt32($bytes, $raw + 1)
            $callRva = [uint32]($section.VirtualAddress + $offset)
            if ([int64]$callRva + 5L + [int64]$displacement -eq $dispatcherRva) {
                $callReturns += [uint32]($callRva + 5)
            }
        }
    }
}
if ($signatureMatches.Count -ne 1 -or $signatureMatches[0] -ne $dispatcherRva) {
    throw "Dispatcher signature is not unique at the exact supported RVA"
}
$actualReturns = @($callReturns | Sort-Object)
$sortedExpectedReturns = @($expectedReturns | Sort-Object)
if ($actualReturns.Count -ne $sortedExpectedReturns.Count) {
    throw "Unexpected direct dispatcher caller count: $($actualReturns.Count)"
}
for ($index = 0; $index -lt $sortedExpectedReturns.Count; ++$index) {
    if ($actualReturns[$index] -ne $sortedExpectedReturns[$index]) {
        throw ('Unexpected dispatcher return RVA 0x{0:X8}' -f $actualReturns[$index])
    }
}

$relocationRva = Read-U32 ($optional + 96 + 5 * 8)
$relocationSize = Read-U32 ($optional + 96 + 5 * 8 + 4)
$relocationRaw = Convert-RvaToRaw $relocationRva
$relocationEnd = $relocationRaw + $relocationSize
$highLow = [System.Collections.Generic.HashSet[uint32]]::new()
$cursor = $relocationRaw
while ($cursor + 8 -le $relocationEnd) {
    $pageRva = Read-U32 $cursor
    $blockSize = Read-U32 ($cursor + 4)
    if ($blockSize -lt 8 -or $cursor + $blockSize -gt $relocationEnd) {
        throw 'Malformed PE base-relocation block'
    }
    for ($entry = $cursor + 8; $entry + 2 -le $cursor + $blockSize; $entry += 2) {
        $encoded = Read-U16 $entry
        if (($encoded -shr 12) -eq 3) {
            [void]$highLow.Add([uint32]($pageRva + ($encoded -band 0x0FFF)))
        }
    }
    $cursor += $blockSize
}
foreach ($rva in $expectedHighLow) {
    if (-not $highLow.Contains($rva)) {
        throw ('Missing dispatcher HIGHLOW relocation RVA 0x{0:X8}' -f $rva)
    }
}

Write-Host ('Monster AI dispatcher profile RT0: hash={0} signature_matches=1 callers={1} highlow={2}' -f `
    $hash, (($actualReturns | ForEach-Object { '0x{0:X8}' -f $_ }) -join ','),
    (($expectedHighLow | ForEach-Object { '0x{0:X8}' -f $_ }) -join ','))
Write-Host 'MONSTER AI DISPATCH PROFILE RT0: PASS'
