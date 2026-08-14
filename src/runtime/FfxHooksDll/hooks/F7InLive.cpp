// F7InLive.cpp — "FFX Editor - In-Live" (F7): Difficulty (RAM), Force Last Battle, Music.
// Lane Jarvis-HOOK. Gate: f7_inlive.flag / FFXHOOKS_ENABLE_F7=1.
//
// Estrategia (honesta):
//  - Difficulty: hook read-only no parse da cena de batalha (FFX_Battle_InitSystemSceneAndActorTable
//    @ VA 0x783ED0) marca "nova batalha"; o apply roda na MAIN THREAD via F7_TickMainThread()
//    (chamado do pump hook) quando os stats dos inimigos ja estao na RAM (Max_hp != 0).
//    Multiplicadores/status sao aplicados SOMENTE na RAM (nunca em bin) — a proxima batalha
//    re-popula do vanilla automaticamente (restore natural). "Uma vez salvo, sempre que entrar
//    em batalha as modificacoes ja sao feitas" = config persistida em f7_inlive.json.
//  - Force Last Battle: hook read-only em FFX_Field_ResolveEncounterToken (VA 0x7828B0) captura
//    field row + group do ultimo encontro NATURAL; "Force" = CALL MsBattleEncountExe (VA 0x780DE0,
//    int __cdecl(int,int,float)) na main thread — caminho ja provado pelo Arena+ (ArenaPlus_ForceBattleDirect).
//  - Music: reusa o contrato do FFXHooksBlock (musicOverrideTrackIndex + musicSeq) para LOCK e o
//    pending de battle-entry do MusicHook (SetArenaBattleMusicPending) para BATTLE. Randomizer
//    sorteia da playlist no battle start. Requer MusicHook instalado (music.flag / FFXHOOKS_ENABLE_MUSIC).
#include "F7InLive.h"
#include "MusicHook.h"   // SetArenaBattleMusicPending / SetMusicHookMinFadeFrames (battle-entry pending)


#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <share.h>
#include <MinHook.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

// ── Constantes (RVAs — VA IDA - 0x400000) ─────────────────────────────────
static const uint32_t RVA_ENEMY_LIST_PTR      = 0x00D37634u; // FFX_Battle_CtbPriorityQueue[4252] -> g_BattleEnemyList
static const uint32_t RVA_ENEMY_LIST_ALT      = 0x00D34460u; // "enemy-list" (handoff AI) — fallback
static const uint32_t RVA_RESOLVE_ENCOUNTER   = 0x003828B0u; // FFX_Field_ResolveEncounterToken
static const uint32_t RVA_INIT_SYSTEM_SCENE   = 0x00383ED0u; // FFX_Battle_InitSystemSceneAndActorTable
static const uint32_t RVA_MS_BATTLE_ENCOUNT   = 0x00380DE0u; // MsBattleEncountExe (field, group, walkedDelta)
static const uint32_t F7_ENEMY_STRIDE         = 0xF90u;

// Offsets do actor (MemoryChr.cs) — validados no decompile 0x79C130
static const uint32_t F7_OFF_MONSTER_ID   = 0x00Eu;  // u16 != 0xFFFF = slot ocupado
static const uint32_t F7_OFF_MAX_HP       = 0x594u;
static const uint32_t F7_OFF_MAX_MP       = 0x598u;
static const uint32_t F7_OFF_OVERKILL     = 0x5A4u;
static const uint32_t F7_OFF_STR          = 0x5A8u;
static const uint32_t F7_OFF_DEF          = 0x5A9u;
static const uint32_t F7_OFF_MAG          = 0x5AAu;
static const uint32_t F7_OFF_MDF          = 0x5ABu;
static const uint32_t F7_OFF_AGI          = 0x5ACu;
static const uint32_t F7_OFF_LCK          = 0x5ADu;
static const uint32_t F7_OFF_EVA          = 0x5AEu;
static const uint32_t F7_OFF_ACC          = 0x5AFu;
static const uint32_t F7_OFF_ELEM_ABSORB  = 0x5DAu;
static const uint32_t F7_OFF_ELEM_RESIST  = 0x5DCu;
static const uint32_t F7_OFF_ELEM_WEAK    = 0x5DDu;
static const uint32_t F7_OFF_INNATE_AUTO  = 0x630u;  // 3 x u16
static const uint32_t F7_OFF_STATUS_RESIST= 0x641u;  // 25 bytes
static const uint32_t F7_OFF_CURRENT_HP   = 0x6E4u;
static const uint32_t F7_OFF_CURRENT_MP   = 0x6E8u;

static const char* const F7_STATUS_NAMES[F7_STATUS_COUNT] = {
    "Death", "Zombie", "Petrify", "Poison", "PowerBreak", "MagicBreak", "ArmorBreak", "MentalBreak",
    "Confuse", "Berserk", "Provoke", "Threaten", "Sleep", "Silence", "Darkness", "Shell",
    "Protect", "Reflect", "NulTide", "NulBlaze", "NulShock", "NulFrost", "Regen", "Haste", "Slow"
};

// ── Estado global ─────────────────────────────────────────────────────────
static uintptr_t          g_base       = 0;
static FFXHooksBlock*     g_block      = nullptr;
static void (*g_log)(const char*)      = nullptr;
static bool               g_enabled    = false;
static volatile LONG      g_inBattle   = 0;   // 1 entre InitSystemScene e apply
static volatile LONG      g_needApply  = 0;   // nova batalha aguardando stats prontos
static volatile int       g_appliedCount = 0;

static F7Config g_cfg = {};

static void* g_trampResolve = nullptr;
static void* g_trampScene   = nullptr;

static void F7_ApplyMusicBattle();   // fwd: armada no battle-start (auto-apply)

// ── Helpers ───────────────────────────────────────────────────────────────
void F7_Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(line);
}

bool F7_IsEnabled() {
    return g_enabled;
}

static bool EnvFlagOn(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T');
}

static bool ModuleFileExists(const char* relativePath) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);           // FFX.exe
    char* slash = strrchr(path, '\\');
    if (slash) *slash = '\0';
    char full[MAX_PATH] = {};
    _snprintf_s(full, sizeof(full), _TRUNCATE, "%s\\%s", path, relativePath);
    return GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES;
}

static void ResolveConfigPath(char* out, size_t cap) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *slash = '\0';
    _snprintf_s(out, cap, _TRUNCATE, "%s\\modules\\config\\f7_inlive.json", path);
}

static bool ReadSmallFile(const char* path, char* out, size_t cap) {
    if (!path || !out || cap == 0) return false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart >= (LONGLONG)cap) {
        CloseHandle(h);
        return false;
    }
    DWORD read = 0;
    BOOL ok = ReadFile(h, out, (DWORD)size.QuadPart, &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out[read] = '\0';
    return true;
}

static int JsonInt(const char* json, const char* key, int def) {
    if (!json || !key) return def;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':');
    if (!p) return def;
    return atoi(p + 1);
}

static bool JsonBool(const char* json, const char* key, bool def) {
    if (!json || !key) return def;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':');
    if (!p) return def;
    return strstr(p, "true") != nullptr;
}

static uint32_t JsonU32(const char* json, const char* key, uint32_t def) {
    const int v = JsonInt(json, key, (int)def);
    return (v < 0) ? def : (uint32_t)v;
}

static void JsonIntArray(const char* json, const char* key, int* out, int maxCount, int* outCount) {
    *outCount = 0;
    if (!json || !key || !out) return;
    char needle[64] = {};
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    ++p;
    while (*p && *p != ']' && *outCount < maxCount) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
        if (*p == ']' || !*p) break;
        out[*outCount] = atoi(p);
        ++*outCount;
        while (*p && *p != ',' && *p != ']') ++p;
    }
}



// ── Config load ───────────────────────────────────────────────────────────
static void LoadPresetFromJson(const char* json, F7DifficultyPreset* p, const char* base) {
    if (!p || !json) return;
    char key[80] = {};
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_enabled", base);     p->enabled = JsonBool(json, key, false);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_hpMul", base);       p->hpMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_mpMul", base);       p->mpMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_strMul", base);      p->strMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_defMul", base);      p->defMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_magMul", base);      p->magMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_mdfMul", base);      p->mdfMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_agiMul", base);      p->agiMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_accMul", base);      p->accMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_evaMul", base);      p->evaMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_lckMul", base);      p->lckMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_overkillMul", base); p->overkillMul = JsonInt(json, key, 1000);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_autoStatus", base);  p->autoStatusMask = JsonU32(json, key, 0);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_elemWeak", base);    p->elemWeak = (uint8_t)JsonInt(json, key, 0);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_elemResist", base);  p->elemResist = (uint8_t)JsonInt(json, key, 0);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_elemAbsorb", base);  p->elemAbsorb = (uint8_t)JsonInt(json, key, 0);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_statusResist", base);
    int count = 0;
    JsonIntArray(json, key, (int*)p->statusResist, F7_STATUS_COUNT, &count);
}

static void LoadAreaRulesFromJson(const char* json) {
    g_cfg.areaCount = 0;
    const char* areasKey = strstr(json, "\"areas\"");
    if (!areasKey) return;
    const char* cursor = areasKey;
    const char* end = json + strlen(json);
    while (cursor && cursor < end && g_cfg.areaCount < F7_AREA_RULES_MAX) {
        const char* rowStart = strchr(cursor, '{');
        if (!rowStart) break;
        const char* rowEnd = strchr(rowStart, '}');
        if (!rowEnd) break;
        F7AreaRule& r = g_cfg.areas[g_cfg.areaCount];
        r.enabled = JsonBool(rowStart, "enabled", false);
        r.fieldRow = JsonInt(rowStart, "fieldRow", -1);
        r.hpMul = JsonInt(rowStart, "hpMul", 1000);
        r.strMul = JsonInt(rowStart, "strMul", 1000);
        r.defMul = JsonInt(rowStart, "defMul", 1000);
        r.magMul = JsonInt(rowStart, "magMul", 1000);
        r.mdfMul = JsonInt(rowStart, "mdfMul", 1000);
        r.agiMul = JsonInt(rowStart, "agiMul", 1000);
        r.autoStatusMask = JsonU32(rowStart, "autoStatus", 0);
        r.elemWeak = (uint8_t)JsonInt(rowStart, "elemWeak", 0);
        r.elemResist = (uint8_t)JsonInt(rowStart, "elemResist", 0);
        ++g_cfg.areaCount;
        cursor = rowEnd + 1;
    }
}

bool F7_LoadConfig() {
    g_cfg = F7Config{};  // zeros (desligado por default)
    char path[MAX_PATH] = {};
    ResolveConfigPath(path, sizeof(path));
    char json[16384] = {};
    if (!ReadSmallFile(path, json, sizeof(json))) {
        F7_Log("[ffx-hooks] F7: config ausente (%s) — defaults (desligado)\n", path);
        return false;
    }
    LoadPresetFromJson(json, &g_cfg.diffGlobal, "diff");
    g_cfg.diffByArea = JsonBool(json, "diffByArea", false);
    LoadAreaRulesFromJson(json);
    g_cfg.music.lockTrack = JsonInt(json, "music_lock", -1);
    g_cfg.music.battleTrack = JsonInt(json, "music_battle", -1);
    g_cfg.music.randomizer = JsonBool(json, "music_randomizer", false);
    g_cfg.music.fadeFrames = JsonInt(json, "music_fade", 0);
    JsonIntArray(json, "music_playlist", g_cfg.music.playlist, F7_PLAYLIST_MAX, &g_cfg.music.playlistCount);
    g_cfg.force.lastField = JsonInt(json, "force_lastField", -1);
    g_cfg.force.lastGroup = JsonInt(json, "force_lastGroup", -1);
    g_cfg.force.lastFormation = JsonInt(json, "force_lastFormation", 0);
    g_cfg.force.hasLast = JsonBool(json, "force_hasLast", false);
    g_cfg.force.repeatCount = JsonInt(json, "force_repeat", 1);
    if (g_cfg.force.repeatCount < 1) g_cfg.force.repeatCount = 1;
    if (g_cfg.force.repeatCount > 9) g_cfg.force.repeatCount = 9;
    F7_Log("[ffx-hooks] F7: config carregada diff=%s hpMul=%d musicLock=%d battle=%d areas=%d forceLast=%s(%d/%d)\n",
        g_cfg.diffGlobal.enabled ? "ON" : "OFF", g_cfg.diffGlobal.hpMul,
        g_cfg.music.lockTrack, g_cfg.music.battleTrack, g_cfg.areaCount,
        g_cfg.force.hasLast ? "sim" : "nao", g_cfg.force.lastField, g_cfg.force.lastGroup);
    return true;
}
// ── Config save (atomic: .tmp + MoveFileEx) ──────────────────────────────
static void AppendPresetJson(char* buf, size_t cap, size_t* used, const F7DifficultyPreset& p, const char* base) {
    char key[80] = {};
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_enabled", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %s,\n", key, p.enabled ? "true" : "false");
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_hpMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.hpMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_strMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.strMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_defMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.defMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_magMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.magMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_mdfMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.mdfMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_agiMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.agiMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_accMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.accMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_evaMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.evaMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_lckMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.lckMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_overkillMul", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, p.overkillMul);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_autoStatus", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %u,\n", key, p.autoStatusMask);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_elemWeak", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, (int)p.elemWeak);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_elemResist", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, (int)p.elemResist);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_elemAbsorb", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": %d,\n", key, (int)p.elemAbsorb);
    _snprintf_s(key, sizeof(key), _TRUNCATE, "%s_statusResist", base);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "    \"%s\": [", key);
    for (int i = 0; i < F7_STATUS_COUNT; ++i)
        *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "%s%d", i ? "," : "", (int)p.statusResist[i]);
    *used += (size_t)_snprintf_s(buf + *used, cap - *used, _TRUNCATE, "],\n");
}


bool F7_SaveConfig() {
    char path[MAX_PATH] = {};
    ResolveConfigPath(path, sizeof(path));
    char tmp[MAX_PATH] = {};
    _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", path);
    char buf[16384] = {};
    size_t used = 0;
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
        "{\n  \"$schema\": \"./f7_inlive.schema.json\",\n  \"version\": 1,\n");
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "  \"diffByArea\": %s,\n", g_cfg.diffByArea ? "true" : "false");
    AppendPresetJson(buf, sizeof(buf), &used, g_cfg.diffGlobal, "diff");
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "  \"areas\": [\n");
    for (int i = 0; i < g_cfg.areaCount; ++i) {
        const F7AreaRule& r = g_cfg.areas[i];
        used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
            "    { \"enabled\": %s, \"fieldRow\": %d, \"hpMul\": %d, \"strMul\": %d, \"defMul\": %d, \"magMul\": %d, \"mdfMul\": %d, \"agiMul\": %d, \"autoStatus\": %u, \"elemWeak\": %d, \"elemResist\": %d }%s\n",
            r.enabled ? "true" : "false", r.fieldRow, r.hpMul, r.strMul, r.defMul, r.magMul, r.mdfMul,
            r.agiMul, r.autoStatusMask, (int)r.elemWeak, (int)r.elemResist,
            (i + 1 < g_cfg.areaCount) ? "," : "");
    }
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "  ],\n");
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
        "  \"music_lock\": %d,\n  \"music_battle\": %d,\n  \"music_randomizer\": %s,\n  \"music_fade\": %d,\n",
        g_cfg.music.lockTrack, g_cfg.music.battleTrack, g_cfg.music.randomizer ? "true" : "false", g_cfg.music.fadeFrames);
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "  \"music_playlist\": [");
    for (int i = 0; i < g_cfg.music.playlistCount; ++i)
        used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "%s%d", i ? "," : "", g_cfg.music.playlist[i]);
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "],\n");
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
        "  \"force_lastField\": %d,\n  \"force_lastGroup\": %d,\n  \"force_lastFormation\": %d,\n  \"force_hasLast\": %s,\n  \"force_repeat\": %d\n}\n",
        g_cfg.force.lastField, g_cfg.force.lastGroup, g_cfg.force.lastFormation,
        g_cfg.force.hasLast ? "true" : "false", g_cfg.force.repeatCount);
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        F7_Log("[ffx-hooks] F7: save tmp open fail (err=%lu)\n", GetLastError());
        return false;
    }
    DWORD written = 0;
    WriteFile(h, buf, (DWORD)used, &written, nullptr);
    CloseHandle(h);
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        F7_Log("[ffx-hooks] F7: save move fail (err=%lu)\n", GetLastError());
        return false;
    }
    F7_Log("[ffx-hooks] F7: config salva (%zu bytes)\n", used);
    return true;
}

const F7Config& F7_GetConfig() {
    return g_cfg;
}


// ── Enemy list (RAM) ──────────────────────────────────────────────────────
static uint8_t* EnemyListBase() {
    if (!g_base) return nullptr;
    uint32_t ptr = *(volatile uint32_t*)(g_base + RVA_ENEMY_LIST_PTR);
    if (ptr < g_base || ptr >= g_base + 0x08000000u) {
        // fallback: "enemy-list" do handoff AI (0xD34460)
        uint32_t alt = *(volatile uint32_t*)(g_base + RVA_ENEMY_LIST_ALT);
        if (alt >= g_base && alt < g_base + 0x08000000u) return (uint8_t*)alt;
        return nullptr;
    }
    return (uint8_t*)ptr;
}

static bool EnemySlotOccupied(const uint8_t* entry) {
    return entry && (*(volatile uint16_t*)(entry + F7_OFF_MONSTER_ID) != 0xFFFFu);
}

static bool EnemyStatsReady(const uint8_t* entry) {
    return entry && (*(volatile int32_t*)(entry + F7_OFF_MAX_HP) != 0);
}

static int ClampStat(int v, int lo, int hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static void ApplyMulToByte(uint8_t* entry, uint32_t off, int mul) {
    if (mul == 1000) return;                       // x1.0 = no-op
    const int v = *(volatile uint8_t*)(entry + off);
    if (v == 0) return;                            // 0 fica 0 (monstro sem o stat)
    int nv = (v * mul) / 1000;
    if (nv < 1) nv = 1;
    if (nv > 255) nv = 255;
    *(volatile uint8_t*)(entry + off) = (uint8_t)nv;
}

// Aplica o preset a UM inimigo (entry). Retorna true se mexeu em algo.
static bool ApplyPresetToEnemy(uint8_t* entry, const F7DifficultyPreset& p) {
    if (!entry) return false;
    bool touched = false;
    // HP/MP: multiplica Max; escala Current proporcional (mantem a razao).
    if (p.hpMul != 1000) {
        const int32_t maxHp = *(volatile int32_t*)(entry + F7_OFF_MAX_HP);
        const int32_t curHp = *(volatile int32_t*)(entry + F7_OFF_CURRENT_HP);
        int32_t newMax = (int32_t)(((int64_t)maxHp * p.hpMul) / 1000);
        if (newMax < 1) newMax = 1;
        *(volatile int32_t*)(entry + F7_OFF_MAX_HP) = newMax;
        if (curHp > 0) {
            int64_t newCur = ((int64_t)curHp * p.hpMul) / 1000;
            *(volatile int32_t*)(entry + F7_OFF_CURRENT_HP) = (int32_t)((newCur < 1) ? 1 : newCur);
        }
        touched = true;
    }
    if (p.mpMul != 1000) {
        const int32_t maxMp = *(volatile int32_t*)(entry + F7_OFF_MAX_MP);
        int32_t newMax = (int32_t)(((int64_t)maxMp * p.mpMul) / 1000);
        if (newMax < 1) newMax = 1;
        *(volatile int32_t*)(entry + F7_OFF_MAX_MP) = newMax;
        const int32_t curMp = *(volatile int32_t*)(entry + F7_OFF_CURRENT_MP);
        if (curMp > 0) {
            int64_t newCur = ((int64_t)curMp * p.mpMul) / 1000;
            *(volatile int32_t*)(entry + F7_OFF_CURRENT_MP) = (int32_t)((newCur < 1) ? 1 : newCur);
        }
        touched = true;
    }
    if (p.overkillMul != 1000) {
        const int32_t ok = *(volatile int32_t*)(entry + F7_OFF_OVERKILL);
        int32_t newOk = (int32_t)(((int64_t)ok * p.overkillMul) / 1000);
        *(volatile int32_t*)(entry + F7_OFF_OVERKILL) = newOk < 1 ? 1 : newOk;
        touched = true;
    }
    ApplyMulToByte(entry, F7_OFF_STR, p.strMul);
    ApplyMulToByte(entry, F7_OFF_DEF, p.defMul);
    ApplyMulToByte(entry, F7_OFF_MAG, p.magMul);
    ApplyMulToByte(entry, F7_OFF_MDF, p.mdfMul);
    ApplyMulToByte(entry, F7_OFF_AGI, p.agiMul);
    ApplyMulToByte(entry, F7_OFF_ACC, p.accMul);
    ApplyMulToByte(entry, F7_OFF_EVA, p.evaMul);
    ApplyMulToByte(entry, F7_OFF_LCK, p.lckMul);
    // Elements (OR — add, not replace): 0x01 Fire 0x02 Ice 0x04 Thunder 0x08 Water 0x10 Holy
    if (p.elemWeak)   { *(volatile uint8_t*)(entry + F7_OFF_ELEM_WEAK)   |= p.elemWeak;   touched = true; }
    if (p.elemResist) { *(volatile uint8_t*)(entry + F7_OFF_ELEM_RESIST) |= p.elemResist; touched = true; }
    if (p.elemAbsorb) { *(volatile uint8_t*)(entry + F7_OFF_ELEM_ABSORB) |= p.elemAbsorb; touched = true; }
    // Auto-status: bits 0..24 -> innate_auto (3 x u16 LE)
    if (p.autoStatusMask) {
        uint32_t mask = p.autoStatusMask & 0x01FFFFFFu;   // so bits 0..24
        uint16_t w0 = *(volatile uint16_t*)(entry + F7_OFF_INNATE_AUTO);
        uint16_t w1 = *(volatile uint16_t*)(entry + F7_OFF_INNATE_AUTO + 2);
        uint16_t w2 = *(volatile uint16_t*)(entry + F7_OFF_INNATE_AUTO + 4);
        w0 |= (uint16_t)(mask & 0xFFFFu);
        w1 |= (uint16_t)((mask >> 16) & 0xFFFFu);
        // bits 32..47 nao usados (25 status)
        *(volatile uint16_t*)(entry + F7_OFF_INNATE_AUTO) = w0;
        *(volatile uint16_t*)(entry + F7_OFF_INNATE_AUTO + 2) = w1;
        *(volatile uint16_t*)(entry + F7_OFF_INNATE_AUTO + 4) = w2;
        touched = true;
    }
    // Imunidades a status: 25 bytes em +0x641 (1 = imune)
    bool anyResist = false;
    for (int i = 0; i < F7_STATUS_COUNT; ++i) {
        if (p.statusResist[i]) { anyResist = true; break; }
    }
    if (anyResist) {
        for (int i = 0; i < F7_STATUS_COUNT; ++i) {
            if (p.statusResist[i])
                *(volatile uint8_t*)(entry + F7_OFF_STATUS_RESIST + i) = 1;
        }
        touched = true;
    }
    return touched;
}


// ── Difficulty apply (walks enemy list, slots 0..7) ──────────────────
int F7_DifficultyAppliedCount() {
    return g_appliedCount;
}

bool F7_DifficultyInBattle() {
    return InterlockedCompareExchange(&g_inBattle, 0, 0) != 0;
}

static bool F7_DifficultyApplyWithPreset(const F7DifficultyPreset& p) {
    uint8_t* list = EnemyListBase();
    if (!list) return false;
    int applied = 0;
    for (int slot = 0; slot < 8; ++slot) {
        uint8_t* entry = list + F7_ENEMY_STRIDE * slot;
        if (!EnemySlotOccupied(entry)) continue;
        if (ApplyPresetToEnemy(entry, p)) ++applied;
    }
    g_appliedCount = applied;
    return applied > 0;
}

// N2: escolhe a regra por area (field row do ultimo encontro; fallback -1).
static bool F7_DifficultyPickAreaPreset(F7DifficultyPreset* out) {
    *out = F7DifficultyPreset{};
    if (!g_cfg.diffByArea || g_cfg.areaCount <= 0) return false;
    const int fieldRow = g_cfg.force.lastField;
    const F7AreaRule* match = nullptr;
    for (int i = 0; i < g_cfg.areaCount; ++i) {
        const F7AreaRule& r = g_cfg.areas[i];
        if (!r.enabled) continue;
        if (r.fieldRow == fieldRow) { match = &r; break; }
        if (r.fieldRow == -1 && !match) match = &r;   // fallback "qualquer area"
    }
    if (!match) return false;
    out->enabled = true;
    out->hpMul = match->hpMul;
    out->strMul = match->strMul;
    out->defMul = match->defMul;
    out->magMul = match->magMul;
    out->mdfMul = match->mdfMul;
    out->agiMul = match->agiMul;
    out->autoStatusMask = match->autoStatusMask;
    out->elemWeak = match->elemWeak;
    out->elemResist = match->elemResist;
    return true;
}

void F7_DifficultyApplyNow() {
    if (!g_enabled) return;
    uint8_t* list = EnemyListBase();
    if (!list) {
        F7_Log("[ffx-hooks] F7: difficulty apply SEM-LISTA (fora de batalha?)\n");
        return;
    }
    const uintptr_t lp = (uintptr_t)list;
    if (lp < 0x01000000u || lp >= 0x80000000u) {
        F7_Log("[ffx-hooks] F7: difficulty apply LISTA-INVALIDA ptr=0x%08X — abortado\n", (unsigned)lp);
        return;
    }
    bool any = false;
    __try {
        F7DifficultyPreset area = {};
        if (F7_DifficultyPickAreaPreset(&area)) {
            if (F7_DifficultyApplyWithPreset(area)) any = true;
        }
        if (g_cfg.diffGlobal.enabled) {
            if (F7_DifficultyApplyWithPreset(g_cfg.diffGlobal)) any = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        F7_Log("[ffx-hooks] F7: difficulty apply EXCECAO 0x%08X — abortado\n", (unsigned)GetExceptionCode());
        return;
    }
    F7_Log("[ffx-hooks] F7: difficulty apply %s (inimigos=%d)\n",
        any ? "OK" : "SEM-ALVO", g_appliedCount);
}

bool F7_DifficultyTryAutoApply() {
    if (!g_enabled) return false;
    if (!InterlockedCompareExchange(&g_needApply, 0, 0)) return false;
    // stats prontos? (algum inimigo com Max_hp != 0)
    uint8_t* list = EnemyListBase();
    if (!list) return false;
    bool ready = false;
    for (int slot = 0; slot < 8; ++slot) {
        uint8_t* entry = list + F7_ENEMY_STRIDE * slot;
        if (EnemySlotOccupied(entry) && EnemyStatsReady(entry)) { ready = true; break; }
    }
    if (!ready) return false;
    InterlockedExchange(&g_needApply, 0);
    InterlockedExchange(&g_inBattle, 1);
    F7_DifficultyApplyNow();
    F7_ApplyMusicBattle();   // mesmthe window: musica de entrada da batalha
    return true;
}


// ── Music (reuses FFXHooksBlock override + MusicHook battle pending) ────
void F7_MusicApplyLock() {
    if (!g_block) return;
    if (g_cfg.music.lockTrack >= 0 && g_cfg.music.lockTrack <= 0xB5) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex),
                            g_cfg.music.lockTrack);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
        F7_Log("[ffx-hooks] F7: music lock -> %d\n", g_cfg.music.lockTrack);
    }
}

void F7_MusicClearOverride() {
    if (!g_block) return;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    F7_Log("[ffx-hooks] F7: music override limpo\n");
}

const char* F7_StatusName(int i) {
    if (i < 0 || i >= F7_STATUS_COUNT) return "?";
    return F7_STATUS_NAMES[i];
}

void F7_MusicPreview(int track) {
    if (!g_block || track < 0 || track > 0xB5) {
        // FIX 2026-08-02 (RT2): o usuario confirmava o Preview com o lock none (-1) e nada acontecia.
        F7_Log("[ffx-hooks] F7: music preview invalido track=%d (ajuste a faixa com L/R antes do Preview)\n", track);
        return;
    }
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), track);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    const unsigned int trigger = (track == 4) ? 7u : 4u;   // chain Lab-proved: override + soundcmd 23 trigger != desired
    int32_t ret = 0;
    bool ok = false;
    ArenaBattleMusicSoundCmdFn fn = GetArenaBattleMusicSoundCmdFn();
    if (fn) {
        // FIX 2026-08-02 (RT2): soundcmd via probe crashed (probe OFF — MMF with 0xCD freed data).
        // SEH: if probe is not alive, preview does NOT crash the game — uses override only.
        __try { ok = fn(trigger, &ret); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
            F7_Log("[ffx-hooks] F7: music preview soundcmd EXCEPTION (probe off? override apenas)\n");
        }
    }
    F7_Log("[ffx-hooks] F7: music preview track=%d trigger=%u soundcmd=%d ret=%d\n", track, trigger, ok ? 1 : 0, ret);
}

void F7_ResetMusic() {
    g_cfg.music.lockTrack = -1;
    g_cfg.music.battleTrack = -1;
    g_cfg.music.randomizer = false;
    g_cfg.music.fadeFrames = 0;
    g_cfg.music.playlistCount = 0;
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), -1);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
    }
    SetMusicHookMinFadeFrames(0);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music reset p/ padrao (lock=-1 battle=-1 randomizer=off fade=0)\n");
}

void F7_SetMusicLock(int track) {
    // FIX 2026-08-02 (RT2): track 0 = none (o 0 silencia o menu inicial / crasha na batalha).
    g_cfg.music.lockTrack = (track <= 0) ? -1 : (track > 0xB5 ? 0xB5 : track);
    F7_MusicApplyLock();
    F7_SaveConfig();
}

void F7_SetMusicBattleTrack(int track) {
    // FIX 2026-08-02 (RT2): track 0 = none (0 crashed on battle entry — auto-apply).
    g_cfg.music.battleTrack = (track <= 0) ? -1 : (track > 0xB5 ? 0xB5 : track);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music battle -> %d\n", g_cfg.music.battleTrack);
}

void F7_SetMusicRandomizer(bool on) {
    g_cfg.music.randomizer = on;
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music randomizer -> %s\n", on ? "ON" : "OFF");
}

void F7_SetMusicFade(int frames) {
    g_cfg.music.fadeFrames = (frames < 0) ? 0 : (frames > 600 ? 600 : frames);
    if (g_cfg.music.fadeFrames > 0)
        FfxHooks::SetMusicHookMinFadeFrames(g_cfg.music.fadeFrames);
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7: music fade -> %d\n", g_cfg.music.fadeFrames);
}

// Battle start: arms the entry music (MusicHook pending — one-shot consumption,
// expira em 45s, nao vaza para o campo). Requer hook de battle-entry instalado.
static void F7_ApplyMusicBattle() {
    int target = -1;
    int fade = (g_cfg.music.fadeFrames > 0) ? g_cfg.music.fadeFrames : 90;
    if (g_cfg.music.randomizer && g_cfg.music.playlistCount > 0) {
        const int idx = (int)(GetTickCount() % (unsigned)g_cfg.music.playlistCount);
        target = g_cfg.music.playlist[idx];
        F7_Log("[ffx-hooks] F7: randomizer sorteou faixa %d (playlist[%d])\n", target, idx);
    } else if (g_cfg.music.battleTrack >= 0) {
        target = g_cfg.music.battleTrack;
    } else {
        return;  // no battle track configured — keep vanilla music
    }
    if (target < 0) return;
    // Use MusicLock mechanism (g_block->musicOverrideTrackIndex) — works with ALL interceptors
    if (g_block) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&g_block->musicOverrideTrackIndex), target);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_block->musicSeq));
        F7_Log("[ffx-hooks] F7: battle music lock -> %d\n", target);
    }
    FfxHooks::SetMusicHookMinFadeFrames(fade);
}


// ── Force Last Battle ─────────────────────────────────────────────────────
int F7_LastEncounterField()  { return g_cfg.force.lastField; }
int F7_LastEncounterGroup()  { return g_cfg.force.lastGroup; }
bool F7_HasLastEncounter()   { return g_cfg.force.hasLast; }

void F7_SetRepeatCount(int n) {
    g_cfg.force.repeatCount = (n < 1) ? 1 : (n > 9 ? 9 : n);
    F7_SaveConfig();
}

// ── Força: scheduled by tick (no Sleep on main thread — fix 2026-08-02) ──
static volatile LONG g_forceRepeatLeft = 0;    // forces pendentes
static volatile LONG g_forceRepeatDelay = 0;   // frames entre forces

static volatile LONG g_skipForceCapture = 0;  // dllmain sets before non-natural launches
void F7_SetSkipForceCapture(bool v) { InterlockedExchange(&g_skipForceCapture, v ? 1 : 0); }

void F7_ForceLastBattle() {
    if (!g_base || !g_cfg.force.hasLast) {
        F7_Log("[ffx-hooks] F7: force last — sem ultima batalha capturada\n");
        return;
    }
    InterlockedExchange(&g_forceRepeatLeft, g_cfg.force.repeatCount);
    InterlockedExchange(&g_forceRepeatDelay, 0);
    F7_Log("[ffx-hooks] F7: force last agendado field=%d group=%d reps=%d (tick-based)\n",
        g_cfg.force.lastField, g_cfg.force.lastGroup, g_cfg.force.repeatCount);
}

// KEYSTONE B (2026-08-02): force with ARBITRARY field/group — o lever do F7 (o stepper do field).
void F7_ForceFieldBattle(int field, int group) {
    if (!g_base) return;
    g_cfg.force.hasLast = true;
    g_cfg.force.lastField = field;
    g_cfg.force.lastGroup = group;
    InterlockedExchange(&g_forceRepeatLeft, g_cfg.force.repeatCount > 0 ? g_cfg.force.repeatCount : 1);
    InterlockedExchange(&g_forceRepeatDelay, 0);
    F7_Log("[ffx-hooks] F7 lever: force field=%d group=%d (tick-based)\n", field, group);
}

// KEYSTONE B (2026-08-02): the F7 lever (row Difficulty) — the battle start auto-apply
// (F7_TickMainThread) le o g_cfg.diffGlobal; aqui so setamos o preset + salvar.
// Mapeamento explicito (permille): 0=x1.00 1=x1.15 2=x1.30 3=x1.50 4=x1.75 5=x2.00.
void F7_SetDifficultyLevel(int level) {
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    static const int kHpMulByLevel[6] = { 1000, 1150, 1300, 1500, 1750, 2000 };
    g_cfg.diffGlobal.enabled = true;
    g_cfg.diffGlobal.hpMul = kHpMulByLevel[level];
    F7_SaveConfig();
    F7_Log("[ffx-hooks] F7 lever: difficulty level=%d hpMul=%d (applies on next battle start)\n",
        level, g_cfg.diffGlobal.hpMul);
}

// chamado do F7_TickMainThread (1x/frame, main thread): 1 force por tick, ~10 frames entre eles
static void F7_ForceTick() {
    if (InterlockedCompareExchange(&g_forceRepeatLeft, 0, 0) <= 0) return;
    if (InterlockedCompareExchange(&g_forceRepeatDelay, 0, 0) > 0) {
        InterlockedDecrement(&g_forceRepeatDelay);
        return;
    }
    const LONG left = InterlockedDecrement(&g_forceRepeatLeft);
    if (left < 0) { InterlockedExchange(&g_forceRepeatLeft, 0); return; }
    typedef int (__cdecl* FnMsBattleEncountExe)(int, int, float);
    F7_SetSkipForceCapture(true);  // prevent self-capture
    FnMsBattleEncountExe fn = (FnMsBattleEncountExe)(g_base + RVA_MS_BATTLE_ENCOUNT);
    const int ret = fn(g_cfg.force.lastField, g_cfg.force.lastGroup, 0.0f);
    F7_Log("[ffx-hooks] F7: force tick (%d restantes) field=%d group=%d -> ret=%d\n",
        (int)(left > 0 ? left : 0), g_cfg.force.lastField, g_cfg.force.lastGroup, ret);
    if (left > 0) InterlockedExchange(&g_forceRepeatDelay, 10);   // ~166ms a 60fps entre forces
}

// ── Hooks (MinHook — read-only / post-original) ───────────────────────────
typedef unsigned __int8* (__cdecl* ResolveEncounterFn)(int a1, int* a2, int* a3, int* a4);
typedef int (__cdecl* InitSceneFn)();

static unsigned __int8* __cdecl ResolveEncounter_Shim(int a1, int* a2, int* a3, int* a4) {
    unsigned __int8* ret = ((ResolveEncounterFn)g_trampResolve)(a1, a2, a3, a4);
    if (g_skipForceCapture) { g_skipForceCapture = 0; return ret; }  // skip non-natural
    if (ret && a2 && a3) {
        g_cfg.force.lastField = *a2;
        g_cfg.force.lastGroup = *a3;
        g_cfg.force.lastFormation = 0;
        g_cfg.force.hasLast = true;
        F7_Log("[ffx-hooks] F7: ultimo encontro capturado field=%d group=%d\n", *a2, *a3);
    }
    return ret;
}

static int __cdecl InitScene_Shim() {
    const int ret = ((InitSceneFn)g_trampScene)();
    InterlockedExchange(&g_needApply, 1);
    F7_ApplyMusicBattle();  // apply music immediately (dont wait for stats)
    return ret;
}

void F7_TickMainThread() {
    if (!g_enabled) return;
    F7_ForceTick();              // force agendado por frames (sem Sleep)
    F7_DifficultyTryAutoApply();
}


// ── Install / Remove ──────────────────────────────────────────────────────
static bool F7_InstallDetour(uintptr_t targetRva, void* shim, void** tramp, const char* name) {
    if (!g_base || !shim || !tramp) return false;
    void* target = (void*)(g_base + targetRva);
    MH_STATUS st = MH_CreateHook(target, shim, tramp);
    if (st != MH_OK) {
        F7_Log("[ffx-hooks] F7: MH_CreateHook(%s) falhou (%d)\n", name, (int)st);
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        F7_Log("[ffx-hooks] F7: MH_EnableHook(%s) falhou (%d)\n", name, (int)st);
        return false;
    }
    F7_Log("[ffx-hooks] F7: hook %s instalado (RVA 0x%08X)\n", name, targetRva);
    return true;
}

bool F7_InstallHooks(uintptr_t base, FFXHooksBlock* block, void (*log)(const char*)) {
    if (g_enabled) return true;
    if (!base) return false;
    g_base = base;
    g_block = block;
    g_log = log;

    // Gate: config\f7_inlive.flag OU FFXHOOKS_ENABLE_F7=1 (OFF por padrao)
    const bool flagGate = ModuleFileExists("modules\\config\\f7_inlive.flag") ||
                          ModuleFileExists("modules\\f7_inlive.flag") ||
                          EnvFlagOn("FFXHOOKS_ENABLE_F7");
    if (!flagGate) {
        F7_Log("[ffx-hooks] F7: desabilitado (crie modules\\config\\f7_inlive.flag ou FFXHOOKS_ENABLE_F7=1)\n");
        return false;
    }
    g_enabled = true;
    F7_LoadConfig();

    bool ok = true;
    ok &= F7_InstallDetour(RVA_RESOLVE_ENCOUNTER, (void*)&ResolveEncounter_Shim, &g_trampResolve, "ResolveEncounterToken");
    ok &= F7_InstallDetour(RVA_INIT_SYSTEM_SCENE, (void*)&InitScene_Shim, &g_trampScene, "InitSystemSceneAndActorTable");
    if (g_cfg.music.fadeFrames > 0)
        FfxHooks::SetMusicHookMinFadeFrames(g_cfg.music.fadeFrames);
    if (g_cfg.music.lockTrack >= 0)
        F7_MusicApplyLock();
    F7_Log("[ffx-hooks] F7: instalado ok=%d (difficulty=%s, force=%s, music lock=%d)\n",
        ok ? 1 : 0, g_cfg.diffGlobal.enabled ? "ON" : "OFF",
        g_cfg.force.hasLast ? "tem-ultima" : "sem-ultima", g_cfg.music.lockTrack);
    return ok;
}

void F7_RemoveHooks() {
    if (!g_enabled) return;
    if (g_base && g_trampResolve) {
        MH_DisableHook((void*)(g_base + RVA_RESOLVE_ENCOUNTER));
        MH_RemoveHook((void*)(g_base + RVA_RESOLVE_ENCOUNTER));
        g_trampResolve = nullptr;
    }
    if (g_base && g_trampScene) {
        MH_DisableHook((void*)(g_base + RVA_INIT_SYSTEM_SCENE));
        MH_RemoveHook((void*)(g_base + RVA_INIT_SYSTEM_SCENE));
        g_trampScene = nullptr;
    }
    if (g_block) F7_MusicClearOverride();
    g_enabled = false;
    g_block = nullptr;
    g_log = nullptr;
    F7_Log("[ffx-hooks] F7: hooks removidos\n");
}

#endif // FFXHOOKS_HAVE_POLYHOOK
} // namespace FfxHooks

