# f7_config_rt0.ps1 — RT0 offline do contrato do f7_inlive.json (F7 In-Live, Jarvis-HOOK).
# Valida: parse JSON, chaves esperadas, ranges dos campos, round-trip de valores-chave.
# Uso: powershell -File f7_config_rt0.ps1   (exit 0 = PASS)
$ErrorActionPreference = 'Stop'
$fail = 0
function Fail($msg) { Write-Host "FAIL: $msg"; $script:fail = 1 }

# ── 1. JSON de exemplo (contrato do SaveConfig do F7InLive.cpp) ──
$sample = @'
{
  "$schema": "./f7_inlive.schema.json",
  "version": 1,
  "diffByArea": false,
  "diff_enabled": true,
  "diff_hpMul": 2500,
  "diff_strMul": 1500,
  "diff_defMul": 1500,
  "diff_magMul": 1300,
  "diff_mdfMul": 1300,
  "diff_agiMul": 1400,
  "diff_accMul": 1000,
  "diff_evaMul": 1000,
  "diff_lckMul": 1000,
  "diff_overkillMul": 1000,
  "diff_autoStatus": 8388608,
  "diff_elemWeak": 0,
  "diff_elemResist": 16,
  "diff_elemAbsorb": 0,
  "diff_statusResist": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
  "areas": [],
  "music_lock": 16,
  "music_battle": -1,
  "music_randomizer": false,
  "music_fade": 90,
  "music_playlist": [16, 28, 29],
  "force_lastField": 2,
  "force_lastGroup": 0,
  "force_lastFormation": 0,
  "force_hasLast": true,
  "force_repeat": 3
}
'@

# ── 2. Parse + chaves esperadas ──
try { $cfg = $sample | ConvertFrom-Json } catch { Fail "parse JSON: $($_.Exception.Message)"; exit 1 }
$required = @('diff_enabled','diff_hpMul','diff_strMul','diff_defMul','diff_magMul','diff_mdfMul',
              'diff_agiMul','diff_accMul','diff_evaMul','diff_lckMul','diff_autoStatus',
              'diff_elemWeak','diff_elemResist','diff_elemAbsorb','diff_statusResist',
              'music_lock','music_battle','music_randomizer','music_fade','music_playlist',
              'force_lastField','force_lastGroup','force_hasLast','force_repeat','diffByArea')
foreach ($k in $required) { if ($null -eq $cfg.$k) { Fail "chave ausente: $k" } }
if ($cfg.diff_statusResist.Count -ne 25) { Fail "statusResist deve ter 25 entries (tem $($cfg.diff_statusResist.Count))" }

# ── 3. Ranges (clamps do F7InLive/F7_BASE_MAX) ──
$ranges = @{
  diff_hpMul  = @(100, 10000); diff_strMul = @(100, 5000); diff_defMul = @(100, 5000)
  diff_magMul = @(100, 5000);  diff_mdfMul = @(100, 5000); diff_agiMul = @(100, 5000)
  diff_accMul = @(100, 5000);  diff_evaMul = @(100, 5000); diff_lckMul = @(100, 5000)
  music_lock  = @(-1, 181);    music_battle = @(-1, 181);  music_fade = @(0, 600)
  force_repeat = @(1, 9)
}
foreach ($k in $ranges.Keys) {
  $v = [int]$cfg.$k; $lo = $ranges[$k][0]; $hi = $ranges[$k][1]
  if ($v -lt $lo -or $v -gt $hi) { Fail "$k = $v fora do range [$lo..$hi]" }
}
if (($cfg.diff_autoStatus -band 0xFE000000) -ne 0) { Fail "diff_autoStatus tem bits acima de 24 (mask 0x1FFFFFF)" }
if (($cfg.diff_elemWeak -band 0xE0) -ne 0 -or ($cfg.diff_elemResist -band 0xE0) -ne 0 -or ($cfg.diff_elemAbsorb -band 0xE0) -ne 0) { Fail "elem masks tem bits acima de 4" }

# ── 4. Round-trip (valores-chave preservados) ──
$rt = $cfg | ConvertTo-Json -Depth 6 | ConvertFrom-Json
foreach ($k in @('diff_hpMul','diff_autoStatus','music_lock','music_fade','force_repeat','force_lastField')) {
  if ("$($rt.$k)" -ne "$($cfg.$k)") { Fail "round-trip divergiu em $k" }
}

if ($fail -eq 0) { Write-Host "F7 CONFIG RT0: PASS (24 chaves, ranges, masks, round-trip)" ; exit 0 }
Write-Host "F7 CONFIG RT0: FAIL"; exit 1
