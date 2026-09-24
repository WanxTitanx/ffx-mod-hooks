# F7 modal/input/UI RT0 harness. It compiles only the portable state machine and geometry core.
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

$objDir = Join-Path $PSScriptRoot 'obj\f7-ui-rt0'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
$testSource = Join-Path $PSScriptRoot 'tests\F7UiRt0.cpp'
$coreSource = Join-Path $PSScriptRoot 'hooks\F7UiCore.cpp'
$exe = Join-Path $objDir 'F7UiRt0.exe'
$pdb = Join-Path $objDir 'F7UiRt0.pdb'
$compile = 'call "{0}" x86 >nul && cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT "{1}" "{2}" /Fd"{3}" /Fe"{4}"' -f `
    $vcvarsall, $testSource, $coreSource, $pdb, $exe

Push-Location $objDir
try {
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "F7 UI RT0 compilation failed with exit code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "F7 UI RT0 executable failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host 'F7 UI portable core: PASS'

# The portable tests prove the transition semantics; these narrow adapter
# contracts ensure every submenu exit actually delegates to that transition.
$dllmainPath = Join-Path $PSScriptRoot 'dllmain.cpp'
$dllmain = Get-Content -Raw -LiteralPath $dllmainPath
$f7InLive = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'hooks\F7InLive.cpp')
function Assert-SourceContract([string]$Pattern, [string]$Message) {
    if ($dllmain -notmatch $Pattern) { throw "F7 UI adapter contract failed: $Message" }
}

Assert-SourceContract `
    'if \(row == SIN_RAM_ROW_BACK\) \{\s*F7CloseTransition\(\s*FfxHooks::F7Ui::CloseSource::BackRow,\s*FfxHooks::F7Ui::CloseDestination::Hub\);' `
    'S.I.N. Back must use the shared F7 close transition'
Assert-SourceContract `
    'if \(row == ARENA_PLUS_HUB_ROW_BACK\) \{\s*F7CloseTransition\(\s*FfxHooks::F7Ui::CloseSource::BackRow,\s*FfxHooks::F7Ui::CloseDestination::Hub\);' `
    'Arena+ hub Back must use the shared F7 close transition'
Assert-SourceContract `
    '(?s)ArenaPlus_CloseMenu\(g_arenaPlusMenu\);.*?if \(g_arenaPlusMenuKind == ArenaPlusMenuKind::Ultra\).*?ProductionCancel\(.*?CancelReason::Cancel\).*?ArenaPlus_SpawnHubMenu\(\).*?else \{\s*F7CloseTransition\(\s*FfxHooks::F7Ui::CloseSource::Cancel,\s*FfxHooks::F7Ui::CloseDestination::Hub\);' `
    'Arena+ Cancel must return Ultra to Arena+ while standard submenus retain the shared F7 transition'
Assert-SourceContract `
    'SinCurse_CloseMenu\(\);\s*F7CloseTransition\(\s*FfxHooks::F7Ui::CloseSource::Cancel,\s*FfxHooks::F7Ui::CloseDestination::Hub\);' `
    'S.I.N. Cancel must use the shared F7 close transition'
$nativeShellPath = Join-Path $PSScriptRoot '..\NativeMenuShell\NativeMenuShell.h'
$nativeShell = Get-Content -Raw -LiteralPath $nativeShellPath
if ($nativeShell -notmatch 'EncodeLabel\("Arrows/Mouse Navigate\s+Confirm Open\s+Cancel Back\s+F7 Exit", s_foot, 64\);') {
    throw 'F7 UI adapter contract failed: the main F7 footer must advertise its working mouse navigation'
}
if ($nativeShell -notmatch '\{ "S\.I\.N\. Curses",\s+ACT_SIN') {
    throw 'F7 UI adapter contract failed: native hub must identify S.I.N. Curses'
}
$sinStart = $dllmain.IndexOf('// -- S.I.N. RAM submenu state --')
$sinEnd = if ($sinStart -ge 0) { $dllmain.IndexOf('static const char* kArenaPlusDarkNames', $sinStart) } else { -1 }
if ($sinStart -lt 0 -or $sinEnd -le $sinStart) { throw 'S.I.N. menu source not found' }
$sin = $dllmain.Substring($sinStart, $sinEnd - $sinStart)
foreach ($token in @('SIN_RAM_ROW_ENABLED','SIN_RAM_ROW_DISTRIBUTION','SIN_RAM_ROW_SEED','SIN_RAM_ROW_SHUFFLE','SIN_RAM_ROW_AREA','SIN_RAM_ROW_SAVE','SIN_RAM_ROW_GUIDE','SIN_RAM_ROW_BACK','SIN_RAM_ROW_COUNT         8')) {
    if (-not $sin.Contains($token)) { throw "S.I.N. seeded control missing $token" }
}
foreach ($label in @('S.I.N. - Curses of Sin','Seeded encounters in Macalania','Cursed monsters:','HP, AP, Gil: +10% per Threat.','Stats: +5% per Threat, then +Threat.','After Difficulty. Model growth: preview.')) {
    if (-not $sin.Contains($label)) { throw "S.I.N. player-facing label missing $label" }
}
if ($sin.Contains('SIN_RAM_ROW_THREAT') -or $sin.Contains('F7_DifficultyApplyNow()')) {
    throw 'Seeded S.I.N. must not expose manual threats or reroll current combat'
}
foreach ($state in @('INVALID','OFF','UNAVAILABLE','WAIT NATURAL','CURRENT NATURAL')) {
    if (-not $f7InLive.Contains('return "' + $state + '";')) { throw "S.I.N. runtime state missing $state" }
}
if ($sin -notmatch 'F7ListMouseTick\(' -or $sin -notmatch 'ResolveDirectionalInput\(dir, mouse\.ownsDirectionalFrame\)' -or $sin -notmatch 'F7SeedPointerForDestination\(\);') {
    throw 'S.I.N. must retain shared mouse, controller and release-barrier handling'
}
if ($sin -notmatch '(?s)SIN_RAM_ROW_SAVE.*?F7_SetSinRamConfig\(g_sinDraft\).*?F7_SaveConfig\(\)') {
    throw 'S.I.N. must save through the existing sole configuration writer'
}
foreach ($feedback in @('Saved for next encounter','Save failed - memory only','NativeMenu::PlaySfx(saved ? 4 : 3)')) {
    if (-not $sin.Contains($feedback)) { throw "S.I.N. truthful save feedback missing $feedback" }
}
if ($f7InLive -notmatch 'if \(!sinRequest\.config\.seeded\)') {
    throw 'Seeded S.I.N. must preserve the current encounter assignment'
}
if ($dllmain -notmatch '(?s)if \(effects\.cancelDraft\).*?g_sinDraftActive = false;.*?g_sinSeedEditing = false;.*?ArenaMixRenameAbort\(\);.*?SinRam_ClearSaveFeedback\(\);') {
    throw 'S.I.N. close must discard draft, release text capture and clear feedback'
}

Assert-SourceContract `
    'R\.type == F7RT_BACK \|\| R\.type == F7RT_ACTION \|\|\s*\(g_f7MenuKind == F7_MENU_FORCE && sel == 1\)' `
    'Force Repeat confirm must enter the explicit action path instead of silently discarding the draft'
Assert-SourceContract `
    '(?s)if \(g_f7MenuKind == F7_MENU_FORCE\) \{\s*if \(row == 0\).*?else if \(row == 1\) \{\s*F7_CommitValsToConfig\(\);\s*FfxHooks::F7_SaveConfig\(\);' `
    'Force Repeat row 1 must save its own mapped draft value'

$mouseHelperDeclaration = $dllmain.IndexOf('static F7MouseInputResult F7ListMouseTick(')
$mouseHelperStart = if ($mouseHelperDeclaration -ge 0) {
    $dllmain.IndexOf('static F7MouseInputResult F7ListMouseTick(', $mouseHelperDeclaration + 1)
} else { -1 }
$mouseHelperEnd = if ($mouseHelperStart -ge 0) {
    $dllmain.IndexOf('static NativeMenu::Menu F7Sub_SpawnMenu', $mouseHelperStart)
} else { -1 }
if ($mouseHelperStart -lt 0 -or $mouseHelperEnd -le $mouseHelperStart) {
    throw 'F7 UI adapter contract failed: list mouse helper body was not found'
}
$mouseHelper = $dllmain.Substring($mouseHelperStart, $mouseHelperEnd - $mouseHelperStart)
if ($mouseHelper -match 'g_f7EasedRowY') {
    throw 'F7 UI adapter contract failed: shared list hit testing must not mutate an F7-only highlight'
}
if ($mouseHelper -notmatch 'ResolveListPointerInput') {
    throw 'F7 UI adapter contract failed: list mouse input must use the portable mixed-input resolver'
}
Assert-SourceContract `
    '(?s)static F7PointerSnapshot F7CapturePointer\(\).*?ObservePointer\(g_f7PointerState, sample\)' `
    'pointer capture must apply the portable movement and release-barrier policy'
$mainMouseStart = $dllmain.IndexOf('static void F7MainMenuMouseTick(int obj)')
$mainMouseEnd = if ($mainMouseStart -ge 0) {
    $dllmain.IndexOf('static F7MouseInputResult F7ListMouseTick(', $mainMouseStart)
} else { -1 }
if ($mainMouseStart -lt 0 -or $mainMouseEnd -le $mainMouseStart -or
    $dllmain.Substring($mainMouseStart, $mainMouseEnd - $mainMouseStart) -notmatch
        'ResolveListPointerInput\(selection, hit, pointer\.decision\)') {
    throw 'F7 UI adapter contract failed: the main hub must use activity-gated pointer resolution'
}
$difficultyMouseStart = $dllmain.IndexOf('static F7MouseInputResult F7DifficultyMouseTick()')
$difficultyMouseEnd = if ($difficultyMouseStart -ge 0) {
    $dllmain.IndexOf('static void F7DiffSetStatus', $difficultyMouseStart)
} else { -1 }
if ($difficultyMouseStart -lt 0 -or $difficultyMouseEnd -le $difficultyMouseStart) {
    throw 'F7 UI adapter contract failed: Difficulty mouse helper body was not found'
}
$difficultyMouse = $dllmain.Substring(
    $difficultyMouseStart, $difficultyMouseEnd - $difficultyMouseStart)
if ($difficultyMouse -notmatch 'pointer\.decision\.applyHover' -or
    $difficultyMouse -notmatch 'ResolveListPointerInput') {
    throw 'F7 UI adapter contract failed: Difficulty hover must require explicit pointer activity'
}
if (([regex]::Matches($dllmain, 'ResolveDirectionalInput\(dir, mouse\.ownsDirectionalFrame\)')).Count -lt 4) {
    throw 'F7 UI adapter contract failed: every F7 list family must give a pressed hit directional precedence'
}
if ($dllmain -match 'g_f7UiModalState\.mouseButtonDown') {
    throw 'F7 UI adapter contract failed: modal cleanup must not own or clear the physical mouse latch'
}
Assert-SourceContract `
    '(?s)static void F7SeedPointerForDestination\(\).*?GetAsyncKeyState\(VK_LBUTTON\).*?SeedPointerForDestination' `
    'every destination must seed a release barrier from the physical button state'
if (([regex]::Matches($dllmain, 'F7SeedPointerForDestination\(\);')).Count -lt 4) {
    throw 'F7 UI adapter contract failed: hub, F7 submenu, S.I.N., and Arena+ spawns must seed the release barrier'
}

$hubInputStart = $nativeShell.IndexOf('static int __cdecl OurListInputCb(int obj)')
$hubInputEnd = if ($hubInputStart -ge 0) {
    $nativeShell.IndexOf('static inline void ClaimModal', $hubInputStart)
} else { -1 }
if ($hubInputStart -lt 0 -or $hubInputEnd -le $hubInputStart) {
    throw 'F7 UI adapter contract failed: native hub input callback body was not found'
}
$hubInput = $nativeShell.Substring($hubInputStart, $hubInputEnd - $hubInputStart)
$mouseAuthorityGuard = $hubInput.IndexOf('if (g_ourClosed) return obj;')
$hubPadDir = $hubInput.IndexOf('PadDir()')
$hubPadEdge = $hubInput.IndexOf('PadEdge()')
if ($mouseAuthorityGuard -lt 0 -or $mouseAuthorityGuard -gt $hubPadDir -or
    $mouseAuthorityGuard -gt $hubPadEdge) {
    throw 'F7 UI adapter contract failed: a pre-input mouse confirm must suppress hub PadDir and PadEdge'
}

$pumpStart = $dllmain.IndexOf('static int __cdecl NativeMenu_PumpHook(unsigned int a1)')
$pumpEnd = if ($pumpStart -ge 0) {
    $dllmain.IndexOf('static int F7DiffColRows', $pumpStart)
} else { -1 }
if ($pumpStart -lt 0 -or $pumpEnd -le $pumpStart) {
    throw 'F7 UI adapter contract failed: native pump body was not found'
}
$pump = $dllmain.Substring($pumpStart, $pumpEnd - $pumpStart)
$preInputMouse = $pump.IndexOf('F7MainMenuMouseTick(g_nativeMenu.obj);')
$originalPump = $pump.IndexOf('g_nativeMenuPumpTramp')
if ($preInputMouse -lt 0 -or $originalPump -lt 0 -or $preInputMouse -gt $originalPump -or
    ([regex]::Matches($pump, 'F7MainMenuMouseTick\(g_nativeMenu\.obj\);')).Count -ne 1) {
    throw 'F7 UI adapter contract failed: hub mouse authority must run exactly once before the original input callback'
}

$rowsStart = $dllmain.IndexOf('static void ArenaPlus_BuildRowsForKind(ArenaPlusMenuKind kind)')
$rowsEnd = if ($rowsStart -ge 0) { $dllmain.IndexOf('static void ArenaPlus_BuildRows()', $rowsStart) } else { -1 }
if ($rowsStart -lt 0 -or $rowsEnd -le $rowsStart) {
    throw 'F7 UI adapter contract failed: Arena+ row builder was not found'
}
$rowsAdapter = $dllmain.Substring($rowsStart, $rowsEnd - $rowsStart)
if ($rowsAdapter -match '(?s)ArenaPlus_BuildRowsForKind\(ArenaPlusMenuKind kind\)\s*\{\s*ArenaPlus_RefreshDiskSaveDarkCache\(\)') {
    throw 'F7 UI adapter contract failed: the generic row builder performs disk I/O before rejecting Ultra'
}

$ultraStart = $dllmain.IndexOf('static void ArenaPlus_Ultra_HandleConfirm')
$ultraEnd = if ($ultraStart -ge 0) {
    $dllmain.IndexOf('static void ArenaPlus_HandleMenuConfirm', $ultraStart)
} else { -1 }
if ($ultraStart -lt 0 -or $ultraEnd -le $ultraStart) {
    throw 'F7 UI adapter contract failed: Arena+ Ultra selection handler was not found'
}
$ultra = $dllmain.Substring($ultraStart, $ultraEnd - $ultraStart)
foreach ($token in @(
    'ArenaPlusMenuKind::Ultra', 'ARENA_PLUS_ULTRA_ROW_REMOVE_LAST',
    'ARENA_PLUS_ULTRA_ROW_CLEAR', 'ARENA_PLUS_ULTRA_ROW_LAUNCH',
    'ARENA_PLUS_ULTRA_ROW_BACK', 'TryAddChoice(', 'RemoveLastChoice(',
    'ClearSelection(', 'BuildSelection(', 'LaunchEditorSelection(',
    'ProductionOperational()', 'ProductionPublishSelection('
)) {
    if (-not $dllmain.Contains($token)) {
        throw "F7 UI adapter contract failed: Arena+ Ultra is missing $token"
    }
}
if ($dllmain -notmatch '(?s)ArenaPlusMenuKind::Ultra.*?F7SeedPointerForDestination\(\);.*?NativeMenu::Register\(obj\);') {
    throw 'F7 UI adapter contract failed: generic Arena+ Ultra spawn must retain the shared pointer release barrier'
}
if ($dllmain -notmatch '(?s)ArenaPlusMenuKind::Ultra.*?ProductionCancel\(.*?CancelReason::Cancel.*?ArenaPlus_SpawnHubMenu\(\)') {
    throw 'F7 UI adapter contract failed: Ultra cancel must clear the request and return to the Arena+ hub'
}

function Get-AdapterSlice([string]$StartToken, [string]$EndToken) {
    $start = $dllmain.IndexOf($StartToken)
    $end = if ($start -ge 0) { $dllmain.IndexOf($EndToken, $start + $StartToken.Length) } else { -1 }
    if ($start -lt 0 -or $end -le $start) {
        throw "F7 UI adapter contract failed: reachable slice '$StartToken' was not found"
    }
    return $dllmain.Substring($start, $end - $start)
}

$ultraPreview = Get-AdapterSlice 'static void ArenaPlus_BuildUltraPreview()' 'static void ArenaPlus_BuildUltraRows()'
$ultraRows = Get-AdapterSlice 'static void ArenaPlus_BuildUltraRows()' 'static void ArenaPlus_BuildHubRows()'
$hubRows = Get-AdapterSlice 'static void ArenaPlus_BuildHubRows()' 'static int ArenaPlus_CustomMixComboIndex'
$preparedSpawn = Get-AdapterSlice 'static NativeMenu::Menu ArenaPlus_SpawnPreparedMenu' 'static NativeMenu::Menu ArenaPlus_SpawnUltraMenu'
$ultraSpawn = Get-AdapterSlice 'static NativeMenu::Menu ArenaPlus_SpawnUltraMenu' 'static NativeMenu::Menu ArenaPlus_SpawnHubMenu'
$hubSpawn = Get-AdapterSlice 'static NativeMenu::Menu ArenaPlus_SpawnHubMenu' 'static NativeMenu::Menu ArenaPlus_SpawnMenuKind'
$ultraReopen = Get-AdapterSlice 'static void ArenaPlus_Ultra_Reopen()' 'static void ArenaPlus_Ultra_HandleConfirm'
$directRequest = Get-AdapterSlice 'static bool ArenaPlus_LaunchBattle781D60Request(' 'struct ArenaPlusUltraLaunchContext'
$ultraQueue = Get-AdapterSlice 'ArenaPlus_UltraQueueCarrier(void* rawContext) noexcept' 'static bool ArenaPlus_UltraArmRequest'
$ultraCallbacks = Get-AdapterSlice 'static bool ArenaPlus_UltraArmRequest' 'ArenaPlus_Ultra_LaunchFromPump() {'
$ultraLaunch = Get-AdapterSlice 'ArenaPlus_Ultra_LaunchFromPump() {' 'static bool ArenaPlus_LaunchBattle7002Template'
$reachableUltraAdapter = $ultraPreview + $ultraRows + $hubRows + $preparedSpawn + $ultraSpawn + $hubSpawn + $ultraReopen + $ultra + $directRequest + $ultraQueue + $ultraCallbacks + $ultraLaunch
foreach ($forbidden in @(
    'ArenaPlus_RefreshDiskSaveDarkCache', 'ArenaPlusComposePick',
    'CreateFile', 'ReadFile', 'ArenaPlus_CatalogReadFile',
    'ArenaPlus_ReadDarkDefeatFromDisk', 'ArenaPlus_TickDeferredFileRestore'
)) {
    if ($reachableUltraAdapter.Contains($forbidden)) {
        throw "F7 UI adapter contract failed: reachable Ultra editor path contains disk/legacy token $forbidden"
    }
}
if (-not $ultraSpawn.Contains('ArenaPlus_BuildUltraRows()') -or
    $ultraSpawn.Contains('ArenaPlus_BuildRowsForKind')) {
    throw 'F7 UI adapter contract failed: Ultra spawn must use only its dedicated I/O-free row builder'
}
$standardRows = Get-AdapterSlice 'static void ArenaPlus_BuildRowsForKind(ArenaPlusMenuKind kind)' 'static void ArenaPlus_BuildRows()'
if ($standardRows.Contains('ArenaPlusMenuKind::Ultra') -or
    ([regex]::Matches($standardRows, 'ArenaPlus_RefreshDiskSaveDarkCache\(\)')).Count -ne 1 -or
    $standardRows -notmatch '(?s)ArenaPlusMenuKind::DarkRematch\).*?ArenaPlus_RefreshDiskSaveDarkCache\(\)') {
    throw 'F7 UI adapter contract failed: disk-save refresh must be reachable only from Dark Rematch rows, never Ultra'
}

Write-Host 'F7 UI adapter contracts: PASS'
Write-Host 'F7 UI RT0: PASS'
