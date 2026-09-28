# RT2 limpo do Sphere Grid — toggle de hooks (pré-flight)
# Uso:  powershell -File rt2_hooks_toggle.ps1 -Off   (desliga ffx-hooks/ffx-probe; mantém file-loader p/ overlay)
#       powershell -File rt2_hooks_toggle.ps1 -On    (restaura)
# Reversível: renomeia para .RT2OFF / volta. NUNCA roda com o jogo aberto.
param([ValidateSet('On','Off')][string]$Action = 'Off')

$game = 'D:\SteamLibrary\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster'
$mods = Join-Path $game 'modules'
$targets = @('ffx-hooks.dll', 'ffx-probe.dll')
$suffix = '.RT2OFF'

# Bloqueio: jogo aberto = DLLs em uso
$ffx = Get-Process -Name 'FFX','FFX&X-2_Will','FFX-2' -ErrorAction SilentlyContinue
if ($ffx) {
    Write-Host 'ERRO: FFX esta aberto. Feche o jogo antes do toggle (DLLs em uso).'
    exit 1
}

foreach ($t in $targets) {
    $src = Join-Path $mods $t
    $dst = Join-Path $mods ($t + $suffix)
    if ($Action -eq 'Off') {
        if (Test-Path $src) {
            if (Test-Path $dst) { Remove-Item $dst -Force }
            Rename-Item $src $dst
            Write-Host ("OFF: {0} -> {1}" -f $t, ($t + $suffix))
        } elseif (Test-Path $dst) {
            Write-Host ("OFF: {0} ja esta desligado (skipped)" -f $t)
        } else {
            Write-Host ("AVISO: {0} nao encontrado em modules\" -f $t)
        }
    } else {
        if (Test-Path $dst) {
            Rename-Item $dst $src
            Write-Host ("ON: {0} restaurado" -f $t)
        } elseif (Test-Path $src) {
            Write-Host ("ON: {0} ja esta ativo (skipped)" -f $t)
        }
    }
}

$manifest = Join-Path $mods 'config\square_grid_manifest.json'
$report = Join-Path $mods 'config\square_grid_report.txt'
if ($Action -eq 'Off') {
    foreach ($f in @($manifest, $report)) {
        if (Test-Path $f) {
            Rename-Item $f ($f + '.legado')
            Write-Host ("OFF: sidecar legado -> {0}.legado" -f (Split-Path $f -Leaf))
        }
    }
    Write-Host ''
    Write-Host 'Estado RT2 LIMPO: file-loader ativo (dats do overlay), hooks OFF.'
    Write-Host 'Proximo: save descartavel + abrir o jogo + Sphere Grid + mover/ativar no novo + sair sem crash.'
    Write-Host 'Restaurar depois: powershell -File rt2_hooks_toggle.ps1 -On  (ou FFX-RESTORE.bat)'
} else {
    foreach ($f in @($manifest, $report)) {
        $leg = $f + '.legado'
        if (Test-Path $leg) { Rename-Item $leg $f; Write-Host ("ON: sidecar {0} restaurado" -f (Split-Path $f -Leaf)) }
    }
    Write-Host 'Hooks restaurados (modo MOD normal).'
}
