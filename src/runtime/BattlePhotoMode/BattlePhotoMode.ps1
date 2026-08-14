<#
.SYNOPSIS
  FFX BATTLE PHOTO MODE (lab interativo, RAM-first) — o "modo a puta que pariu".
  Congela/segura a cena de batalha e deixa tu NAVEGAR e EDITAR ao vivo pela tecla:
  seleciona boneco, move, rotaciona, sobe/desce, paneia a camera, da snapshot.

  E o PROTOTIPO do futuro BOTAO IN-GAME (a logica aqui porta pro menu da ffx-hooks).
  Le teclas globais por GetAsyncKeyState (funciona com o jogo em foco) e escreve a RAM
  em loop continuo (vence o writer por-frame na medida do possivel; pode tremer em campos
  contestados — no lab externo pode dar jitter; no porte in-DLL, escrever no tick
  pos-update deve deixar mover/mirar lisos.

  KEYMAP (tambem impresso ao iniciar):
    [  ]            -> seleciona boneco anterior / proximo
    Setas           -> move boneco selecionado no plano (Esq/Dir = X, Cima/Baixo = Z)
    PgUp / PgDn     -> sobe / desce o boneco (Y)
    , / .           -> rotaciona o boneco (yaw)  [experimental]
    W A S D         -> paneia o ALVO da camera (ref) no plano
    R / F           -> sobe / desce o alvo da camera (Y)
    Q / E           -> orbita heading - / +       [experimental]
    Z / X           -> elevacao da camera - / +   [experimental]
    Shift (segurar) -> 5x velocidade
    P               -> congela / descongela o jogo (freeze real)
    Enter           -> snapshot da cena (salva JSON: atores + camera)
    Backspace       -> reset (restaura tudo ao original)
    Esc             -> sair (restaura e descongela)

  Struct: count +0x1FC44E0 | table +0x1FC44E4 | stride 0x880 | id +0x000 | active +0x002
          pos +0x00C/10/14 | world matrix +0x1D0 (row-major; tx em +0x200) | camRef +0xD378A0
#>
param(
    [Alias('Pid')]
    [int]$GamePid = 0,
    [string]$ProcessName = 'FFX',
    [int]$PollMs = 16,
    [single]$MoveStep = 0.6,     # unidades por tick (base)
    [single]$CamStep = 0.6,
    [single]$YawStep = 0.04,     # rad por tick
    [single]$AngleStep = 0.02,   # rad por tick (heading/elev)
    [single]$SpeedMult = 5.0,
    [switch]$NoRestore,
    [string]$OutDir = 'work\actor_ram\photo'
)

$ErrorActionPreference = 'Stop'
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture
$RVA_COUNT = 0x01FC44E0; $RVA_TABLE = 0x01FC44E4
$RVA_REF = 0x00D378A0; $RVA_HEAD = 0x00D378B0; $RVA_ELEV = 0x00D378B4
$STRIDE = 0x880; $MAX_COUNT = 4096; $MTX = 0x1D0; $MTX_TX = 0x200

if (-not ('PhotoNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PhotoNative {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, int s, out IntPtr r);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, int s, out IntPtr w);
  [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
  [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vKey);
}
'@
}

function Get-TargetProcess {
    if ($GamePid -gt 0) { return Get-Process -Id $GamePid -ErrorAction Stop }
    return Get-Process -Name $ProcessName -ErrorAction Stop | Sort-Object StartTime -Descending | Select-Object -First 1
}
$proc = Get-TargetProcess
$base = [uint64]$proc.MainModule.BaseAddress.ToInt64()
$h = [PhotoNative]::OpenProcess(0x0438 -bor 0x0800, $false, $proc.Id)  # QUERY|VM_READ|VM_WRITE|VM_OPERATION|SUSPEND_RESUME
if ($h -eq [IntPtr]::Zero) { throw "OpenProcess falhou: 0x$([Runtime.InteropServices.Marshal]::GetLastWin32Error().ToString('X8'))" }

function RB { param([uint64]$a, [int]$s) $b = New-Object byte[] $s; $r = [IntPtr]::Zero; if (-not [PhotoNative]::ReadProcessMemory($h, [IntPtr][int64]$a, $b, $s, [ref]$r) -or $r.ToInt64() -ne $s) { return $null }; return $b }
function RU32 { param([uint64]$a) $b = RB $a 4; if ($null -eq $b) { return $null }; [BitConverter]::ToUInt32($b, 0) }
function RF { param([uint64]$a) $b = RB $a 4; if ($null -eq $b) { return [single]::NaN }; [BitConverter]::ToSingle($b, 0) }
function WF { param([uint64]$a, [single]$v) $b = [BitConverter]::GetBytes([single]$v); $w = [IntPtr]::Zero; [void][PhotoNative]::WriteProcessMemory($h, [IntPtr][int64]$a, $b, 4, [ref]$w) }
function Down { param([int]$vk) return ([PhotoNative]::GetAsyncKeyState($vk) -band 0x8000) -ne 0 }
function Kind { param([uint16]$id) if ($id -ge 0x1000 -and $id -le 0x1FFF) { 'monster' } elseif ($id -lt 0x1000) { 'party' } else { 'other' } }

# VK codes
$VK = @{ ESC=0x1B; ENTER=0x0D; BACK=0x08; SHIFT=0x10; SPACE=0x20;
    LEFT=0x25; UP=0x26; RIGHT=0x27; DOWN=0x28; PGUP=0x21; PGDN=0x22;
    W=0x57; A=0x41; S=0x53; D=0x44; Q=0x51; E=0x45; R=0x52; F=0x46; Z=0x5A; X=0x58;
    LB=0xDB; RB=0xDD; COMMA=0xBC; DOT=0xBE; P=0x50 }

# ── snapshot inicial dos atores ───────────────────────────────────────────
$count = RU32 ($base + $RVA_COUNT); $table = RU32 ($base + $RVA_TABLE)
if ($null -eq $count -or $null -eq $table -or $table -eq 0 -or $count -eq 0 -or $count -gt $MAX_COUNT) {
    [void][PhotoNative]::CloseHandle($h); throw "Sem tabela de atores valida (em batalha?). count=$count table=$table"
}
$actors = New-Object System.Collections.Generic.List[object]
for ($i = 0; $i -lt $count; $i++) {
    $inst = [uint64]$table + ($i * $STRIDE)
    $blk = RB $inst 0x210
    if ($null -eq $blk -or $blk[2] -eq 0) { continue }
    $id = [BitConverter]::ToUInt16($blk, 0)
    $k = Kind $id
    if ($k -eq 'other') { continue }
    $x = [BitConverter]::ToSingle($blk, 0x0C); $y = [BitConverter]::ToSingle($blk, 0x10); $z = [BitConverter]::ToSingle($blk, 0x14)
    if (([math]::Abs($x) -lt 0.01 -and [math]::Abs($y) -lt 0.01 -and [math]::Abs($z) -lt 0.01) -or [math]::Abs($x) -gt 1000.0) { continue }
    # escala uniforme da world matrix (len da row0)
    $m0 = [BitConverter]::ToSingle($blk, 0x1D0); $m1 = [BitConverter]::ToSingle($blk, 0x1D4); $m2 = [BitConverter]::ToSingle($blk, 0x1D8)
    $scale = [math]::Sqrt($m0 * $m0 + $m1 * $m1 + $m2 * $m2); if ($scale -lt 1e-6) { $scale = 0.001 }
    $actors.Add([pscustomobject]@{
        idx = $i; inst = $inst; id = $id; kind = $k
        ox = $x; oy = $y; oz = $z
        x = [single]$x; y = [single]$y; z = [single]$z; yaw = [single]0
        scale = [single]$scale; moved = $false
    }) | Out-Null
}
if ($actors.Count -eq 0) { [void][PhotoNative]::CloseHandle($h); throw "Nenhum ator party/monstro deployado." }

$camOX = RF ($base + $RVA_REF); $camOY = RF ($base + ($RVA_REF + 4)); $camOZ = RF ($base + ($RVA_REF + 8))
$cam = [pscustomobject]@{ x = [single]$camOX; y = [single]$camOY; z = [single]$camOZ; moved = $false }
$camHeadO = RF ($base + $RVA_HEAD); $camElevO = RF ($base + $RVA_ELEV)
$camHead = [single]$camHeadO; $camElev = [single]$camElevO; $camAngMoved = $false

$sel = 0
$frozen = $false
# edge-detect previous states
$prev = @{}
foreach ($kk in 'LB', 'RB', 'P', 'ENTER', 'BACK', 'ESC') { $prev[$kk] = $false }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host ""
Write-Host "=============== FFX BATTLE PHOTO MODE (lab) ==============="
Write-Host (" alvo: {0} (PID {1})  atores: {2}" -f $proc.ProcessName, $proc.Id, $actors.Count)
Write-Host " [ ]=seleciona  Setas=move XZ  PgUp/PgDn=Y  ,/.=girar  Shift=5x"
Write-Host " WASD=paneia camera  R/F=camY  Q/E=heading  Z/X=elevacao"
Write-Host " P=freeze  Enter=snapshot  Backspace=reset  Esc=sair"
Write-Host "==========================================================="
Write-Host ""

function Write-Actor { param($a)
    WF ($a.inst + 0x0C) $a.x; WF ($a.inst + 0x10) $a.y; WF ($a.inst + 0x14) $a.z
    WF ($a.inst + $MTX_TX) $a.x; WF ($a.inst + $MTX_TX + 4) $a.y; WF ($a.inst + $MTX_TX + 8) $a.z
    if ([math]::Abs($a.yaw) -gt 1e-4) {
        $s = $a.scale; $c = [math]::Cos($a.yaw); $sn = [math]::Sin($a.yaw)
        WF ($a.inst + $MTX + 0)  ([single]($c * $s));  WF ($a.inst + $MTX + 4)  ([single]0);       WF ($a.inst + $MTX + 8)  ([single]($sn * $s))
        WF ($a.inst + $MTX + 16) ([single]0);          WF ($a.inst + $MTX + 20) ([single]$s);      WF ($a.inst + $MTX + 24) ([single]0)
        WF ($a.inst + $MTX + 32) ([single](-$sn * $s)); WF ($a.inst + $MTX + 36) ([single]0);      WF ($a.inst + $MTX + 40) ([single]($c * $s))
    }
}
function Restore-All {
    foreach ($a in $actors) {
        if (-not $a.moved) { continue }
        WF ($a.inst + 0x0C) $a.ox; WF ($a.inst + 0x10) $a.oy; WF ($a.inst + 0x14) $a.oz
        WF ($a.inst + $MTX_TX) $a.ox; WF ($a.inst + $MTX_TX + 4) $a.oy; WF ($a.inst + $MTX_TX + 8) $a.oz
    }
    if ($cam.moved) { WF ($base + $RVA_REF) $camOX; WF ($base + ($RVA_REF + 4)) $camOY; WF ($base + ($RVA_REF + 8)) $camOZ }
}

$statusTick = 0
try {
    while ($true) {
        if (Down $VK.ESC) { if (-not $prev.ESC) { break }; $prev.ESC = $true } else { $prev.ESC = $false }

        $mult = if (Down $VK.SHIFT) { $SpeedMult } else { [single]1.0 }
        $ms = [single]($MoveStep * $mult); $cs = [single]($CamStep * $mult)

        # selecao (edge)
        if (Down $VK.RB) { if (-not $prev.RB) { $sel = ($sel + 1) % $actors.Count }; $prev.RB = $true } else { $prev.RB = $false }
        if (Down $VK.LB) { if (-not $prev.LB) { $sel = ($sel - 1 + $actors.Count) % $actors.Count }; $prev.LB = $true } else { $prev.LB = $false }
        $a = $actors[$sel]

        # mover boneco
        if (Down $VK.LEFT)  { $a.x = [single]($a.x - $ms); $a.moved = $true }
        if (Down $VK.RIGHT) { $a.x = [single]($a.x + $ms); $a.moved = $true }
        if (Down $VK.UP)    { $a.z = [single]($a.z + $ms); $a.moved = $true }
        if (Down $VK.DOWN)  { $a.z = [single]($a.z - $ms); $a.moved = $true }
        if (Down $VK.PGUP)  { $a.y = [single]($a.y + $ms); $a.moved = $true }
        if (Down $VK.PGDN)  { $a.y = [single]($a.y - $ms); $a.moved = $true }
        if (Down $VK.COMMA) { $a.yaw = [single]($a.yaw - $YawStep); $a.moved = $true }
        if (Down $VK.DOT)   { $a.yaw = [single]($a.yaw + $YawStep); $a.moved = $true }

        # camera (alvo/ref)
        if (Down $VK.A) { $cam.x = [single]($cam.x - $cs); $cam.moved = $true }
        if (Down $VK.D) { $cam.x = [single]($cam.x + $cs); $cam.moved = $true }
        if (Down $VK.W) { $cam.z = [single]($cam.z + $cs); $cam.moved = $true }
        if (Down $VK.S) { $cam.z = [single]($cam.z - $cs); $cam.moved = $true }
        if (Down $VK.R) { $cam.y = [single]($cam.y + $cs); $cam.moved = $true }
        if (Down $VK.F) { $cam.y = [single]($cam.y - $cs); $cam.moved = $true }
        if (Down $VK.Q) { $camHead = [single]($camHead - $AngleStep); $camAngMoved = $true }
        if (Down $VK.E) { $camHead = [single]($camHead + $AngleStep); $camAngMoved = $true }
        if (Down $VK.Z) { $camElev = [single]($camElev - $AngleStep); $camAngMoved = $true }
        if (Down $VK.X) { $camElev = [single]($camElev + $AngleStep); $camAngMoved = $true }

        # freeze toggle (edge)
        if (Down $VK.P) {
            if (-not $prev.P) {
                if ($frozen) { [void][PhotoNative]::NtResumeProcess($h); $frozen = $false }
                else { [void][PhotoNative]::NtSuspendProcess($h); $frozen = $true }
            }
            $prev.P = $true
        } else { $prev.P = $false }

        # reset (edge)
        if (Down $VK.BACK) {
            if (-not $prev.BACK) {
                Restore-All
                foreach ($aa in $actors) { $aa.x = $aa.ox; $aa.y = $aa.oy; $aa.z = $aa.oz; $aa.yaw = [single]0; $aa.moved = $false }
                $cam.x = [single]$camOX; $cam.y = [single]$camOY; $cam.z = [single]$camOZ; $cam.moved = $false
                $camHead = [single]$camHeadO; $camElev = [single]$camElevO; $camAngMoved = $false
            }
            $prev.BACK = $true
        } else { $prev.BACK = $false }

        # snapshot (edge)
        if (Down $VK.ENTER) {
            if (-not $prev.ENTER) {
                $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
                $snap = [ordered]@{
                    capturedAt = $stamp; pid = $proc.Id
                    camera = [ordered]@{ refX = $cam.x; refY = $cam.y; refZ = $cam.z; headingRad = $camHead; elevationRad = $camElev }
                    actors = @($actors | ForEach-Object { [ordered]@{ idx = $_.idx; id = ('0x{0:X4}' -f $_.id); kind = $_.kind; x = $_.x; y = $_.y; z = $_.z; yaw = $_.yaw } })
                }
                $snapPath = Join-Path $OutDir "scene_$stamp.json"
                $snap | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $snapPath -Encoding UTF8
                Write-Host "  [snapshot] $snapPath"
            }
            $prev.ENTER = $true
        } else { $prev.ENTER = $false }

        # ── escreve os overrides (segura contra o writer por-frame) ──
        if (-not $frozen) {
            foreach ($aa in $actors) { if ($aa.moved) { Write-Actor $aa } }
            if ($cam.moved) { WF ($base + $RVA_REF) $cam.x; WF ($base + ($RVA_REF + 4)) $cam.y; WF ($base + ($RVA_REF + 8)) $cam.z }
            if ($camAngMoved) { WF ($base + $RVA_HEAD) $camHead; WF ($base + $RVA_ELEV) $camElev }
        }

        $statusTick++
        if ($statusTick % 20 -eq 0) {
            Write-Host ("`r sel #{0} id=0x{1:X4} {2} ({3:F1},{4:F1},{5:F1}) yaw={6:F2} | cam({7:F1},{8:F1},{9:F1}) | {10}    " -f `
                $sel, $a.id, $a.kind, $a.x, $a.y, $a.z, $a.yaw, $cam.x, $cam.y, $cam.z, ($(if ($frozen) { 'FROZEN' } else { 'live' }))) -NoNewline
        }
        Start-Sleep -Milliseconds $PollMs
    }
} finally {
    if ($frozen) { [void][PhotoNative]::NtResumeProcess($h) }
    if (-not $NoRestore) { Restore-All }
    [void][PhotoNative]::CloseHandle($h)
    Write-Host ""
    Write-Host "saiu do photo mode (restore=$(-not $NoRestore))"
}
