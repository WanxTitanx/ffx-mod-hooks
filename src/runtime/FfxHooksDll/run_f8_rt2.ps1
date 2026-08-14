# run_f8_rt2.ps1 - Onda 4: RT2 do F8 (Operacao Demonio, 2026-08-02, Jarvis-HOOK)
# Smoke no _isolated: F8 = dashboard, F7 = NativeMenu, sem disputa, cheats ok.
# REGRAS (gates transversais do F7F8_RECONCILIATION_PLAN):
#   1. FECHAR o FFXProjectEditor ANTES (softlock historico = editor aberto, nao hooks).
#   2. Save descartavel. 3. Hash dos bins antes/depois. 4. UnX legado NAO volta ao deploy.
param(
    [int]$BootSeconds = 90,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
$game = 'D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster'
$logPath = Join-Path $env:TEMP 'ffx-hooks.log'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir = "work/f8_recon/fase4_rt2_$stamp"

Write-Host "=== F8 RT2 ($stamp) ==="

# 1. Gates
$editor = Get-Process -Name 'FFXProjectEditor' -ErrorAction SilentlyContinue
if ($editor) {
    Write-Host "[FAIL] Editor ABERTO (PID $($editor.Id)) - feche antes do RT2 (gate transversal)."
    exit 1
}
$ffx = Get-Process -Name 'FFX' -ErrorAction SilentlyContinue
if ($ffx) { Write-Host "[WARN] FFX.exe ja rodando (PID $($ffx.Id)) - fechando"; $ffx | Stop-Process -Force }

# 2. Hashes antes
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$targets = @(
    "$game\modules\ffx-hooks.dll",
    "$game\data\mods\ffx_ps2\ffx\master\jppc\battle\btl"
)
foreach ($t in $targets) {
    if (Test-Path $t) { Get-FileHash $t -Algorithm SHA256 | Out-File "$outDir\hash_before.txt" -Append }
}

# 3. Boot
Write-Host "[INFO] boot do FFX.exe (Steam) - $BootSeconds s..."
if (-not $NoLaunch) { Start-Process "$game\FFX.exe" }
Start-Sleep -Seconds $BootSeconds

# 4. Log do hook
Write-Host "=== log: %TEMP%\ffx-hooks.log (ultimas linhas) ==="
if (Test-Path $logPath) {
    $log = Get-Content $logPath
    $log | Select-Object -Last 40
    $checks = @('F8 dashboard started', 'DialogSkipHook installed', 'NativeMenu', 'UnXBoosterHook started')
    foreach ($c in $checks) {
        $hit = $log | Select-String -SimpleMatch $c
        Write-Host ("[CHECK] {0} => {1}" -f $c, $(if ($hit) { 'OK' } else { 'NAO VISTO (pode ser normal se o gate nao ativou)' }))
    }
    $crash = $log | Select-String -Pattern 'CRASH|ACCESS_VIOLATION|AV WRITE|exception' 
    if ($crash) { Write-Host "[FAIL] padroes de crash no log:"; $crash | Select-Object -First 5 } else { Write-Host "[OK] sem padroes de crash no log" }
} else {
    Write-Host "[WARN] log nao existe (o jogo nao carregou o hook?)"
}

# 5. Hashes depois
foreach ($t in $targets) {
    if (Test-Path $t) { Get-FileHash $t -Algorithm SHA256 | Out-File "$outDir\hash_after.txt" -Append }
}
Write-Host "=== CHECKS MANUAIS (no jogo) ==="
Write-Host "  1. F8 -> deve abrir o DASHBOARD (tabs Plugins/Boosters/Cheats/Field/Arena+/Input)"
Write-Host "  2. F8 de novo -> fecha; movement keys voltam"
Write-Host "  3. F7 -> NativeMenu intacto (Difficulty/Force/Music)"
Write-Host "  4. Cheats (tab Cheats -> Always Overdrive ON) -> batalha: overdrive sempre cheio"
Write-Host "  5. Dialog Skip (tab Input -> ON) -> dialogo falado pula sem crash"
Write-Host "  6. Sem disputa F7/F8 (um menu de cada vez)"

# 6. Probe (Tier 2) - opcional: se o ffx-probe.dll estiver ativo no modules, o heartbeat
#    hooked=1 deve aparecer (slot vtable[9] livre com o UnX fora do deploy - INC-002 resolvido).
$probe = Get-ChildItem "$game\modules" -Filter 'ffx-probe.dll' -ErrorAction SilentlyContinue
if ($probe) {
    Write-Host "[INFO] ffx-probe.dll presente no modules - conferir heartbeat no log:"
    if (Test-Path $logPath) {
        $hb = Get-Content $logPath | Select-String -Pattern 'hooked=1|probe'
        if ($hb) { $hb | Select-Object -First 4 } else { Write-Host "[WARN] sem linhas de heartbeat do probe no log" }
    }
} else {
    Write-Host "[INFO] probe OFF (so .RT2OFF) - normal: Tier 2 na fase pos-F8 (plano secao 9)"
}
Write-Host "Artefatos: $outDir"