/* Arena+ Custom Mix Phase 2 â€” F7 boss checklist + subprocess --compose before launch. */
#define WIN32_LEAN_AND_MEAN
#include "ArenaPlusComposePick.h"
#include "ArenaPlusGil.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK

#include "../FfxDinput8Probe/ffx_probe_block.h"
#include "../NativeMenuShell/NativeMenuShell.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace {

static ArenaPlusComposeLogFn g_log = nullptr;
static HMODULE g_module = NULL;

static const int kCustomMixFirstCombo = 5;
static const int kCustomMixLastCombo = 7;
static const int kScenarioSlotMax = 4;
static const int kPickCount = 8;
static const int kMagusPickIndex = 7;
static const int kMaxRowCount = kScenarioSlotMax + kPickCount + 5;
static const int kLabelCap = 64;
static const int kVisiblePage = 10;

enum ComposePickResult : int {
    ComposePickResult_None = 0,
    ComposePickResult_Relaunch = 1,
    ComposePickResult_Launch = 2,
    ComposePickResult_Back = 3,
};

static const char* kPickKeys[kPickCount] = {
    "valefor", "ifrit", "ixion", "shiva", "bahamut", "yojimbo", "anima", "magus"
};

static const char* kPickLabels[kPickCount] = {
    "Dark Valefor",
    "Dark Ifrit",
    "Dark Ixion",
    "Dark Shiva",
    "Dark Bahamut",
    "Dark Yojimbo",
    "Dark Anima",
    "Dark Magus (+3)"
};

enum ComposeJobState : int {
    JOB_IDLE = 0,
    JOB_RUNNING,
    JOB_OK,
    JOB_FAIL,
};

static NativeMenu::Menu g_menu = { 0 };
static char g_labels[kMaxRowCount][kLabelCap] = {};
static unsigned char g_labelBytes[kMaxRowCount][kLabelCap] = {};
static uint8_t g_selected[kPickCount] = {};
static int g_pickOrder[kPickCount] = {};  /* 1..N when selected; 0 = off */
static int g_pickOrderSeq = 0;
static int g_targetCombo = -1;
static int g_requiredActors = 0;
static volatile int g_drawCalls = 0;
static volatile int g_result = 0;
static volatile int g_closed = 0;
static volatile int g_inputCooldown = 0;
static int g_lastConfirmEdge = 0;
/* Menu frames @ ~60fps â€” aeon toggles stay snappy; battle rows wait out held Enter. */
static const int kCooldownOpen = 24;
static const int kCooldownScenario = 14;
static const int kCooldownAeonPick = 7;
static const int kCooldownBattleRow = 8;
static char g_statusLine[128] = "Toggle bosses, then Launch.  (E/L2-LT = Edit Positions after Build Preview)";
static char g_footerLine[128] = "Confirm toggle/launch   Cancel Back";

static volatile LONG g_jobState = JOB_IDLE;
static volatile LONG g_jobExitCode = -1;
static HANDLE g_jobThread = NULL;
static char g_jobPickCsv[256] = {};
static char g_jobError[256] = {};
static char g_lastMixSummary[128] = {};
static char g_lastMixBattleId[32] = {};
static char g_lastMixScenarioKey[32] = {};
static char g_lastMixPicks[8][32] = {};
static int g_lastMixPickCount = 0;
static int g_lastMixActorCount = 0;
static char g_scenarioKeys[kScenarioSlotMax][32] = {};
static char g_scenarioLabels[kScenarioSlotMax][48] = {};
static int g_scenarioCount = 0;
static int g_scenarioSelected = 0;
static char g_pendingLaunchScenarioKey[32] = {};
static bool g_lastMixValid = false;
static bool g_lastMixBinReady = false;
static bool g_hasBuiltThisSession = false;  // first build of this session (reset on compose pick open)
static int g_pendingLaunchGilCost = 0;
/* Fase 2 â€” preview ingame: as linhas do mapa do layout (prefixo PREV| do lab). */
static char g_previewLines[12][64] = {};
static unsigned char g_previewBytes[12][64] = {};
static volatile LONG g_previewCount = 0;
static int g_previewEncoded = -1;
/* Fase 3 â€” ajuste de layout por cenario (perfil JSON modules\arena_layout_profiles.json). */
static const int kAdjCount = 7;                    /* elev, dist, spread, shift, mon dist, mon arc, mode */
static int g_adjustMode = 0;                       /* 0 normal, 1 ajustando layout */
static int g_adjParam = 0;                         /* 0..6 */
static char g_profilePath[MAX_PATH * 2] = {};
static float g_scenProfile[kScenarioSlotMax][kAdjCount] = {};
static const float g_adjSteps[kAdjCount] = { 0.5f, 0.05f, 0.05f, 0.5f, 5.0f, 5.0f, 1.0f };
static const char* g_adjParamNames[kAdjCount] = { "cam elev", "cam dist", "mon spread", "mon shift", "mon dist", "mon arc", "mode" };
// Live position edit (Part 1, 2026-08-03): editable dot map window.
// Grid = preview map (g_previewLines), 16x8 cells.
// As setas movem o cursor (o P do bicho ativo); L/R alterna o bicho; ENTER salva o JSON das posicoes.
// WHY: o jogo le as posicoes dos monstros/players das AN CORAS do chunk3 do bin (pos = bin + chunk3 + 16*a5);
// salvar o grid editado deixa o compose (o runner C#) aplicar via BattleArenaPositionWriter -> a batalha
// inicia com o posicionamento custom.
static int g_editPosMode = 0;       // 0 normal, 1 editando posicao
static int g_edSel = 0;             // bicho ativo (indice em g_edPos)
static int g_edBichos = 0;          // total de bichos (P no mapa)
static int g_edPos[24][2] = {};     // [bicho][0]=gx [1]=gy — posicao no grid 16x8
static int g_edPosOrig[24][2] = {}; // posicao ORIGINAL quando o E carregou (p/ o runner aplicar DELTA)
static bool g_editCamMode = false;  // E alterna o alvo: false=posicoes (bichos), true=camera
static int g_edCam[2] = { 8, 4 };    // camera no grid 16x8 (gx, gy) - projecao do yaw+zoom (visual)
static int g_edCamOrig[2] = { 8, 4 }; // snapshot da camera original (p/ nao apagar o JSON se so a CAM mudou)
static int g_camParam = 0;          // parametro ativo no modo CAM: 0=YAW, 1=PITCH, 2=ZOOM
static int g_edCamYaw = 180;        // yaw (0-360) - direcao orbital da camera (a 3a dim eh explicita)
static int g_edCamPitch = 12;       // pitch (-45..45) - elevacao (a 2a "outra" dimensao)
static int g_edCamZoom = 40;        // zoom/distancia (10-80) ao centro (a 3a "outra" dimensao)
static int g_edCamYawOrig = 180, g_edCamPitchOrig = 12, g_edCamZoomOrig = 40; // snapshot original da CAM
static bool g_edLoaded = false;     // se o g_edPos ja foi carregado (das edicoes). Senao, NAO re-montar do preview.
static char g_edGrid[16][8];        // o grid montado (16x8) para o desenho
static char g_positionsPath[MAX_PATH] = {};   // path do arena_positions.json (salvo no modules)
/* Fase 4 â€” formacoes: slots infinitos em modules\arena_formations\formation_XXX.json.
 * 1 = lista de import (janela), 2 = popup de exclusao com timer de 5s. */
static const int kFormationMax = 64;
static const int kDeleteTimerFrames = 300;   /* 5s @ 60fps */
static char g_formationsDir[MAX_PATH * 2] = {};
static char g_formationList[kFormationMax][64] = {};
static int g_formationCount = 0;
static int g_importMode = 0;
static int g_importSel = 0;
static int g_lastFormSel = 0;              /* a formaÃ§Ã£o selecionada antes da row Excluir */
static int g_confirmMode = 0;
static int g_confirmChoice = 0;              /* 0 = excluir, 1 = cancelar */
static int g_delTimer = 0;
static char g_delTarget[64] = {};

static int ComposePick_PickFirst() { return g_scenarioCount; }
static int ComposePick_RowRelaunch() { return ComposePick_PickFirst() + kPickCount; }
static int ComposePick_RowBuildPreview() { return ComposePick_RowRelaunch() + 1; }
static int ComposePick_RowFormations() { return ComposePick_RowBuildPreview() + 1; }
static int ComposePick_RowLaunch() { return ComposePick_RowFormations() + 1; }
static int ComposePick_RowBack() { return ComposePick_RowLaunch() + 1; }
static int ComposePick_TotalRows() { return ComposePick_RowBack() + 1; }

static bool ScenarioBackdropBattleIdForKey(int combo, const char* key, char* out, size_t outCap) {
    if (!key || !key[0] || !out || outCap == 0) return false;
    if (_stricmp(key, "remiem") == 0) {
        lstrcpynA(out, "kino00_00", static_cast<int>(outCap));
        return true;
    }
    if (combo == 7) {
        if (_stricmp(key, "cavern") == 0) {
            lstrcpynA(out, "nagi05_50", static_cast<int>(outCap));
            return true;
        }
        if (_stricmp(key, "cavern_alt") == 0) {
            lstrcpynA(out, "nagi05_25", static_cast<int>(outCap));
            return true;
        }
    }
    if (combo == 6 && _stricmp(key, "cavern") == 0) {
        lstrcpynA(out, "nagi05_24", static_cast<int>(outCap));
        return true;
    }
    struct Row { const char* key; const char* battleId; };
    static const Row kRows[] = {
        { "macalania_forest", "mcfr00_00" },
        { "macalania_open", "mcyt00_00" },
        { "macalania_open2", "mcyt00_21" },
        { "macalania", "mcyt00_21" },
        { "bikanel", "bika02_01" },
        { "remiem", "kino00_00" },
    };
    for (const Row& row : kRows) {
        if (_stricmp(key, row.key) != 0) continue;
        lstrcpynA(out, row.battleId, static_cast<int>(outCap));
        return true;
    }
    return false;
}

static void CopyBackdropBattleIdOptional(const char* src, char* out, int outCap) {
    if (!out || outCap <= 0) return;
    out[0] = '\0';
    if (!src || !src[0]) return;
    lstrcpynA(out, src, outCap);
}

static bool ScenarioBattlefieldIdForKey(const char* key, int* battlefieldId) {
    if (!key || !key[0] || !battlefieldId) return false;
    /* btl.bin group battlefield u16 â€” visual-only backdrop @ MemoryBtl 0xD2C254 (LOWORD). */
    if (_stricmp(key, "macalania_forest") == 0) {
        *battlefieldId = 1044; /* mcfr00 g0 */
        return true;
    }
    if (_stricmp(key, "macalania_open") == 0 || _stricmp(key, "macalania_open2") == 0 ||
        _stricmp(key, "macalania") == 0) {
        *battlefieldId = 1046; /* mcyt00 g0 */
        return true;
    }
    if (_stricmp(key, "cavern") == 0 || _stricmp(key, "cavern_alt") == 0) {
        *battlefieldId = 1080; /* nagi05 g2 */
        return true;
    }
    if (_stricmp(key, "bikanel") == 0) {
        *battlefieldId = 1049; /* bika02/bika03 g0 desert bf 0x0419 */
        return true;
    }
    if (_stricmp(key, "remiem") == 0) {
        *battlefieldId = 1035; /* kino00 g0/g1 */
        return true;
    }
    return false;
}

static bool ScenarioRouteForKey(int combo, const char* key, int* field, int* group, int* formation) {
    if (!key || !key[0] || !field || !group || !formation) return false;
    if (combo == 5) {
        if (_stricmp(key, "macalania_forest") == 0) { *field = 36; *group = 0; *formation = 0; return true; }
        if (_stricmp(key, "macalania_open") == 0) { *field = 42; *group = 0; *formation = 0; return true; }
        if (_stricmp(key, "macalania_open2") == 0 || _stricmp(key, "macalania") == 0) {
            *field = 42; *group = 0; *formation = 21; return true;
        }
        if (_stricmp(key, "remiem") == 0) { *field = 24; *group = 0; *formation = 0; return true; }
        return false;
    }
    if (combo == 6) {
        if (_stricmp(key, "cavern") == 0) { *field = 63; *group = 2; *formation = 24; return true; }
        if (_stricmp(key, "bikanel") == 0) { *field = 47; *group = 0; *formation = 1; return true; }
        if (_stricmp(key, "remiem") == 0) { *field = 24; *group = 0; *formation = 0; return true; }
        return false;
    }
    if (combo == 7) {
        if (_stricmp(key, "cavern") == 0) { *field = 63; *group = 2; *formation = 50; return true; }
        if (_stricmp(key, "cavern_alt") == 0) { *field = 63; *group = 2; *formation = 25; return true; }
        if (_stricmp(key, "remiem") == 0) { *field = 24; *group = 0; *formation = 0; return true; }
        return false;
    }
    return false;
}

static void InitScenariosForCombo(int combo) {
    g_scenarioCount = 0;
    g_scenarioSelected = 0;
    struct ScenarioRow { const char* key; const char* label; };
    static const ScenarioRow kX3[] = {
        { "macalania_forest", "Macalania Forest" },
        { "macalania_open", "Macalania Open" },
        { "macalania_open2", "Macalania Open 2" },
        { "remiem", "Mushroom Rock Road" },
    };
    static const ScenarioRow kX4[] = {
        { "cavern", "Calm Lands Cavern" },
        { "bikanel", "Bikanel Desert" },
        { "remiem", "Mushroom Rock Road" },
    };
    static const ScenarioRow kX5[] = {
        { "cavern", "Calm Lands Cavern (wide)" },
        { "cavern_alt", "Calm Lands (alt)" },
        { "remiem", "Mushroom Rock Road" },
    };
    const ScenarioRow* rows = nullptr;
    int rowCount = 0;
    switch (combo) {
        case 5: rows = kX3; rowCount = 4; g_scenarioSelected = 2; break; /* default Macalania Open 2 */
        case 6: rows = kX4; rowCount = 3; break;
        case 7: rows = kX5; rowCount = 3; break;
        default: return;
    }
    for (int i = 0; i < rowCount && i < kScenarioSlotMax; ++i) {
        lstrcpynA(g_scenarioKeys[i], rows[i].key, 32);
        lstrcpynA(g_scenarioLabels[i], rows[i].label, 48);
        ++g_scenarioCount;
    }
}

static int ScenarioIndexForKey(const char* key) {
    if (!key || !key[0]) return 0;
    for (int i = 0; i < g_scenarioCount; ++i) {
        if (_stricmp(g_scenarioKeys[i], key) == 0)
            return i;
    }
    return 0;
}

static const char* SelectedScenarioKey() {
    if (g_scenarioSelected >= 0 && g_scenarioSelected < g_scenarioCount)
        return g_scenarioKeys[g_scenarioSelected];
    return g_scenarioCount > 0 ? g_scenarioKeys[0] : "macalania_open2";
}

static const char* LaunchScenarioKey() {
    if (g_pendingLaunchScenarioKey[0]) return g_pendingLaunchScenarioKey;
    return SelectedScenarioKey();
}

static void StashPendingLaunchScenario() {
    lstrcpynA(g_pendingLaunchScenarioKey, SelectedScenarioKey(), static_cast<int>(sizeof(g_pendingLaunchScenarioKey)));
}

static int GilCostForSelectedPicks() {
    const char* keys[8] = {};
    int n = 0;
    for (int i = 0; i < kPickCount; ++i) {
        if (g_selected[i])
            keys[n++] = kPickKeys[i];
    }
    return ArenaPlus_GilCostSumPickKeys(keys, n);
}

static int GilCostForLastMixPicks() {
    const char* keys[8] = {};
    for (int i = 0; i < g_lastMixPickCount && i < 8; ++i)
        keys[i] = g_lastMixPicks[i];
    return ArenaPlus_GilCostSumPickKeys(keys, g_lastMixPickCount);
}

static bool GilCanAfford(int cost) {
    if (cost <= 0 || !ArenaPlus_IsChargeGilEnabled()) return true;
    uint32_t gil = 0, st = 0, err = 0;
    if (!ArenaPlus_ReadGilForCompose(&gil, &st, &err) || st != FFXPROBE_ST_OK) {
        /* fallback: allow launch path to fail with same check in dllmain */
        return true;
    }
    return gil >= static_cast<uint32_t>(cost);
}

static void LogLine(const char* fmt, ...) {
    if (!g_log) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log("%s", line);
}

static bool EnvFlagEnabled(const char* name) {
    char value[16] = {};
    const DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
                       value[0] == 't' || value[0] == 'T');
}

static bool ModuleRelativePath(const char* relativePath, char* outPath, size_t outPathSize) {
    if (!g_module || !relativePath || !relativePath[0] || !outPath || outPathSize == 0) return false;
    char modulePath[MAX_PATH] = {};
    if (GetModuleFileNameA(g_module, modulePath, sizeof(modulePath)) == 0) return false;
    char* slash = strrchr(modulePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    _snprintf_s(outPath, outPathSize, _TRUNCATE, "%s%s", modulePath, relativePath);
    return true;
}

static bool ModuleFileExists(const char* relativePath) {
    char path[MAX_PATH] = {};
    if (!ModuleRelativePath(relativePath, path, sizeof(path))) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool GameRootDirectoryPath(char* outPath, size_t outPathSize) {
    if (!outPath || outPathSize == 0) return false;
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(GetModuleHandleA(nullptr), exePath, sizeof(exePath)) == 0) return false;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    lstrcpynA(outPath, exePath, static_cast<int>(outPathSize));
    return true;
}

static bool ReadOneLineConfig(const char* relativePath, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    char path[MAX_PATH] = {};
    if (!ModuleRelativePath(relativePath, path, sizeof(path))) return false;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return false;
    char line[512] = {};
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = '\0';
    size_t start = 0;
    while (line[start] == ' ' || line[start] == '\t') ++start;
    if (line[start] == '#' || line[start] == '\0') return false;
    lstrcpynA(out, line + start, static_cast<int>(outSize));
    return out[0] != '\0';
}

static bool PathLooksLikeDirectory(const char* path) {
    if (!path || !path[0]) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool ResolveVanillaBtlRoot(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    char env[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_VANILLA_BTL", env, sizeof(env)) > 0 && PathLooksLikeDirectory(env)) {
        lstrcpynA(out, env, static_cast<int>(outSize));
        return true;
    }
    if (ReadOneLineConfig("config\\arena_plus_compose_vanilla_btl.txt", out, outSize) && PathLooksLikeDirectory(out))
        return true;
    if (ReadOneLineConfig("arena_plus_compose_vanilla_btl.txt", out, outSize) && PathLooksLikeDirectory(out))
        return true;
    return false;
}

static bool ResolveModBtlRoot(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    char env[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_MOD_BTL", env, sizeof(env)) > 0 && PathLooksLikeDirectory(env)) {
        lstrcpynA(out, env, static_cast<int>(outSize));
        return true;
    }
    if (ReadOneLineConfig("config\\arena_plus_compose_mod_btl.txt", out, outSize) && PathLooksLikeDirectory(out))
        return true;
    if (ReadOneLineConfig("arena_plus_compose_mod_btl.txt", out, outSize) && PathLooksLikeDirectory(out))
        return true;

    char gameRoot[MAX_PATH] = {};
    if (!GameRootDirectoryPath(gameRoot, sizeof(gameRoot))) return false;
    _snprintf_s(out, outSize, _TRUNCATE,
        "%sdata\\mods\\ffx_ps2\\ffx\\master\\jppc\\battle\\btl", gameRoot);
    return PathLooksLikeDirectory(out);
}

static bool PathFileExistsA(const char* path);

static bool ResolveLabExe(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    char env[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_COMPOSE_LAB", env, sizeof(env)) > 0) {
        const DWORD attr = GetFileAttributesA(env);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            lstrcpynA(out, env, static_cast<int>(outSize));
            return true;
        }
    }
    if (ReadOneLineConfig("config\\arena_plus_compose_lab.txt", out, outSize) && PathFileExistsA(out))
        return true;
    if (ReadOneLineConfig("arena_plus_compose_lab.txt", out, outSize) && PathFileExistsA(out))
        return true;

    const char* candidates[] = {
        "..\\data\\modules\\tools\\ArenaMultiBossLab\\ArenaMultiBossLab.exe",   // padrao tools do usuario (2026-08-02)
        "tools\\ArenaMultiBossLab\\ArenaMultiBossLab.exe",
        "ArenaMultiBossLab\\ArenaMultiBossLab.exe",
        "ArenaMultiBossLab.exe",
        "tools\\ArenaMultiBossLab.exe",
        "config\\ArenaMultiBossLab.exe",
    };
    for (const char* rel : candidates) {
        char path[MAX_PATH] = {};
        if (!ModuleRelativePath(rel, path, sizeof(path))) continue;
        if (PathFileExistsA(path)) {
            lstrcpynA(out, path, static_cast<int>(outSize));
            return true;
        }
    }
    return false;
}

static int CountSelectedActors() {
    int count = 0;
    for (int i = 0; i < kPickCount; ++i) {
        if (!g_selected[i]) continue;
        count += g_selected[i] * ((i == kMagusPickIndex) ? 3 : 1);
    }
    return count;
}

static void ClearPickState() {
    memset(g_selected, 0, sizeof(g_selected));
    memset(g_pickOrder, 0, sizeof(g_pickOrder));
    g_pickOrderSeq = 0;
}

static bool BuildPickCsv(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    bool first = true;
    for (int order = 1; order <= g_pickOrderSeq; ++order) {
        for (int i = 0; i < kPickCount; ++i) {
            if (!g_selected[i] || g_pickOrder[i] != order) continue;
            for (int rep = 0; rep < g_selected[i]; ++rep) {
                if (!first) strncat_s(out, outSize, ",", _TRUNCATE);
                strncat_s(out, outSize, kPickKeys[i], _TRUNCATE);
                first = false;
            }
            break;
        }
    }
    if (first) {
        for (int i = 0; i < kPickCount; ++i) {
            if (!g_selected[i]) continue;
            for (int rep = 0; rep < g_selected[i]; ++rep) {
                if (!first) strncat_s(out, outSize, ",", _TRUNCATE);
                strncat_s(out, outSize, kPickKeys[i], _TRUNCATE);
                first = false;
            }
        }
    }
    return !first;
}

static void BuildPickSummary(char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    char csv[256] = {};
    if (!BuildPickCsv(csv, sizeof(csv))) return;
    _snprintf_s(out, outSize, _TRUNCATE, "%s", csv);
}

static bool PathFileExistsA(const char* path) {
    if (!path || !path[0]) return false;
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static const char* CarrierBattleIdForCombo(int combo) {
    switch (combo) {
        case 5: return "mcyt00_22";
        case 6: return "nagi05_23";
        case 7: return "nagi05_22";
        default: return nullptr;
    }
}

static const char* JsonFindKey(const char* json, const char* key) {
    if (!json || !key) return nullptr;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    return strstr(json, needle);
}

static bool JsonReadIntAfterKey(const char* json, const char* key, int* out) {
    if (!json || !key || !out) return false;
    const char* p = JsonFindKey(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    *out = atoi(p);
    return true;
}

static bool JsonReadQuotedStringAfterKey(const char* json, const char* key, char* out, size_t outSize) {
    if (!json || !key || !out || outSize == 0) return false;
    out[0] = '\0';
    const char* p = JsonFindKey(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') return false;
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outSize) {
        if (*p == '\\' && p[1]) ++p;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static bool JsonReadPickArray(const char* json, char items[][32], int maxItems, int* countOut) {
    if (!json || !items || !countOut) return false;
    *countOut = 0;
    const char* p = JsonFindKey(json, "picks");
    if (!p) return false;
    p = strchr(p, '[');
    if (!p) return false;
    ++p;
    int count = 0;
    while (*p && *p != ']' && count < maxItems) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
        if (*p == ']') break;
        if (*p != '"') {
            ++p;
            continue;
        }
        ++p;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < 32) {
            if (*p == '\\' && p[1]) ++p;
            items[count][i++] = *p++;
        }
        items[count][i] = '\0';
        if (i > 0) ++count;
        if (*p == '"') ++p;
    }
    *countOut = count;
    return count > 0;
}

static bool ResolveManifestPath(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    char env[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_COMPOSE_MANIFEST", env, sizeof(env)) > 0 &&
        PathFileExistsA(env)) {
        lstrcpynA(out, env, static_cast<int>(outSize));
        return true;
    }
    if (ReadOneLineConfig("config\\arena_plus_compose_manifest.txt", out, outSize) && PathFileExistsA(out))
        return true;
    if (ReadOneLineConfig("arena_plus_compose_manifest.txt", out, outSize) && PathFileExistsA(out))
        return true;

    if (ModuleRelativePath("compose_last.json", out, outSize) && PathFileExistsA(out))
        return true;

    char gameRoot[MAX_PATH] = {};
    if (GameRootDirectoryPath(gameRoot, sizeof(gameRoot))) {
        _snprintf_s(out, outSize, _TRUNCATE, "%scompose_last.json", gameRoot);
        if (PathFileExistsA(out)) return true;
        _snprintf_s(out, outSize, _TRUNCATE,
            "%smods\\Spira Reforge\\arena\\compose_last.json", gameRoot);
        if (PathFileExistsA(out)) return true;
    }
    return false;
}

static bool ReadManifestLaunchRoute(
    int combo,
    int* field,
    int* group,
    int* formation,
    char* backdropBattleId,
    int backdropBattleIdCap,
    int* backdropBattlefieldId) {
    if (!field || !group || !formation) return false;
    if (!ArenaPlusComposePick_IsCustomMixCombo(combo)) return false;

    char manifestPath[MAX_PATH * 2] = {};
    if (!ResolveManifestPath(manifestPath, sizeof(manifestPath))) return false;

    FILE* f = nullptr;
    if (fopen_s(&f, manifestPath, "rb") != 0 || !f) return false;
    char json[8192] = {};
    const size_t read = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    if (read == 0) return false;
    json[read] = '\0';

    char battleId[32] = {};
    int manifestField = 0;
    int manifestGroup = 0;
    int manifestFormation = 0;
    if (!JsonReadQuotedStringAfterKey(json, "battle_id", battleId, sizeof(battleId))) return false;
    if (!JsonReadIntAfterKey(json, "field", &manifestField)) return false;
    if (!JsonReadIntAfterKey(json, "group", &manifestGroup)) return false;
    if (!JsonReadIntAfterKey(json, "formation", &manifestFormation)) return false;

    const char* expectedCarrier = CarrierBattleIdForCombo(combo);
    char templateId[32] = {};
    const bool hasTemplate = JsonReadQuotedStringAfterKey(json, "source_template_id", templateId, sizeof(templateId));
    if (expectedCarrier && _stricmp(battleId, expectedCarrier) != 0) {
        if (!hasTemplate || _stricmp(battleId, templateId) != 0)
            return false;
    }

    /* BUGFIX 2026-08-05 (Jarvis-HOOK): When battleId == carrier (nagi05_23/mcyt00_22),
     * scenario_key resolves the REAL backdrop (bika02_01, kino00_00) for the cross-map
     * token route via ArenaPlus_BuildScenarioLaunchRoute. Otherwise the compose writes
     * to the carrier bin but the hook tries to launch the scenario bin (vanilla). */
    char scenarioBackdropId[32] = {};
    {
        char scenarioKey[32] = {};
        if (JsonReadQuotedStringAfterKey(json, "scenario_key", scenarioKey, sizeof(scenarioKey)))
            ScenarioBackdropBattleIdForKey(combo, scenarioKey, scenarioBackdropId, sizeof(scenarioBackdropId));
    }

    *field = manifestField;
    *group = manifestGroup;
    *formation = manifestFormation;
    if (scenarioBackdropId[0]) {
        CopyBackdropBattleIdOptional(scenarioBackdropId, backdropBattleId, backdropBattleIdCap);
    } else {
        CopyBackdropBattleIdOptional(battleId[0] ? battleId : templateId, backdropBattleId, backdropBattleIdCap);
    }
    if (backdropBattlefieldId) {
        int manifestBf = 0;
        if (JsonReadIntAfterKey(json, "battlefield_id", &manifestBf) && manifestBf > 0) {
            *backdropBattlefieldId = manifestBf;
        } else {
            char scenarioKey[32] = {};
            if (JsonReadQuotedStringAfterKey(json, "scenario_key", scenarioKey, sizeof(scenarioKey)))
                ScenarioBattlefieldIdForKey(scenarioKey, backdropBattlefieldId);
            else
                *backdropBattlefieldId = 0;
        }
    }
    LogLine("[ffx-hooks] ArenaPlus compose: launch route from manifest field=%d group=%d formation=%d backdrop=%s bf=%d (%s)\n",
        manifestField,
        manifestGroup,
        manifestFormation,
        templateId[0] ? templateId : "?",
        backdropBattlefieldId ? *backdropBattlefieldId : 0,
        manifestPath);
    return true;
}

static bool ResolveComposeManifestOut(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    char env[MAX_PATH * 2] = {};
    if (GetEnvironmentVariableA("FFXHOOKS_ARENAPLUS_COMPOSE_MANIFEST", env, sizeof(env)) > 0 && env[0]) {
        lstrcpynA(out, env, static_cast<int>(outSize));
        return true;
    }
    if (ReadOneLineConfig("config\\arena_plus_compose_manifest.txt", out, outSize) && out[0])
        return true;
    if (ReadOneLineConfig("arena_plus_compose_manifest.txt", out, outSize) && out[0])
        return true;
    return ModuleRelativePath("compose_last.json", out, outSize);
}

static bool ResolveDeployBinPath(const char* deployRel, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!deployRel || !deployRel[0]) return false;

    if (deployRel[1] == ':' || (deployRel[0] == '\\' && deployRel[1] == '\\')) {
        lstrcpynA(out, deployRel, static_cast<int>(outSize));
        return PathFileExistsA(out);
    }

    char gameRoot[MAX_PATH] = {};
    if (!GameRootDirectoryPath(gameRoot, sizeof(gameRoot))) return false;
    _snprintf_s(out, outSize, _TRUNCATE, "%s%s", gameRoot, deployRel);
    if (PathFileExistsA(out)) return true;

    char modRoot[MAX_PATH * 2] = {};
    const char* carrier = g_lastMixBattleId[0] ? g_lastMixBattleId : "carrier";
    if (ResolveModBtlRoot(modRoot, sizeof(modRoot))) {
        _snprintf_s(out, outSize, _TRUNCATE, "%s\\%s\\%s.bin", modRoot, carrier, carrier);
        return PathFileExistsA(out);
    }
    return false;
}

static int PickIndexForKey(const char* key) {
    if (!key) return -1;
    for (int i = 0; i < kPickCount; ++i) {
        if (_stricmp(kPickKeys[i], key) == 0) return i;
    }
    return -1;
}

static void ApplyPicksFromKeys(const char keys[][32], int keyCount) {
    ClearPickState();
    for (int i = 0; i < keyCount; ++i) {
        const int idx = PickIndexForKey(keys[i]);
        if (idx >= 0) {
            g_selected[idx] = 1;
            g_pickOrder[idx] = ++g_pickOrderSeq;
        }
    }
}

static void ReloadLastMixManifest() {
    g_lastMixValid = false;
    g_lastMixBinReady = false;
    g_lastMixActorCount = 0;
    g_lastMixPickCount = 0;
    g_lastMixBattleId[0] = '\0';
    g_lastMixScenarioKey[0] = '\0';
    g_lastMixSummary[0] = '\0';

    char manifestPath[MAX_PATH * 2] = {};
    if (!ResolveManifestPath(manifestPath, sizeof(manifestPath))) return;

    FILE* f = nullptr;
    if (fopen_s(&f, manifestPath, "rb") != 0 || !f) return;
    char json[8192] = {};
    const size_t read = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    if (read == 0) return;
    json[read] = '\0';

    int actorCount = 0;
    char battleId[32] = {};
    char deployPath[MAX_PATH * 2] = {};
    char picks[8][32] = {};
    int pickCount = 0;
    if (!JsonReadIntAfterKey(json, "actor_count", &actorCount)) return;
    if (!JsonReadQuotedStringAfterKey(json, "battle_id", battleId, sizeof(battleId))) return;
    if (!JsonReadPickArray(json, picks, 8, &pickCount)) return;
    JsonReadQuotedStringAfterKey(json, "deploy_path", deployPath, sizeof(deployPath));
    char scenarioKey[32] = {};
    JsonReadQuotedStringAfterKey(json, "scenario_key", scenarioKey, sizeof(scenarioKey));

    const char* expectedCarrier = CarrierBattleIdForCombo(g_targetCombo);
    if (!expectedCarrier || actorCount != g_requiredActors || _stricmp(battleId, expectedCarrier) != 0)
        return;

    lstrcpynA(g_lastMixBattleId, battleId, static_cast<int>(sizeof(g_lastMixBattleId)));
    g_lastMixActorCount = actorCount;
    g_lastMixPickCount = pickCount;
    for (int i = 0; i < pickCount && i < 8; ++i)
        lstrcpynA(g_lastMixPicks[i], picks[i], 32);
    if (scenarioKey[0])
        lstrcpynA(g_lastMixScenarioKey, scenarioKey, static_cast<int>(sizeof(g_lastMixScenarioKey)));

    char pickSummary[96] = {};
    for (int i = 0; i < pickCount; ++i) {
        if (i > 0) strncat_s(pickSummary, sizeof(pickSummary), ",", _TRUNCATE);
        strncat_s(pickSummary, sizeof(pickSummary), picks[i], _TRUNCATE);
    }
    _snprintf_s(g_lastMixSummary, sizeof(g_lastMixSummary), _TRUNCATE, "%s", pickSummary);

    char binPath[MAX_PATH * 2] = {};
    if (deployPath[0] && ResolveDeployBinPath(deployPath, binPath, sizeof(binPath)))
        g_lastMixBinReady = true;
    else {
        char modRoot[MAX_PATH * 2] = {};
        if (ResolveModBtlRoot(modRoot, sizeof(modRoot))) {
            _snprintf_s(binPath, sizeof(binPath), _TRUNCATE, "%s\\%s\\%s.bin", modRoot, battleId, battleId);
            g_lastMixBinReady = PathFileExistsA(binPath);
        }
    }

    g_lastMixValid = true;
    LogLine("[ffx-hooks] ArenaPlus compose: last mix %s (%d) bin=%d path=%s\n",
        g_lastMixSummary, g_lastMixActorCount, g_lastMixBinReady ? 1 : 0, manifestPath);
}

static bool AdjustPickAllowed(int pickIndex, int delta) {
    // delta: +1 = increment, -1 = decrement, 0 = just check
    if (delta <= 0) return true;
    const int mul = (pickIndex == kMagusPickIndex) ? 3 : 1;
    return CountSelectedActors() + delta * mul <= g_requiredActors;
}

static void RenumberPickOrder() {
    int n = 0;
    for (int i = 0; i < kPickCount; ++i) {
        if (g_selected[i]) g_pickOrder[i] = ++n;
        else g_pickOrder[i] = 0;
    }
    g_pickOrderSeq = n;
}

static void RefreshLabels() {
    const int actors = CountSelectedActors();
    const bool chargeGil = ArenaPlus_IsChargeGilEnabled();
    const int mixCost = GilCostForSelectedPicks();
    const int lastCost = GilCostForLastMixPicks();
    char pickSummary[96] = {};
    BuildPickSummary(pickSummary, sizeof(pickSummary));
    for (int i = 0; i < g_scenarioCount; ++i) {
        _snprintf_s(g_labels[i], kLabelCap, _TRUNCATE, "%s Scenario: %s",
            (i == g_scenarioSelected) ? "[X]" : "[ ]", g_scenarioLabels[i]);
    }
    for (int i = g_scenarioCount; i < kMaxRowCount; ++i)
        g_labels[i][0] = '\0';
    const int pickFirst = ComposePick_PickFirst();
    for (int i = 0; i < kPickCount; ++i) {
        const int row = pickFirst + i;
        const int rowCost = chargeGil ? ArenaPlus_GilCostForPickKey(kPickKeys[i]) : 0;
        char mark[8];
        if (g_selected[i] == 0) strcpy_s(mark, sizeof(mark), "[ ]");
        else if (g_selected[i] == 1) strcpy_s(mark, sizeof(mark), "[X]");
        else _snprintf_s(mark, sizeof(mark), _TRUNCATE, "[X%d]", g_selected[i]);
        if (rowCost > 0) {
            _snprintf_s(g_labels[row], kLabelCap, _TRUNCATE, "%s %s %dG",
                mark, kPickLabels[i], rowCost);
        } else {
            _snprintf_s(g_labels[row], kLabelCap, _TRUNCATE, "%s %s",
                mark, kPickLabels[i]);
        }
    }
    _snprintf_s(g_labels[ComposePick_RowRelaunch()], kLabelCap, _TRUNCATE,
        g_lastMixValid ? "Last Mix: %s" : "Last Mix (none)", g_lastMixSummary);
    if (g_lastMixValid && g_lastMixBinReady) {
        _snprintf_s(g_labels[ComposePick_RowRelaunch()], kLabelCap, _TRUNCATE,
        (chargeGil && lastCost > 0) ? "Fight Last Mix COST %dG (L=load)" : "Fight Last Mix (L=load picks)");
    } else {
        _snprintf_s(g_labels[ComposePick_RowRelaunch()], kLabelCap, _TRUNCATE, "Fight Last Mix (need bin)");
    }
    _snprintf_s(g_labels[ComposePick_RowBuildPreview()], kLabelCap, _TRUNCATE,
        (g_hasBuiltThisSession || g_previewCount > 0)
            ? "Build Preview (E / L2-LT to Edit)"
            : "Build Preview  (First Build - Press Enter)");
    _snprintf_s(g_labels[ComposePick_RowFormations()], kLabelCap, _TRUNCATE,
        "Formations  (Enter=save  L=import)");
    if (chargeGil && mixCost > 0 && actors == g_requiredActors) {
        _snprintf_s(g_labels[ComposePick_RowLaunch()], kLabelCap, _TRUNCATE,
            "Build + Launch COST %dG (%d/%d)", mixCost, actors, g_requiredActors);
    } else {
        _snprintf_s(g_labels[ComposePick_RowLaunch()], kLabelCap, _TRUNCATE,
            "Build + Launch: %s (%d/%d)", pickSummary[0] ? pickSummary : "...", actors, g_requiredActors);
    }
    _snprintf_s(g_labels[ComposePick_RowBack()], kLabelCap, _TRUNCATE, "Back");
    for (int i = 0; i < ComposePick_TotalRows(); ++i)
        NativeMenu::EncodeLabel(g_labels[i], g_labelBytes[i], kLabelCap);
}

static int RequiredActorsForCombo(int combo) {
    switch (combo) {
        case 5: return 3;
        case 6: return 4;
        case 7: return 5;
        default: return 0;
    }
}

struct ComposeJobArgs {
    char labExe[MAX_PATH];
    char pickCsv[256];
    char scenarioKey[32];
    char vanillaRoot[MAX_PATH * 2];
    char modRoot[MAX_PATH * 2];
    char manifestOut[MAX_PATH * 2];
    char layoutProfile[MAX_PATH * 2];
};

static DWORD WINAPI ComposeJobThread(LPVOID param) {
    ComposeJobArgs* args = static_cast<ComposeJobArgs*>(param);
    if (!args) {
        InterlockedExchange(&g_jobState, JOB_FAIL);
        lstrcpynA(g_jobError, "compose job alloc failed", static_cast<int>(sizeof(g_jobError)));
        return 1;
    }

    char cmdline[4096] = {};
    _snprintf_s(cmdline, sizeof(cmdline), _TRUNCATE,
        "\"%s\" --compose --pick %s --scenario %s --vanilla-root \"%s\" --mod-root \"%s\" --positions-root \"%s\" --manifest-out \"%s\" --auto-layout --layout-profile \"%s\"",
        args->labExe, args->pickCsv, args->scenarioKey, args->vanillaRoot, args->modRoot, g_positionsPath, args->manifestOut, args->layoutProfile);

    LogLine("[ffx-hooks] ArenaPlus compose: %s\n", cmdline);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    char mutableCmd[4096] = {};
    lstrcpynA(mutableCmd, cmdline, static_cast<int>(sizeof(mutableCmd)));

    /* Fase 2 â€” captura o stdout do lab (o mapa PREV|) via pipe. */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (CreatePipe(&hRead, &hWrite, &sa, 0) && hRead && hWrite) {
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
        si.hStdOutput = hWrite;
        si.hStdError = hWrite;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    char workDir[MAX_PATH] = {};
    lstrcpynA(workDir, args->labExe, MAX_PATH);
    char* workSlash = strrchr(workDir, '\\');
    if (workSlash) *(workSlash + 1) = '\0';

    const BOOL created = CreateProcessA(
        args->labExe, mutableCmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, workDir[0] ? workDir : nullptr, &si, &pi);

    if (hWrite) CloseHandle(hWrite);
    if (hRead) {
        char outBuf[16384] = {};
        DWORD rd = 0, total = 0;
        while (total < sizeof(outBuf) - 1 &&
               ReadFile(hRead, outBuf + total, (DWORD)(sizeof(outBuf) - 1 - total), &rd, nullptr) && rd > 0)
            total += rd;
        CloseHandle(hRead);
        outBuf[total] = '\0';
        /* Link log (custom positions): detect whether the runner applied the custom grid or the auto-layout. */
        if (strstr(outBuf, "[custom-positions]"))
            LogLine("[ffx-hooks] ArenaPlus compose: CUSTOM POSITIONS aplicado pelo runner (grid do hook)");
        else if (strstr(outBuf, "[auto-layout]"))
            LogLine("[ffx-hooks] ArenaPlus compose: auto-layout (sem custom positions) -> json=%s",
                g_positionsPath[0] ? g_positionsPath : "(nenhum)");

        /* extrai as linhas do mapa (prefixo PREV|) â€” thread do job so; render le via g_previewCount. */
        int n = 0;
        char* line = outBuf;
        while (line && *line && n < 12) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            char* eol = line + strlen(line);
            while (eol > line && (eol[-1] == '\r' || eol[-1] == ' ')) *--eol = '\0';
            if (strncmp(line, "PREV|", 5) == 0 && line[5] == ' ') {
                lstrcpynA(g_previewLines[n], line + 6, 64);
                ++n;
            }
            line = nl ? nl + 1 : nullptr;
        }
        InterlockedExchange(&g_previewCount, n);
        g_previewEncoded = -1;

        /* exporta o preview para o disco â€” o usuario/agentes leem depois (modules\compose_preview_last.txt). */
        char previewPath[MAX_PATH * 2] = {};
        if (n > 0 && ModuleRelativePath("compose_preview_last.txt", previewPath, sizeof(previewPath))) {
            FILE* pf = nullptr;
            if (fopen_s(&pf, previewPath, "w") == 0 && pf) {
                fprintf(pf, "# compose layout preview â€” %s @ %s\n", g_jobPickCsv, args->scenarioKey);
                for (int i = 0; i < n; ++i)
                    fprintf(pf, "%s\n", g_previewLines[i]);
                fclose(pf);
                LogLine("[ffx-hooks] ArenaPlus compose: preview exportado -> %s\n", previewPath);
            }
        }
    }

    if (!created) {
        _snprintf_s(g_jobError, sizeof(g_jobError), _TRUNCATE,
            "CreateProcess failed err=0x%08X", GetLastError());
        LogLine("[ffx-hooks] ArenaPlus compose: %s\n", g_jobError);
        HeapFree(GetProcessHeap(), 0, args);
        InterlockedExchange(&g_jobExitCode, 1);
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    HeapFree(GetProcessHeap(), 0, args);

    InterlockedExchange(&g_jobExitCode, static_cast<LONG>(exitCode));
    if (exitCode == 0) {
        InterlockedExchange(&g_jobState, JOB_OK);
        LogLine("[ffx-hooks] ArenaPlus compose: OK picks=%s\n", g_jobPickCsv);
    } else {
        if (exitCode == 0x800080BAu || exitCode == 0x800080BCu) {
            _snprintf_s(g_jobError, sizeof(g_jobError), _TRUNCATE,
                "lab missing .NET files â€” redeploy tools\\ArenaMultiBossLab\\ folder");
        } else {
            _snprintf_s(g_jobError, sizeof(g_jobError), _TRUNCATE,
                "compose exit code %lu (check %%TEMP%%\\ffx-hooks.log + lab output)", exitCode);
        }
        InterlockedExchange(&g_jobState, JOB_FAIL);
        LogLine("[ffx-hooks] ArenaPlus compose: FAILED exit=%lu picks=%s\n", exitCode, g_jobPickCsv);
    }
    return 0;
}

static float FindJsonNumber(const char* json, const char* key, float def) {
    if (!json || !key) return def;
    char needle[96];
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p, ':');
    if (!p) return def;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return (float)atof(p);
}

static void ResolveProfilePath() {
    if (g_profilePath[0] != 0) return;
    if (!ModuleRelativePath("arena_layout_profiles.json", g_profilePath, sizeof(g_profilePath)))
        lstrcpynA(g_profilePath, "", sizeof(g_profilePath));
}

static void LoadLayoutProfiles() {
    memset(g_scenProfile, 0, sizeof(g_scenProfile));
    ResolveProfilePath();
    if (g_profilePath[0] == 0 || !PathFileExistsA(g_profilePath)) return;
    char buf[8192] = {};
    FILE* f = nullptr;
    if (fopen_s(&f, g_profilePath, "rb") != 0 || !f) return;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return;
    buf[n] = '\0';
    for (int i = 0; i < g_scenarioCount && i < kScenarioSlotMax; ++i) {
        const char* blk = strstr(buf, g_scenarioKeys[i]);
        if (!blk) continue;   /* cenario sem bloco = default (zeros/uns) */
        g_scenProfile[i][0] = FindJsonNumber(blk, "cam_elev_offset_deg", 0.0f);
        g_scenProfile[i][1] = FindJsonNumber(blk, "cam_dist_scale", 1.0f);
        g_scenProfile[i][2] = FindJsonNumber(blk, "mon_spread_scale", 1.0f);
        g_scenProfile[i][3] = FindJsonNumber(blk, "mon_shift_forward", 0.0f);
        g_scenProfile[i][4] = FindJsonNumber(blk, "mon_dist_base", 105.0f);
        g_scenProfile[i][5] = FindJsonNumber(blk, "mon_arc_deg", 70.0f);
        g_scenProfile[i][6] = FindJsonNumber(blk, "layout_mode_pattern", 0.0f);
    }
}

static void SaveLayoutProfiles() {
    ResolveProfilePath();
    if (g_profilePath[0] == 0) return;
    FILE* f = nullptr;
    if (fopen_s(&f, g_profilePath, "w") != 0 || !f) {
        if (f) fclose(f);
        return;
    }
    fprintf(f, "{\n");
    for (int i = 0; i < g_scenarioCount && i < kScenarioSlotMax; ++i) {
        fprintf(f,
            "  \"%s\": { \"cam_elev_offset_deg\": %.1f, \"cam_dist_scale\": %.2f, \"mon_spread_scale\": %.2f, \"mon_shift_forward\": %.1f, \"mon_dist_base\": %.0f, \"mon_arc_deg\": %.0f, \"layout_mode_pattern\": %.0f }%s\n",
            g_scenarioKeys[i],
            g_scenProfile[i][0], g_scenProfile[i][1], g_scenProfile[i][2], g_scenProfile[i][3],
            g_scenProfile[i][4], g_scenProfile[i][5], g_scenProfile[i][6],
            (i == g_scenarioCount - 1) ? "" : ",");
    }
    fprintf(f, "  \"_default\": { \"cam_elev_offset_deg\": 0, \"cam_dist_scale\": 1, \"mon_spread_scale\": 1, \"mon_shift_forward\": 0, \"mon_dist_base\": 105, \"mon_arc_deg\": 70, \"layout_mode_pattern\": 0 }\n");
    fprintf(f, "}\n");
    fclose(f);
    LogLine("[ffx-hooks] ArenaPlus layout profile saved: %s\n", g_profilePath);
}

static void ResolvePositionsPath() {
    if (g_positionsPath[0] != 0) return;
    if (!ModuleRelativePath("arena_positions.json", g_positionsPath, sizeof(g_positionsPath)))
        lstrcpynA(g_positionsPath, "", sizeof(g_positionsPath));
}

// WHY (live position edit, 2026-08-03): grava o grid editado (gx/gy de each creature) em JSON no modules.
// The compose runner (ArenaMultiBossLab --compose --positions) reads this file and applies it
// BattleArenaPositionWriter (C#) no bin -> a batalha inicia com o posicionamento custom.
static bool SavePositions() {
    ResolvePositionsPath();
    if (g_positionsPath[0] == 0 || g_edBichos == 0) return false;
    // LINK stale-grid (2026-08-04): if the user did NOT edit anything (delta 0 on every bicho),
    // do NOT keep/overwrite a stale arena_positions.json. Otherwise the FIRST compose of a new
    // session would still apply old edits from a previous session (the "first build looks wrong").
    // When nothing changed, delete the stale JSON -> the runner falls back to the auto-layout
    // (the software's own preview) exactly as if the user never touched it.
    bool edited = false;
    for (int i = 0; i < g_edBichos; ++i)
        if (g_edPos[i][0] != g_edPosOrig[i][0] || g_edPos[i][1] != g_edPosOrig[i][1]) { edited = true; break; }
    // camera considered too: if the user moved the CAM point (gx/gy) or changed yaw/pitch/zoom,
    // keep the JSON (the camera is edited even if no monster moved).
    bool camEdited =
        g_edCam[0] != g_edCamOrig[0] || g_edCam[1] != g_edCamOrig[1] ||
        g_edCamYaw != g_edCamYawOrig || g_edCamPitch != g_edCamPitchOrig || g_edCamZoom != g_edCamZoomOrig;
    if (!edited && !camEdited) {
        DeleteFileA(g_positionsPath);
        LogLine("[ffx-hooks] ArenaPlus positions: no edits (monsters nor camera) - removed stale grid (auto-layout will be used)\n");
        return true;
    }
    FILE* f = nullptr;
    if (fopen_s(&f, g_positionsPath, "w") != 0 || !f) { if (f) fclose(f); return false; }
    fprintf(f, "{\n  \"grid_w\": 16, \"grid_h\": 8,\n  \"bichos\": [\n");
    for (int i = 0; i < g_edBichos; ++i)
        fprintf(f, "    { \"gx\": %d, \"gy\": %d, \"ogx\": %d, \"ogy\": %d }%s\n",
            g_edPos[i][0], g_edPos[i][1], g_edPosOrig[i][0], g_edPosOrig[i][1],
            (i == g_edBichos - 1) ? "" : ",");
    fprintf(f, "  ],\n  \"camera\": { \"gx\": %d, \"gy\": %d, \"yaw\": %d, \"pitch\": %d, \"zoom\": %d }\n}\n",
            g_edCam[0], g_edCam[1], g_edCamYaw, g_edCamPitch, g_edCamZoom);
    fclose(f);
    LogLine("[ffx-hooks] ArenaPlus positions saved: %s (%d bichos) camera=%d,%d\n",
        g_positionsPath, g_edBichos, g_edCam[0], g_edCam[1]);
    return true;
}
/* Local bind helpers (this file cannot see the dllmain.cpp ones - closed scope).
Same RVA/offsets as the Aurora W2S scan: live actor table + X/Y/Z at +0x00C/+0x010/+0x014. */

static const uint32_t RVA_ACTIVE_CHR_TABLE_LOCAL = 0x01FC44E4u; /* VA 0x23C44E4 - 0x400000 */
static const uint32_t RVA_ACTIVE_CHR_COUNT_LOCAL = 0x01FC44E0u; /* VA 0x23C44E0 - 0x400000 */
static const uint32_t ACTIVE_CHR_STRIDE_LOCAL = 0x880u;

static inline uintptr_t HookRva(uintptr_t offset) {
    // IMPORTANT: the RVAs belong to FFX.exe, NOT to ffx-hooks.dll. The dllmain g_base uses
    // GetModuleHandleA("FFX.exe") (dllmain.cpp 11851). Using g_module here would read the wrong
    // address -> garbage table -> write to a random address -> CRASH.
    static const uintptr_t s_ffxBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("FFX.exe"));
    return s_ffxBase + offset;
}
static bool HookPtrOk(uintptr_t p) {
    return p >= 0x10000u && p < 0x7FFF0000u &&
        p != 0xCDCDCDCDu && p != 0xDDDDDDDDu && p != 0xFEEEFEEEu;
}
static bool HookReadBytes(uintptr_t address, void* out, size_t len) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(address), len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(out, 0, len);
        return false;
    }
}
static bool HookReadU32(uintptr_t address, uint32_t* out) {
    return HookReadBytes(address, out, sizeof(uint32_t));
}
static bool HookReadU8(uintptr_t address, uint8_t* out) {
    return HookReadBytes(address, out, sizeof(uint8_t));
}
/* Runtime bind (2026-08-03): writes the edited grid to the LIVE actors in-game (monLive).
   Grid 16x8 -> world: 1 cell = 10 units, center at (gx=7.5, gy=3.5).
   X = (gx - 7.5) * 10, Z = (gy - 3.5) * 10. Y (height) and angle are untouched. */
static bool AuroraWriteFloat(uintptr_t address, float value) {
    if (!HookPtrOk(address)) return false;
    __try {
        *reinterpret_cast<volatile float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void GridXZFromCell(int gx, int gy, float& outX, float& outZ) {
    outX = (static_cast<float>(gx) - 7.5f) * 10.0f;
    outZ = (static_cast<float>(gy) - 3.5f) * 10.0f;
}


/* Camera polar -> grid 16x8 (visual projection of yaw+zoom; pitch does not appear in 2D, only the number). */
static void CamGridFromPolar() {
    const float rad = g_edCamYaw * 3.14159265f / 180.0f;
    float raio = g_edCamZoom / 10.0f;
    if (raio < 1.0f) raio = 1.0f;
    if (raio > 7.0f) raio = 7.0f;
    int gx = static_cast<int>(7.5f + cosf(rad) * raio);
    int gy = static_cast<int>(3.5f + sinf(rad) * raio);
    if (gx < 0) gx = 0;
    if (gx > 15) gx = 15;
    if (gy < 0) gy = 0;
    if (gy > 7) gy = 7;
    g_edCam[0] = gx;
    g_edCam[1] = gy;
}

/* Adjusts the active camera parameter (YAW/PITCH/ZOOM) by one step. */
static void AdjustCamParam(int step) {
    switch (g_camParam) {
        case 0: g_edCamYaw = (g_edCamYaw + step * 5 + 360) % 360; break;
        case 1: g_edCamPitch += step * 3; if (g_edCamPitch < -45) g_edCamPitch = -45; if (g_edCamPitch > 45) g_edCamPitch = 45; break;
        case 2: g_edCamZoom += step * 5; if (g_edCamZoom < 10) g_edCamZoom = 10; if (g_edCamZoom > 80) g_edCamZoom = 80; break;
    }
    CamGridFromPolar();
}

static int ApplyGridToGame() {
    if (g_edBichos == 0) return 0;
    uint32_t table = 0, count = 0;
    if (!HookReadU32(HookRva(RVA_ACTIVE_CHR_TABLE_LOCAL), &table) || !HookPtrOk(table)) return 0;
    if (!HookReadU32(HookRva(RVA_ACTIVE_CHR_COUNT_LOCAL), &count) || count == 0 || count > 256) return 0;
    int applied = 0;
    for (uint32_t idx = 0; idx < count && applied < g_edBichos; ++idx) {
        const uintptr_t inst = static_cast<uintptr_t>(table) + idx * ACTIVE_CHR_STRIDE_LOCAL;
        uint8_t active = 0;
        if (!HookReadU8(inst + 0x002, &active) || active == 0) continue;
        float wx = 0.0f, wz = 0.0f;
        GridXZFromCell(g_edPos[applied][0], g_edPos[applied][1], wx, wz);
        if (!AuroraWriteFloat(inst + 0x00C, wx)) continue;   // X do actor
        if (!AuroraWriteFloat(inst + 0x014, wz)) continue;   // Z do actor
        ++applied;
    }
    return applied;
}


static bool FindJsonString(const char* json, const char* key, char* out, size_t outCap) {
    if (!json || !key || !out || outCap == 0) return false;
    char needle[96];
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\"') ++p;
    size_t i = 0;
    while (*p && *p != '\"' && i + 1 < outCap) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static void ResolveFormationsDir() {
    if (g_formationsDir[0] != 0) return;
    char base[MAX_PATH * 2] = {};
    if (ModuleRelativePath("arena_formations", base, sizeof(base))) {
        _snprintf_s(g_formationsDir, sizeof(g_formationsDir), _TRUNCATE, "%s\\", base);
        CreateDirectoryA(g_formationsDir, nullptr);
    } else {
        lstrcpynA(g_formationsDir, "", sizeof(g_formationsDir));
    }
}

static void FormationFullPath(int index, char* out, size_t outCap) {
    ResolveFormationsDir();
    _snprintf_s(out, outCap, _TRUNCATE, "%s%s", g_formationsDir, g_formationList[index]);
}

static void ScanFormations() {
    ResolveFormationsDir();
    g_formationCount = 0;
    if (g_formationsDir[0] == 0) return;
    char pattern[MAX_PATH * 2] = {};
    _snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%sformation_*.json", g_formationsDir);
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            g_formationCount < kFormationMax) {
            lstrcpynA(g_formationList[g_formationCount], fd.cFileName, 64);
            ++g_formationCount;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static bool SaveFormation() {
    ResolveFormationsDir();
    if (g_formationsDir[0] == 0) return false;
    char src[MAX_PATH * 2] = {};
    if (!ModuleRelativePath("compose_last_layout.json", src, sizeof(src)) || !PathFileExistsA(src))
        return false;
    ScanFormations();
    int next = 1;
    for (int i = 0; i < g_formationCount; ++i) {
        int n = atoi(g_formationList[i] + strlen("formation_"));
        if (n >= next) next = n + 1;
    }
    char dst[MAX_PATH * 2] = {};
    _snprintf_s(dst, sizeof(dst), _TRUNCATE, "%sformation_%03d.json", g_formationsDir, next);
    if (!CopyFileA(src, dst, FALSE)) return false;
    LogLine("[ffx-hooks] ArenaPlus formation saved: %s\n", dst);
    return true;
}

static bool ApplyFormation(int index) {
    char path[MAX_PATH * 2] = {};
    FormationFullPath(index, path, sizeof(path));
    if (!PathFileExistsA(path)) return false;
    char buf[16384] = {};
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;
    buf[n] = '\0';

    char scenKey[32] = {};
    bool hasProfile = FindJsonString(buf, "scenario", scenKey, sizeof(scenKey)) && scenKey[0] != 0;
    /* o bloco do perfil da formacao (se a formacao tinha ajustes ativos) */
    float elev = FindJsonNumber(buf, "cam_elev_offset_deg", 0.0f);
    float dist = FindJsonNumber(buf, "cam_dist_scale", 1.0f);
    float spread = FindJsonNumber(buf, "mon_spread_scale", 1.0f);
    float shift = FindJsonNumber(buf, "mon_shift_forward", 0.0f);

    int scenIdx = hasProfile ? ScenarioIndexForKey(scenKey) : -1;
    if (scenIdx < 0) {
        /* cenario da formacao nao esta no menu deste tier â€” aplica no selecionado */
        scenIdx = g_scenarioSelected;
    }
    g_scenProfile[scenIdx][0] = elev;
    g_scenProfile[scenIdx][1] = dist;
    g_scenProfile[scenIdx][2] = spread;
    g_scenProfile[scenIdx][3] = shift;
    SaveLayoutProfiles();
    InterlockedExchange(&g_previewCount, 0);
    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
        "Formation imported: %s (%s) - Build Preview to see it.", g_formationList[index],
        g_scenarioLabels[scenIdx]);
    return true;
}

static void DeleteFormation(int index) {
    char path[MAX_PATH * 2] = {};
    FormationFullPath(index, path, sizeof(path));
    DeleteFileA(path);
    LogLine("[ffx-hooks] ArenaPlus formation deleted: %s\n", path);
}

static bool StartComposeJob() {
    if (InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_IDLE) != JOB_IDLE &&
        InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_OK) != JOB_OK &&
        InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_FAIL) != JOB_FAIL) {
        return false;
    }

    char labExe[MAX_PATH] = {};
    char vanillaRoot[MAX_PATH * 2] = {};
    char modRoot[MAX_PATH * 2] = {};
    if (!ResolveLabExe(labExe, sizeof(labExe))) {
        lstrcpynA(g_jobError,
            "ArenaMultiBossLab missing â€” need modules\\tools\\ArenaMultiBossLab\\ (run deploy-arena-plus-compose.ps1)",
            static_cast<int>(sizeof(g_jobError)));
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }
    if (!ResolveVanillaBtlRoot(vanillaRoot, sizeof(vanillaRoot))) {
        lstrcpynA(g_jobError, "missing vanilla btl root (config\\arena_plus_compose_vanilla_btl.txt)", static_cast<int>(sizeof(g_jobError)));
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }
    if (!ResolveModBtlRoot(modRoot, sizeof(modRoot))) {
        lstrcpynA(g_jobError, "missing mod btl root", static_cast<int>(sizeof(g_jobError)));
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }
    if (!BuildPickCsv(g_jobPickCsv, sizeof(g_jobPickCsv))) {
        lstrcpynA(g_jobError, "empty pick list", static_cast<int>(sizeof(g_jobError)));
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }

    char manifestOut[MAX_PATH * 2] = {};
    if (!ResolveComposeManifestOut(manifestOut, sizeof(manifestOut))) {
        lstrcpynA(g_jobError, "manifest-out path failed", static_cast<int>(sizeof(g_jobError)));
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }

    ComposeJobArgs* args = static_cast<ComposeJobArgs*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ComposeJobArgs)));
    if (!args) {
        lstrcpynA(g_jobError, "HeapAlloc failed", static_cast<int>(sizeof(g_jobError)));
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }
    lstrcpynA(args->labExe, labExe, MAX_PATH);
    lstrcpynA(args->pickCsv, g_jobPickCsv, sizeof(args->pickCsv));
    lstrcpynA(args->scenarioKey, SelectedScenarioKey(), sizeof(args->scenarioKey));
    lstrcpynA(args->vanillaRoot, vanillaRoot, sizeof(args->vanillaRoot));
    lstrcpynA(args->modRoot, modRoot, sizeof(args->modRoot));
    if (ModuleRelativePath("arena_layout_profiles.json", args->layoutProfile, sizeof(args->layoutProfile))) {
        /* ok â€” o perfil fica em modules\arena_layout_profiles.json */
    } else {
        lstrcpynA(args->layoutProfile, "", sizeof(args->layoutProfile));
    }
    lstrcpynA(args->manifestOut, manifestOut, sizeof(args->manifestOut));

    g_jobThread = CreateThread(nullptr, 0, ComposeJobThread, args, 0, nullptr);
    if (!g_jobThread) {
        HeapFree(GetProcessHeap(), 0, args);
        _snprintf_s(g_jobError, sizeof(g_jobError), _TRUNCATE, "CreateThread failed err=0x%08X", GetLastError());
        InterlockedExchange(&g_jobState, JOB_FAIL);
        return false;
    }
    CloseHandle(g_jobThread);
    g_jobThread = NULL;
    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE, "Building mix: %s ...", g_jobPickCsv);
    return true;
}

static int __cdecl ComposePick_Draw(int obj) {
    using namespace NativeMenu;
    // WHY (crash do compose, 2026-08-03): durante o compose (o runner externo ArenaMultiBossLab),
    // NAO desenha o nosso menu no menu 2D do jogo. O EncodeLabel/DrawString corrompe o pool/cache
    // de texto do jogo (cache-hit -> pool[cursor-1] sem bound check -> handle invalido) -> o
    // UpdateWindowTitle (o jogo) crasha no DrawUITextElement com o ctx nulo (AV WRITE 0xBD, rva 0x4FB05E).
    // Congela o menu (1-2s) enquanto o bin e composto; o draw volta normal apos o compose.
    if (InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_RUNNING) == JOB_RUNNING)
        return obj;
    ++g_drawCalls;
    const int F = g_drawCalls;

    static unsigned char s_title[64], s_sub[128], s_foot[128];
    static bool s_enc = false;
    if (!s_enc) {
        EncodeLabel("Custom Mix - Pick Bosses", s_title, 64);
        s_enc = true;
    }
    EncodeLabel(g_statusLine, s_sub, 128);
    EncodeLabel(g_footerLine, s_foot, 128);

    DrawMenuBackdrop();
    DrawMenuNeonFrame(F);

    const float hx = NX(0.047f), hy = NY(0.054f), hw = NW(0.906f), hh = NH(0.126f);
    DrawMenuGlassPanel(hx, hy, hw, hh, F, 0);
    DrawString(s_title, NX(0.071f), NY(0.081f));
    DrawString(s_sub, NX(0.071f), NY(0.137f));

    /* Fase 2 â€” painel esquerdo: o mapa do layout (bolinhas) do ultimo Build Preview.
     * Fonte MENOR (DrawStringSub â€” scale 0.52/0.70) para caber no painel sem estourar.
     * A lista foi empurrada p/ a direita (vLeft 0.29) p/ o quadro do preview aparecer limpo. */
    if (g_previewCount > 0) {
        // Final preview (approximate, 2026-08-04): mostra o C (camera) junto aos P/M. Se a celula
        // cell already has a monster, STACKS (C on the row below, if free) - never overlaps.
        // If everything is full, C wins (approximate). Uses a local copy so the
        // runner original g_previewLines is not corrupted (otherwise the grid drifts each frame).
        char local[12][64];
        int rows;
        if (g_edLoaded && g_edBichos > 0) {
            // PREVIEW OF THE EDITS: builds the 16x8 grid from g_edPos (the edited positions),
            // not the runner original preview. This way "what I move shows in the preview".
            rows = 8;
            for (int gy = 0; gy < rows; ++gy) {
                for (int gx = 0; gx < 16; ++gx) local[gy][gx] = '.';
                local[gy][16] = '\0';
            }
            for (int i = 0; i < g_edBichos && i < 24; ++i) {
                int gx = g_edPos[i][0], gy = g_edPos[i][1];
                if (gx >= 0 && gx < 16 && gy >= 0 && gy < rows) {
                    // LINK: mirrors the runner preview — the first g_lastMixActorCount are the
                    // MONSTERS (digits 1-9), the rest is the PARTY (P, active = X).
                    local[gy][gx] = (i < g_lastMixActorCount && g_lastMixActorCount > 0)
                        ? (char)('1' + (i % 9))
                        : (i == g_edSel) ? 'X' : 'P';
                }
            }
        } else {
            rows = g_previewCount < 12 ? g_previewCount : 12;
            for (int i = 0; i < rows; ++i)
                strncpy_s(local[i], sizeof(local[i]), g_previewLines[i], _TRUNCATE);
        }
        // Camera (C), empilhada se a celula ja tem um bicho - "aproximado", sem sobrepor.
        if (g_edCam[0] >= 0 && g_edCam[0] < 16 && g_edCam[1] >= 0 && g_edCam[1] < rows) {
            const int cy = g_edCam[1], cx = g_edCam[0];
            const char cur = local[cy][cx];
            if (cur == '.' || cur == ' ' || cur == '\0') {
                local[cy][cx] = 'C';
            } else if (cy + 1 < rows &&
                       (local[cy + 1][cx] == '.' || local[cy + 1][cx] == ' ' || local[cy + 1][cx] == '\0')) {
                local[cy + 1][cx] = 'C';   // empilha na linha de baixo (nao sobrepoe)
            } else {
                local[cy][cx] = 'C';       // tudo cheio: o C prevalece (aproximado)
            }
        }
        for (int i = 0; i < rows; ++i)
            NativeMenu::EncodeLabel(local[i], g_previewBytes[i], 64);
        const float px = NX(0.032f), py = NY(0.172f), pw = NW(0.235f);
        const float ph = NH(0.021f * rows + 0.034f);
        DrawMenuGlassPanel(px, py, pw, ph, F, 1);
        DrawStringSub(g_previewBytes[0], NX(0.043f), NY(0.182f));
        for (int i = 1; i < rows; ++i)
            DrawStringSub(g_previewBytes[i], NX(0.043f), NY(0.182f + 0.021f * i));
    }

    const int top = RdW(obj, O_TOP);
    const int page = RdW(obj, O_PAGE);
    const int count = RdW(obj, O_COUNT);
    const int sel = RdW(obj, O_SELECTED);
    const float vLeft = NX(0.290f), vTop = NY(0.204f), vWidth = NW(0.455f);
    const float vStep = NH(0.063f), vBarH = NH(0.056f);
    const float selLine = MenuBorderPx() * 0.45f;
    const float cursorOff = NW(0.020f);

    /* Fase 3 â€” painel de ajuste de layout (modo ajuste): overlay escuro + painel central limpo. */
/* Live position edit (Part 1 â€” 2026-08-03): the window "dot map" editavel. */
    if (g_editPosMode) {
        DrawSolidRect(0.0f, 0.0f, MenuPhysW(), MenuPhysH(), 0xB0000000u, 0xB0000000u);
        const float px = NX(0.230f), py = NY(0.180f), pw = NW(0.54f), ph = NH(0.62f);
        DrawMenuGlassPanel(px, py, pw, ph, F, 1);
        if (g_editCamMode) {
            // CAM: os 3 parametros EXPLICITOS (YAW/PITCH/ZOOM) - o ativo entre [..].
            // U/D cicla o parametro; L/R ajusta o valor. O C no grid e a projecao do yaw+zoom.
            char camTitle[128];
            if (g_camParam == 0)
                _snprintf_s(camTitle, sizeof(camTitle), _TRUNCATE,
                    "CAM [YAW %d]  PITCH %d  ZOOM %d  (U/D param, L/R value)",
                    g_edCamYaw, g_edCamPitch, g_edCamZoom);
            else if (g_camParam == 1)
                _snprintf_s(camTitle, sizeof(camTitle), _TRUNCATE,
                    "CAM  YAW %d  [PITCH %d]  ZOOM %d  (U/D param, L/R value)",
                    g_edCamYaw, g_edCamPitch, g_edCamZoom);
            else
                _snprintf_s(camTitle, sizeof(camTitle), _TRUNCATE,
                    "CAM  YAW %d  PITCH %d  [ZOOM %d]  (U/D param, L/R value)",
                    g_edCamYaw, g_edCamPitch, g_edCamZoom);
            unsigned char ct[96]; NativeMenu::EncodeLabel(camTitle, ct, 96);
            DrawString(ct, NX(0.285f), NY(0.205f));
        } else {
            char edTitle[64]; _snprintf_s(edTitle, sizeof(edTitle), _TRUNCATE,
                "Edit Positions - %s (%d/%d)  gx %d gy %d",
                (g_edSel < g_lastMixActorCount && g_lastMixPicks[g_edSel][0]) ? g_lastMixPicks[g_edSel] : "Party",
                g_edSel + 1, g_edBichos, g_edPos[g_edSel][0], g_edPos[g_edSel][1]);
            unsigned char edT[64]; NativeMenu::EncodeLabel(edTitle, edT, 64);
            DrawString(edT, NX(0.285f), NY(0.205f));
        }
        // Builds the 16x8 grid. SEPARATE modes (by design, 2026-08-04):
        //  - POS: only P/M (monsters) - does NOT show C (the camera is not part of monster editing).
        //  - CAM: only C (camera) - does NOT show P (no overlap). Each mode keeps its own state.
        char grid[8][17];
        for (int gy = 0; gy < 8; ++gy) {
            for (int gx = 0; gx < 16; ++gx) grid[gy][gx] = '.';
            grid[gy][16] = '\0';
        }
        if (g_editCamMode) {
            if (g_edCam[0] >= 0 && g_edCam[0] < 16 && g_edCam[1] >= 0 && g_edCam[1] < 8)
                grid[g_edCam[1]][g_edCam[0]] = 'C';
        } else {
            for (int i = 0; i < g_edBichos; ++i) {
                int gx = g_edPos[i][0], gy = g_edPos[i][1];
                if (gx >= 0 && gx < 16 && gy >= 0 && gy < 8) {
                    char ch;
                    if (i == g_edSel) ch = 'X';
                    else if (i < g_lastMixActorCount) ch = (i < 9) ? (char)('1' + i) : '*';  // monsters = digits
                    else ch = 'P';  // party
                    grid[gy][gx] = ch;
                }
            }
        }
        for (int gy = 0; gy < 8; ++gy) {
            unsigned char gb[32]; NativeMenu::EncodeLabel(grid[gy], gb, 32);
            DrawString(gb, NX(0.285f), NY(0.275f) + NH(0.055f) * (float)gy);
        }
        const char* foot = "Arrows move  TAB switch  ENTER save  Cancel back";
        unsigned char fB[128]; NativeMenu::EncodeLabel(foot, fB, 128);
        DrawString(fB, NX(0.245f), NY(0.735f));
        return obj;
    }

    /* Fase 3 â€” painel de ajuste de layout (modo ajuste): overlay escuro + painel central limpo. */
    if (g_adjustMode) {
        DrawSolidRect(0.0f, 0.0f, MenuPhysW(), MenuPhysH(), 0xB0000000u, 0xB0000000u);
        const float ax = NX(0.270f), ay = NY(0.250f), aw = NW(0.460f), ah = NH(0.40f);
        DrawMenuGlassPanel(ax, ay, aw, ah, F, 1);
        unsigned char tmp[128];
        char tline[128];
        _snprintf_s(tline, sizeof(tline), _TRUNCATE, "AJUSTAR LAYOUT - %s", g_scenarioLabels[g_scenarioSelected]);
        NativeMenu::EncodeLabel(tline, tmp, 128);
        DrawString(tmp, ax + NW(0.03f), ay + NH(0.035f));
        for (int p = 0; p < kAdjCount; ++p) {
            char line[96];
            float v = g_scenProfile[g_scenarioSelected][p];
            const char* mark = (p == g_adjParam) ? "> " : "  ";
            if (p == 0) _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s %+.1f", mark, g_adjParamNames[p], v);
            else if (p == 2) _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s x%.2f", mark, g_adjParamNames[p], v);
            else if (p == 3) _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s %+.1f", mark, g_adjParamNames[p], v);
            else if (p == 4) _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s %.0f u", mark, g_adjParamNames[p], v);   // mon dist
            else if (p == 5) _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s +-%.0f deg", mark, g_adjParamNames[p], v); // mon arc
            else if (p == 6) _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s %s", mark, g_adjParamNames[p], v >= 0.5f ? "pattern" : "preserve"); // mode
            else _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s x%.2f", mark, g_adjParamNames[p], v);
            NativeMenu::EncodeLabel(line, tmp, 128);
            DrawStringSub(tmp, ax + NW(0.03f), ay + NH(0.105f) + NH(0.055f) * p);
        }
        unsigned char hint[160];
        EncodeLabel("Left/Right valor   Up/Down parametro   Enter salvar   Cancel voltar", hint, 160);
        DrawStringSub(hint, ax + NW(0.03f), ay + ah - NH(0.05f));
    }

    /* Fase 4 â€” janela de import de formaÃ§Ãµes (a Ãºltima opÃ§Ã£o = Excluir selecionada). */
    if (g_importMode) {
        const float wx = NX(0.300f), wy = NY(0.170f), ww = NW(0.460f), wh = NH(0.58f);
        DrawMenuGlassPanel(wx, wy, ww, wh, F, 1);
        unsigned char tmp[160];
EncodeLabel("Import Formation - Enter applies", tmp, 160);
        DrawStringSub(tmp, wx + NW(0.015f), wy + NH(0.018f));
        for (int i = 0; i < g_formationCount && i < 12; ++i) {
            char line[96];
            _snprintf_s(line, sizeof(line), _TRUNCATE, "%s %s",
                (i == g_importSel) ? ">" : "  ", g_formationList[i]);
            NativeMenu::EncodeLabel(line, tmp, 160);
            DrawStringSub(tmp, wx + NW(0.015f), wy + NH(0.050f) + NH(0.036f) * i);
        }
        char line[96];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "%s [Excluir selecionada]",
            (g_importSel >= g_formationCount) ? ">" : "  ");
        NativeMenu::EncodeLabel(line, tmp, 160);
        DrawStringSub(tmp, wx + NW(0.015f), wy + NH(0.050f) + NH(0.036f) * (g_formationCount < 12 ? g_formationCount : 12));
        EncodeLabel("Cancel volta ao menu", tmp, 160);
        DrawStringSub(tmp, wx + NW(0.015f), wy + wh - NH(0.042f));
    }

    /* Fase 4 â€” popup "Tem certeza?" com timer de 5s no botÃ£o Excluir. */
    if (g_confirmMode) {
        const float wx = NX(0.240f), wy = NY(0.330f), ww = NW(0.520f), wh = NH(0.26f);
        DrawMenuGlassPanel(wx, wy, ww, wh, F, 1);
        unsigned char tmp[160];
        char line[128];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "Tem certeza que quer excluir %s?", g_delTarget);
        EncodeLabel(line, tmp, 160);
        DrawStringSub(tmp, wx + NW(0.02f), wy + NH(0.045f));
        int secs = (g_delTimer + 59) / 60;
        if (g_delTimer > 0)
            _snprintf_s(line, sizeof(line), _TRUNCATE, "%s Excluir (%ds) %s",
                (g_confirmChoice == 0) ? ">" : "  ", secs, (g_confirmChoice == 0) ? "<" : "  ");
        else
            _snprintf_s(line, sizeof(line), _TRUNCATE, "%s Excluir %s",
                (g_confirmChoice == 0) ? ">" : "  ", (g_confirmChoice == 0) ? "<" : "  ");
        NativeMenu::EncodeLabel(line, tmp, 160);
        DrawStringSub(tmp, wx + NW(0.02f), wy + NH(0.13f));
        _snprintf_s(line, sizeof(line), _TRUNCATE, "%s Cancelar %s",
            (g_confirmChoice == 1) ? ">" : "  ", (g_confirmChoice == 1) ? "<" : "  ");
        NativeMenu::EncodeLabel(line, tmp, 160);
        DrawStringSub(tmp, wx + NW(0.02f), wy + NH(0.185f));
        EncodeLabel("Timer interno â€” 5s p/ habilitar o Excluir", tmp, 160);
        DrawStringSub(tmp, wx + NW(0.02f), wy + wh - NH(0.040f));
    }

    for (int r = 0; r < page; ++r) {
        const int row = top + r;
        const int totalRows = ComposePick_TotalRows();
        if (row >= count || row >= totalRows) break;
        unsigned int c0 = kMenuRowGlassTop;
        unsigned int c1 = kMenuRowGlassBot;
        if (row >= 0 && row < g_scenarioCount) {
            if (row == g_scenarioSelected) {
                c0 = 0xD03A6A88u;
                c1 = 0xD0183848u;
            }
        } else if (row >= ComposePick_PickFirst() && row < ComposePick_PickFirst() + kPickCount) {
            const int pickIdx = row - ComposePick_PickFirst();
            if (g_selected[pickIdx]) {
                c0 = 0xD04A4F88u;
                c1 = 0xD017203Fu;
            }
        } else if (row == ComposePick_RowLaunch()) {
            c0 = 0xD05A3A72u;
            c1 = 0xD0241848u;
        } else if (row == ComposePick_RowRelaunch() && g_lastMixValid && g_lastMixBinReady) {
            c0 = 0xD04A6A58u;
            c1 = 0xD0183048u;
        } else if (row == ComposePick_RowBuildPreview()) {
            c0 = 0xD03A7A88u;
            c1 = 0xD0142858u;
        } else if (row == ComposePick_RowRelaunch() && g_lastMixValid) {
            c0 = 0xD03A5A68u;
            c1 = 0xD0142838u;
        } else if (row == ComposePick_RowBack()) {
            c0 = 0xC0222A34u;
            c1 = 0xC00A1018u;
        }

        const float vy = vTop + (float)r * vStep;
        if (row == sel) {
            c0 = ColorLerp(c0, kMenuNeonGreenHi, 0.26f + Osc01(F, 46) * 0.10f);
            c1 = ColorLerp(c1, kMenuNeonGreenLo, 0.22f);
        }
        DrawSolidRect(vLeft, vy, vWidth, vBarH, c0, c1);
        if (row == sel) {
            DrawSolidRect(vLeft, vy + vBarH - selLine, vWidth, selLine, kMenuNeonGreenLine, kMenuNeonGreenLineLo);
            DrawCursor(vLeft - cursorOff, vy + NH(0.001f));
        }
        DrawString(g_labelBytes[row], vLeft + NW(0.015f), vy + NH(0.016f));
    }

    const float fx = NX(0.047f), fy = NY(0.887f), fw = NW(0.906f), fh = NH(0.070f);
    DrawMenuGlassPanel(fx, fy, fw, fh, F, 1);
    DrawString(s_foot, NX(0.071f), NY(0.911f));

    if (InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_RUNNING) == JOB_RUNNING) {
        DrawSolidRect(0.0f, 0.0f, MenuPhysW(), MenuPhysH(), 0x70000000u, 0x70000000u);
        static unsigned char s_busy[96];
        static bool s_busyEnc = false;
        if (!s_busyEnc) {
            EncodeLabel("Building mix - please wait...", s_busy, 96);
            s_busyEnc = true;
        }
        const float bx = NX(0.188f), by = NY(0.444f), bw = NW(0.625f), bh = NH(0.111f);
        DrawMenuGlassPanel(bx, by, bw, bh, F, 0);
        DrawString(s_busy, NX(0.219f), NY(0.489f));
    } else {
        /* Fase 2 â€” status do Build Preview quando o job termina (menu fica aberto). */
        static LONG s_lastJobState = JOB_IDLE;
        const LONG jobState = InterlockedCompareExchange(&g_jobState, JOB_IDLE, JOB_IDLE);
        if (jobState != s_lastJobState) {
            if (jobState == JOB_OK) {
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Preview ready - layout above. Fight Last Mix to enter.");
            } else if (jobState == JOB_FAIL) {
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Build falhou: %s", g_jobError);
            }
            s_lastJobState = jobState;
        }
    }
    return obj;
}

static int __cdecl ComposePick_InputCb(int obj) {
    if (g_inputCooldown > 0) {
        --g_inputCooldown;
        return obj;
    }
    if (InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_RUNNING) == JOB_RUNNING)
        return obj;
    const int dir = NativeMenu::PadDir();
    const int edge = NativeMenu::PadEdge();
    static int pL1=0,pR1=0,pL2=0,pR2=0;
    bool l1E=(dir&0x0400)&&!pL1, r1E=(dir&0x0800)&&!pR1, l2E=(dir&0x0100)&&!pL2, r2E=(dir&0x0200)&&!pR2;
    pL1=(dir&0x0400); pR1=(dir&0x0800); pL2=(dir&0x0100); pR2=(dir&0x0200);

/* Live position edit (Part 1): atalho E entra no modo de edicao do grid (monstros + players). */
    static bool s_ePrev = false;
    const bool eDown = (GetAsyncKeyState('E') & 0x8000) != 0;
    if ((eDown && !s_ePrev || l2E) && !g_adjustMode && !g_confirmMode) {
        if (!g_edLoaded) {
            // Loads ALL monsters from the preview (not just the P): the monsters (digits 1-9) first,
            // the party (P/X) after — the ORDER the apply expects (first actorCount = monsters).
            g_edBichos = 0;
            for (int pass = 0; pass < 2 && g_edBichos < 24; ++pass) {
                for (int y = 0; y < 8 && y < g_previewCount; ++y) {
                    for (int x = 0; x < 16 && x < static_cast<int>(strlen(g_previewLines[y])); ++x) {
                        const char ch = g_previewLines[y][x];
                        const bool mon = (ch >= '1' && ch <= '9');
                        const bool pch = (ch == 'P' || ch == 'X');
                        if ((pass == 0 && mon) || (pass == 1 && pch)) {
                            if (g_edBichos < 24) {
                                g_edPos[g_edBichos][0] = x; g_edPos[g_edBichos][1] = y;
                                g_edPosOrig[g_edBichos][0] = x; g_edPosOrig[g_edBichos][1] = y; // snapshot original
                                ++g_edBichos;
                            }
                        }
                    }
                }
            }
            g_edLoaded = true;   // apos carregar, mantem as edicoes (NAO re-monta do preview)
        }
        g_edSel = 0;
        g_editPosMode = (g_edBichos > 0) ? 1 : 0;
        if (g_edBichos == 0)
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                "No preview positions - Build Preview first (E).");
        else
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                "Edit Positions: arrows move, TAB switch bicho, ENTER save.");
        NativeMenu::PlaySfx(1);
    }
    s_ePrev = eDown;

    const int confirmEdge = edge & 0x20;
    const int cancelEdge = edge & 0x40;
    const bool confirmPressed = confirmEdge && !(g_lastConfirmEdge & 0x20);
    const bool cancelPressed = cancelEdge && !(g_lastConfirmEdge & 0x40);
    g_lastConfirmEdge = edge & 0x60;

/* Live position edit (Part 1 â€” 2026-08-03): o grid de pontos editavel (monstros + players). */
    if (g_editPosMode) {
        // CAM mode removed (2026-08-04). Space no longer toggles.
        static bool s_eEditPrev = false;
        const bool eEditDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        if ((eEditDown && !s_eEditPrev) || r2E) {
            // CAM removed (2026-08-04): g_editCamMode = !g_editCamMode;
            NativeMenu::PlaySfx(1);
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                g_editCamMode
                    ? "CAM mode: U/D param, L/R value, Space=POS/CAM, ENTER save."
                    : "POS mode: arrows move, TAB switch bicho, Space=POS/CAM, ENTER save.");
        }
        s_eEditPrev = eEditDown;
        if (g_editCamMode) {
            // CAM (3 dims explicitas): U/D cicla o parametro (YAW/PITCH/ZOOM); L/R ajusta o valor.
            if (dir & 0x1000) { g_camParam = (g_camParam + 2) % 3; NativeMenu::PlaySfx(1); }
            else if (dir & 0x4000) { g_camParam = (g_camParam + 1) % 3; NativeMenu::PlaySfx(1); }
            else if (dir & 0x8000) { AdjustCamParam(-1); NativeMenu::PlaySfx(1); }
            else if (dir & 0x2000) { AdjustCamParam(+1); NativeMenu::PlaySfx(1); }
        } else {
            if (dir & 0x1000) { if (g_edPos[g_edSel][1] > 0) --g_edPos[g_edSel][1]; NativeMenu::PlaySfx(1); }
            else if (dir & 0x4000) { if (g_edPos[g_edSel][1] < 7) ++g_edPos[g_edSel][1]; NativeMenu::PlaySfx(1); }
            else if (dir & 0x8000) { if (g_edPos[g_edSel][0] > 0) --g_edPos[g_edSel][0]; NativeMenu::PlaySfx(1); }
            else if (dir & 0x2000) { if (g_edPos[g_edSel][0] < 15) ++g_edPos[g_edSel][0]; NativeMenu::PlaySfx(1); }
            // TAB alterna o bicho (o L/R do usuario): o cursor pula pro P do proximo bicho.
            static bool s_tabPrev = false;
            const bool tabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
            if ((tabDown && !s_tabPrev) || r1E) { if (g_edBichos > 0) { g_edSel = (g_edSel + 1) % g_edBichos; NativeMenu::PlaySfx(1); } }
            if (l1E) { if (g_edBichos > 0) { g_edSel = (g_edSel + g_edBichos - 1) % g_edBichos; NativeMenu::PlaySfx(1); } }
            s_tabPrev = tabDown;
        }
        if (confirmPressed && !r2E && !(GetAsyncKeyState(VK_SPACE)&0x8000)) {
            // real bind = compose on Launch (the C# runner applies the grid to the bin, byte-safe).
            // WHY: writing X/Z directly to monLive at runtime corrupts the game memory
            // (crash AV WRITE 0x2409A5 pc -> pointer 0xBD). Only JSON + compose here.
            // The "&& !eEditDown" stops the toggle (Space) from also triggering the NativeMenu save/cancel.
            if (SavePositions()) {
                g_editPosMode = 0;
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Positions saved. Launch to apply in-game.");
            } else {
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Save failed (no preview?). Build Preview first.");
            }
        } else if (cancelPressed && !r2E && !(GetAsyncKeyState(VK_SPACE)&0x8000)) {
            // WHY: o V (toggle POS/CAM) e o avancar/trocar do jogo -> gera edge de cancel no
            // NativeMenu no mesmo frame. Sem o "&& !eEditDown", trocar o painel voltaria pro menu.
            g_editPosMode = 0;
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                "Position edit cancelled (not saved).");
        }
        return obj;
    }

    /* Fase 3 â€” modo ajuste de layout: setas mudam o perfil do cenÃ¡rio selecionado. */
    /* Fase 3 â€” modo ajuste de layout: setas mudam o perfil do cenÃ¡rio selecionado. */
    if (g_adjustMode) {
        if (dir & 0x1000) g_adjParam = (g_adjParam + kAdjCount - 1) % kAdjCount;
        else if (dir & 0x4000) g_adjParam = (g_adjParam + 1) % kAdjCount;
else if (dir & 0x8000) g_scenProfile[g_scenarioSelected][g_adjParam] -= g_adjSteps[g_adjParam];
        else if (dir & 0x2000) g_scenProfile[g_scenarioSelected][g_adjParam] += g_adjSteps[g_adjParam];
        if (dir != 0) NativeMenu::PlaySfx(1);
        if (confirmPressed) {
            SaveLayoutProfiles();
            g_adjustMode = 0;
            InterlockedExchange(&g_previewCount, 0);
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
            "Profile saved (modules\\arena_layout_profiles.json). Build Preview to see the layout.");
        } else if (cancelPressed) {
            g_adjustMode = 0;
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                "Adjust cancelled (not saved).");
        }
        return obj;
    }

    /* Fase 4 â€” popup de exclusÃ£o (timer 5s): "Tem certeza?" */
    if (g_confirmMode) {
        if (g_delTimer > 0) --g_delTimer;
        if (dir & (0x1000 | 0x4000)) {
            g_confirmChoice = 1 - g_confirmChoice;
            NativeMenu::PlaySfx(1);
        }
        if (cancelPressed) {
            g_confirmMode = 0;
            g_importMode = 0;
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                "ExclusÃ£o negada â€” saiu dthe window.");
        } else if (confirmPressed) {
            if (g_confirmChoice == 0 && g_delTimer <= 0) {
                DeleteFormation(g_lastFormSel);
                g_confirmMode = 0;
                g_importMode = 0;
                ScanFormations();
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Formation deleted: %s", g_delTarget);
            } else if (g_confirmChoice == 1) {
                g_confirmMode = 0;
                g_importMode = 0;
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "ExclusÃ£o negada â€” saiu dthe window.");
            }
        }
        return obj;
    }

    /* Fase 4 â€” janela de import de formaÃ§Ãµes. */
    if (g_importMode) {
        const int total = g_formationCount + 1;   /* + a row "Excluir selecionada" */
        if (dir & 0x1000) {
            g_importSel = (g_importSel > 0) ? (g_importSel - 1) : (total - 1);
            if (g_importSel < g_formationCount) g_lastFormSel = g_importSel;
            NativeMenu::PlaySfx(1);
        } else if (dir & 0x4000) {
            g_importSel = (g_importSel < total - 1) ? (g_importSel + 1) : 0;
            if (g_importSel < g_formationCount) g_lastFormSel = g_importSel;
            NativeMenu::PlaySfx(1);
        }
        if (confirmPressed) {
            if (g_importSel < g_formationCount) {
                ApplyFormation(g_importSel);
                g_importMode = 0;
            } else {
                lstrcpynA(g_delTarget, g_formationList[g_lastFormSel], sizeof(g_delTarget));
                g_confirmMode = 1;
                g_confirmChoice = 0;
                g_delTimer = kDeleteTimerFrames;
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Tem certeza? Excluir %s (5s p/ habilitar)", g_delTarget);
            }
        } else if (cancelPressed) {
            g_importMode = 0;
            _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                "ImportaÃ§Ã£o cancelada.");
        }
        return obj;
    }

    int sel = NativeMenu::RdW(obj, NativeMenu::O_SELECTED);
    const int count = NativeMenu::RdW(obj, NativeMenu::O_COUNT);
    int top = NativeMenu::RdW(obj, NativeMenu::O_TOP);
    const int page = NativeMenu::RdW(obj, NativeMenu::O_PAGE);
    if (count <= 0) return obj;

    if (dir & 0x1000) {
        sel = (sel > 0) ? (sel - 1) : (count - 1);
        NativeMenu::PlaySfx(1);
    } else if (dir & 0x4000) {
        sel = (sel < count - 1) ? (sel + 1) : 0;
        NativeMenu::PlaySfx(1);
    }

    if (sel < 0) sel = 0;
    if (sel > count - 1) sel = count - 1;
    if (sel < top) top = sel;
    if (sel >= top + page) top = sel - page + 1;
    if (top > count - page) top = count - page;
    if (top < 0) top = 0;
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, static_cast<int16_t>(sel));
    NativeMenu::WrW(obj, NativeMenu::O_TOP, static_cast<int16_t>(top));

    if (!g_closed) {
        if (confirmPressed) {
            NativeMenu::PlaySfx(1);
            if (sel >= 0 && sel < g_scenarioCount) {
                g_scenarioSelected = sel;
                RefreshLabels();
                InterlockedExchange(&g_previewCount, 0);   // cenÃ¡rio mudou â€” o preview do anterior nÃ£o vale mais
                _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                    "Scenario: %s", g_scenarioLabels[sel]);
                g_inputCooldown = kCooldownScenario;
            } else if (sel >= ComposePick_PickFirst() && sel < ComposePick_PickFirst() + kPickCount) {
                const int pickIdx = sel - ComposePick_PickFirst();
                int cur = g_selected[pickIdx];
                int next = cur + 1;
                if (!AdjustPickAllowed(pickIdx, 1) || next > g_requiredActors) next = 0;
                if (next != cur) {
                    g_selected[pickIdx] = (uint8_t)next;
                    if (next > 0 && cur == 0) g_pickOrder[pickIdx] = ++g_pickOrderSeq;
                    else RenumberPickOrder();
                    RefreshLabels();
                    char pickSummary[96] = {};
                    BuildPickSummary(pickSummary, sizeof(pickSummary));
                    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                        "Mix: %s (%d/%d) @ %s", pickSummary, CountSelectedActors(), g_requiredActors,
                        g_scenarioLabels[g_scenarioSelected]);
                }
                g_inputCooldown = kCooldownAeonPick;
            } else if (sel == ComposePick_RowRelaunch()) {
                if (dir & 0x8000) {
                    /* L = carregar os picks do ultimo mix (sem launch). */
                    if (!g_lastMixValid || g_lastMixPickCount <= 0) {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "No saved mix for Custom Mix x%d.", g_requiredActors);
                    } else {
                        ApplyPicksFromKeys(g_lastMixPicks, g_lastMixPickCount);
                        if (g_lastMixScenarioKey[0])
                            g_scenarioSelected = ScenarioIndexForKey(g_lastMixScenarioKey);
                        RefreshLabels();
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "Loaded last mix: %s @ %s", g_lastMixSummary,
                            g_scenarioLabels[g_scenarioSelected]);
                    }
                } else if (!g_lastMixValid || !g_lastMixBinReady) {
                    NativeMenu::PlaySfx(4);
                    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                        "Last bin missing â€” use Build + Launch first.");
                } else {
                    const int cost = GilCostForLastMixPicks();
                    if (!GilCanAfford(cost)) {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "Not enough Gil â€” need %d G.", cost);
                    } else {
                        StashPendingLaunchScenario();
                        g_pendingLaunchGilCost = cost;
                        g_result = ComposePickResult_Relaunch;
                        g_closed = 1;
                    }
                }
            } else if (sel == ComposePick_RowFormations()) {
                if (dir & 0x8000) {
                    /* L = importar (janela de lista). */
                    ScanFormations();
                    if (g_formationCount <= 0) {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "No formation saved yet.");
                    } else {
                        g_importMode = 1;
                        g_importSel = 0;
                        g_lastFormSel = 0;
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "Import: U/D choose  Enter apply  (last option = Delete)");
                    }
                } else if (SaveFormation()) {
                    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                        "formation saved (slot novo em modules\\arena_formations\\).");
                } else {
                    NativeMenu::PlaySfx(4);
                    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                        "Nothing to save - run Build Preview first.");
                }
            } else if (sel == ComposePick_RowBuildPreview()) {
                /* Fase 2/3 â€” L (esquerda) = ajustar layout (perfil do cenario); Confirm = compoe sem launch. */
                if (g_hasBuiltThisSession) {
                    NativeMenu::PlaySfx(1);
                    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                        "Preview ready - E / L2-LT to edit positions.");
                } else {
                    const int actors = CountSelectedActors();
                    if (actors != g_requiredActors) {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "Need exactly %d actors (now %d). Magus = +3.", g_requiredActors, actors);
                    } else if (StartComposeJob()) {
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "Building preview: %s ...", g_jobPickCsv);
                    } else {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE, "%s", g_jobError);
                    }
                }
            } else if (sel == ComposePick_RowLaunch()) {
                const int actors = CountSelectedActors();
                if (actors != g_requiredActors) {
                    NativeMenu::PlaySfx(4);
                    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                        "Need exactly %d actors (now %d). Magus = +3.", g_requiredActors, actors);
                } else {
                    const int cost = GilCostForSelectedPicks();
                    if (!GilCanAfford(cost)) {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
                            "Not enough Gil â€” need %d G.", cost);
                    } else if (StartComposeJob()) {
                        StashPendingLaunchScenario();
                        g_pendingLaunchGilCost = cost;
                        g_result = ComposePickResult_Launch;
                        g_closed = 1;
                    } else {
                        NativeMenu::PlaySfx(4);
                        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE, "%s", g_jobError);
                    }
                }
            } else if (sel == ComposePick_RowBack()) {
                g_result = ComposePickResult_Back;
                g_closed = 1;
            }
            if (sel >= ComposePick_RowRelaunch())
                g_inputCooldown = kCooldownBattleRow;
        } else if (cancelPressed) {
            NativeMenu::PlaySfx(4);
            g_result = ComposePickResult_Back;
            g_closed = 1;
        }
    }
    return obj;
}

static void CloseMenuObject() {
    if (!g_menu.obj) return;
    NativeMenu::WrB(g_menu.obj, 65, 1);
    g_menu.obj = 0;
    g_closed = 0;
    g_result = 0;
    g_inputCooldown = 0;
    g_lastConfirmEdge = 0;
    g_drawCalls = 0;
    InterlockedExchange(&g_previewCount, 0);   // zera preview apos batalha/close
    g_hasBuiltThisSession = false;              // nova sessao = fresh build
}

} // namespace

void ArenaPlusComposePick_SetLog(ArenaPlusComposeLogFn fn) { g_log = fn; }
void ArenaPlusComposePick_SetModule(HMODULE module) { g_module = module; }

bool ArenaPlusComposePick_IsCustomMixCombo(int combo) {
    return combo >= kCustomMixFirstCombo && combo <= kCustomMixLastCombo;
}

bool ArenaPlusComposePick_IsEnabled() {
    if (EnvFlagEnabled("FFXHOOKS_DISABLE_ARENA_PLUS_COMPOSE_F7")) return false;
    if (EnvFlagEnabled("FFXHOOKS_ENABLE_ARENA_PLUS_COMPOSE_F7")) return true;
    if (ModuleFileExists("arena_plus_compose_f7.flag") ||
        ModuleFileExists("config\\arena_plus_compose_f7.flag"))
        return true;
    /* Opt-in: other lanes can test legacy direct-launch DLL without the picker. */
    return false;
}

bool ArenaPlusComposePick_IsActive() { return g_menu.obj != 0; }

bool ArenaPlusComposePick_IsBusy() {
    return InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_RUNNING) == JOB_RUNNING;
}

bool ArenaPlusComposePick_Open(int combo) {
    if (!ArenaPlusComposePick_IsEnabled() || !ArenaPlusComposePick_IsCustomMixCombo(combo))
        return false;

    CloseMenuObject();
    ClearPickState();
    g_pendingLaunchScenarioKey[0] = '\0';
    g_targetCombo = combo;
    g_pendingLaunchGilCost = 0;   // fresh session — cost reset on open, consumed on LaunchComboBattleFromPump
    g_requiredActors = RequiredActorsForCombo(combo);
    InitScenariosForCombo(combo);
    InterlockedExchange(&g_jobState, JOB_IDLE);
    InterlockedExchange(&g_jobExitCode, -1);
    g_jobError[0] = '\0';
    g_jobPickCsv[0] = '\0';
    _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE,
        "Custom Mix x%d - pick scenario + %d bosses (Magus = +3)", g_requiredActors, g_requiredActors);
    lstrcpynA(g_footerLine, "Confirm select/launch   Cancel Back", static_cast<int>(sizeof(g_footerLine)));
    ReloadLastMixManifest();
    g_hasBuiltThisSession = false;  // first build of the session: button reverts to "First Build - Press Enter"
    // CRITICAL: if the user closed the menu mid-edit (E/adjust/confirm), the mode persisted and
    // on reopen "the first menu shown is E" (no build). Resets ALL modes.
    g_editPosMode = 0;
    g_adjustMode = 0;
    g_confirmMode = 0;
    g_editCamMode = false;               // volta pro modo POS ao abrir o compose pick
    g_edCam[0] = 8; g_edCam[1] = 4;      // camera default: centro do grid (yaw/zoom neutro)
    g_camParam = 0;                      // parametro ativo da camera: YAW
    g_edCamYaw = 180; g_edCamPitch = 12; g_edCamZoom = 40;  // default da camera
    g_edCamOrig[0] = 8; g_edCamOrig[1] = 4;               // snapshot original da CAM (p/ nao apagar JSON se so CAM mudou)
    g_edCamYawOrig = 180; g_edCamPitchOrig = 12; g_edCamZoomOrig = 40;
    CamGridFromPolar();
    g_edLoaded = false;                  // ao reabrir o pick, recarrega as posicoes do preview
    InterlockedExchange(&g_previewCount, 0);   // zera preview do session anterior
    if (g_lastMixScenarioKey[0])
        g_scenarioSelected = ScenarioIndexForKey(g_lastMixScenarioKey);
    RefreshLabels();

    int obj = NativeMenu::Alloc();
    if (!obj) return false;

    NativeMenu::WrW(obj, NativeMenu::O_COUNT, static_cast<int16_t>(ComposePick_TotalRows()));
    NativeMenu::WrW(obj, NativeMenu::O_PAGE, static_cast<int16_t>(kVisiblePage));
    NativeMenu::WrW(obj, NativeMenu::O_TOP, 0);
    NativeMenu::WrW(obj, NativeMenu::O_SELECTED, 0);
    NativeMenu::WrB(obj, NativeMenu::O_SLOTS, 1);
    NativeMenu::WrB(obj, NativeMenu::O_CANCEL, 1);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP62, 2);
    NativeMenu::WrB(obj, NativeMenu::O_GROUP63, 1);
    NativeMenu::WrP(obj, NativeMenu::O_ENTER, (void*)0);
    NativeMenu::WrP(obj, NativeMenu::O_UPDATE, (void*)(uintptr_t)&ComposePick_InputCb);
    NativeMenu::WrP(obj, NativeMenu::O_DRAW, (void*)(uintptr_t)&ComposePick_Draw);
    NativeMenu::WrP(obj, NativeMenu::O_AUX, (void*)(uintptr_t)&NativeMenu::OurAux);
    NativeMenu::WrP(obj, NativeMenu::O_VALIDATOR, (void*)0);

    g_closed = 0;
    g_result = 0;
    g_inputCooldown = kCooldownOpen;
    g_lastConfirmEdge = NativeMenu::PadEdge() & 0x60;
    g_menu = NativeMenu::Menu{ obj };
    NativeMenu::Register(obj);
    LogLine("[ffx-hooks] ArenaPlus compose pick open combo=%d need=%d obj=0x%08X\n",
        combo, g_requiredActors, static_cast<unsigned>(obj));
    return true;
}

void ArenaPlusComposePick_Close() {
    CloseMenuObject();
    g_targetCombo = -1;
}

int ArenaPlusComposePick_PendingGilCost() {
    return g_pendingLaunchGilCost;
}

void ArenaPlusComposePick_Tick() {
    static volatile LONG s_lastJobState = JOB_IDLE;
    const LONG state = InterlockedCompareExchange(&g_jobState, JOB_IDLE, JOB_IDLE);
    if (state == JOB_OK && s_lastJobState != JOB_OK) {
        ReloadLastMixManifest();
        g_hasBuiltThisSession = true;  // first build of the session done -> button becomes "E / L2-LT to Edit"
        RefreshLabels();
        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE, "Mix ready: %s", g_jobPickCsv);
    } else if (state == JOB_FAIL && s_lastJobState != JOB_FAIL) {
        _snprintf_s(g_statusLine, sizeof(g_statusLine), _TRUNCATE, "Compose failed: %s", g_jobError);
    }
    s_lastJobState = state;
}

ArenaPlusComposePollResult ArenaPlusComposePick_PollMenu() {
    ArenaPlusComposePollResult out = {};
    if (!g_menu.obj) return out;

    if (g_closed) {
        if (g_result == ComposePickResult_Relaunch) {
            out.what = ArenaPlusComposePollKind::LaunchCached;
            out.combo = g_targetCombo;
            return out;
        }
        if (g_result == ComposePickResult_Launch) {
            const LONG state = InterlockedCompareExchange(&g_jobState, JOB_OK, JOB_OK);
            if (state == JOB_OK) {
                out.what = ArenaPlusComposePollKind::Launch;
                out.combo = g_targetCombo;
                InterlockedExchange(&g_jobState, JOB_IDLE);
                return out;
            }
            const LONG fail = InterlockedCompareExchange(&g_jobState, JOB_FAIL, JOB_FAIL);
            if (fail == JOB_FAIL) {
                g_closed = 0;
                g_result = 0;
                g_inputCooldown = 30;
                g_lastConfirmEdge = 0;
                InterlockedExchange(&g_jobState, JOB_IDLE);
                NativeMenu::PlaySfx(4);
                return out;
            }
            if (InterlockedCompareExchange(&g_jobState, JOB_RUNNING, JOB_RUNNING) == JOB_RUNNING) {
                out.what = ArenaPlusComposePollKind::Nav;
                return out;
            }
        }
        if (g_result == ComposePickResult_Back) {
            out.what = ArenaPlusComposePollKind::Back;
            return out;
        }
    }

    out.what = ArenaPlusComposePollKind::Nav;
    return out;
}

bool ArenaPlusComposePick_ApplyLaunchRouteOverride(
    int combo,
    int* field,
    int* group,
    int* formation,
    char* backdropBattleId,
    int backdropBattleIdCap,
    int* backdropBattlefieldId) {
    if (backdropBattlefieldId) *backdropBattlefieldId = 0;
    const char* key = LaunchScenarioKey();
    if (ScenarioRouteForKey(combo, key, field, group, formation)) {
        char backdrop[32] = {};
        ScenarioBackdropBattleIdForKey(combo, key, backdrop, sizeof(backdrop));
        CopyBackdropBattleIdOptional(backdrop, backdropBattleId, backdropBattleIdCap);
        if (backdropBattlefieldId) ScenarioBattlefieldIdForKey(key, backdropBattlefieldId);
        LogLine("[ffx-hooks] ArenaPlus compose: launch route from picker key=%s field=%d group=%d formation=%d backdrop=%s bf=%d\n",
            key,
            *field,
            *group,
            *formation,
            backdrop[0] ? backdrop : "?",
            backdropBattlefieldId ? *backdropBattlefieldId : 0);
        g_pendingLaunchScenarioKey[0] = '\0';
        return true;
    }
    if (ReadManifestLaunchRoute(combo, field, group, formation, backdropBattleId, backdropBattleIdCap, backdropBattlefieldId))
        return true;
    g_pendingLaunchScenarioKey[0] = '\0';
    return false;
}

#endif // FFXHOOKS_HAVE_POLYHOOK
