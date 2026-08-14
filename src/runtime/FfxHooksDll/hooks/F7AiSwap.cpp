// F7AiSwap.cpp - "F7 Monster AI Swap" (Jarvis-HOOK): per-monster/per-ability status injection.
//
// Lane Jarvis-HOOK. Gate: modules\\config\\f7_aiswap.flag / FFXHOOKS_ENABLE_F7_AISWAP=1.
//
// Config persistida em modules\\config\\f7_aiswap.json (atomico .tmp + MoveFileEx).
// RUNTIME (F7_TickMainThread): escaneia a enemy list; quando Stat_action de um inimigo muda e ha
// config (monsterId, anim1Id == Stat_action), aplica status no ALVO (Status_suffer + durations).
// Stat_action/Seck_target_id sao HINTS - apply governado por flag (ver MemoryChr.cs, fonte de verdade).
//
// OFFSETS de status alvo (MemoryChr.cs):
//   +0x606 u16 Status_suffer (bit i = StatusByteList i: Death=0..Slow=24)
//   +0x608 StatusDurationByteList turns (13 bytes: Sleep,Silence,Darkness,Shell,Protect,Reflect,
//          NulTide,NulBlaze,NulShock,NulFrost,Regen,Haste,Slow)
#include "F7AiSwap.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

namespace FfxHooks {

static uintptr_t  g_base    = 0;
static void (*g_log)(const char*) = nullptr;
static bool       g_enabled = false;
static F7AiSwapConfig g_cfg = {};

static const uint32_t RVA_ENEMY_LIST_PTR = 0x00D37634u;
static const uint32_t RVA_ENEMY_LIST_ALT = 0x00D34460u;
static const uint32_t F7_ENEMY_STRIDE    = 0xF90u;
static const uint32_t F7_OFF_MONSTER_ID  = 0x00Eu;
static const uint32_t F7_OFF_STAT_ACTION = 0xDD6u;
static const uint32_t F7_OFF_SECK_TARGET = 0x438u;
static const uint32_t F7_OFF_SUFFER      = 0x606u;
static const uint32_t F7_OFF_SUFFER_TURNS= 0x608u;

static uint8_t g_lastAction[8] = {};

static const char* const F7_AS_STATUS_NAMES[F7_AISWAP_STATUS_COUNT] = {
    "Death","Zombie","Petrify","Poison","PowerBreak","MagicBreak","ArmorBreak","MentalBreak",
    "Confuse","Berserk","Provoke","Threaten","Sleep","Silence","Darkness","Shell",
    "Protect","Reflect","NulTide","NulBlaze","NulShock","NulFrost","Regen","Haste","Slow"
};
static const int8_t F7_AS_DUR_IDX[F7_AISWAP_STATUS_COUNT] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    0,   // Sleep
    1,   // Silence
    2,   // Darkness
    -1,  // Shell
    -1,  // Protect
    -1,  // Reflect
    -1,  // NulTide
    -1,  // NulBlaze
    -1,  // NulShock
    -1,  // NulFrost
    -1,  // Regen
    -1,  // Haste
    -1   // Slow
};

void F7AiSwap_Log(const char* fmt, ...) {
    if (!g_log) return;
    char line[512] = {};
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(line);
}

bool F7AiSwap_IsEnabled() { return g_enabled; }

static bool EnvFlagOn(const char* name) {
    char value[16] = {};
    DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
    return len > 0 && (value[0]=='1'||value[0]=='y'||value[0]=='Y'||value[0]=='t'||value[0]=='T');
}

static bool ModuleFileExists(const char* relativePath) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
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
    _snprintf_s(out, cap, _TRUNCATE, "%s\\modules\\config\\f7_aiswap.json", path);
}

static bool ReadSmallFile(const char* path, char* out, size_t cap) {
    if (!path || !out || cap == 0) return false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart >= (LONGLONG)cap) { CloseHandle(h); return false; }
    DWORD read = 0;
    BOOL ok = ReadFile(h, out, (DWORD)size.QuadPart, &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out[read] = '\0';
    return true;
}

static int JsonInt(const char* json, const char* key, int def) {
    if (!json || !key) return def;
    char needle[64] = {}; _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':'); if (!p) return def;
    return atoi(p + 1);
}

static bool JsonBool(const char* json, const char* key, bool def) {
    if (!json || !key) return def;
    char needle[64] = {}; _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return def;
    p = strchr(p + strlen(needle), ':'); if (!p) return def;
    return strstr(p, "true") != nullptr;
}

static void LoadConfigFromJson(const char* json) {
    if (!json) return;
    g_cfg.enabled = JsonBool(json, "enabled", false);
    g_cfg.entryCount = 0;
    const char* entriesKey = strstr(json, "\"entries\"");
    if (!entriesKey) return;
    const char* cursor = entriesKey; const char* end = json + strlen(json);
    while (cursor && cursor < end && g_cfg.entryCount < F7_AISWAP_ENTRIES_MAX) {
        const char* es = strstr(cursor, "{"); if (!es) break;
        const char* eobj = es;
        int depth = 0;
        const char* ee = nullptr;
        for (const char* q = es; q && q < end; ++q) {
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) { ee = q; break; } }
        }
        if (!ee) break;
        int monId = JsonInt(es, "monsterId", -1);
        if (monId < 0) { cursor = ee + 1; continue; }
        F7AiSwapEntry& E = g_cfg.entries[g_cfg.entryCount];
        E.monsterId = (uint16_t)monId;
        E.abilityCount = 0;
        const char* ab = es;
        const char* abkey = strstr(ab, "\"abilities\"");
        ab = abkey ? abkey : ee;
        while (ab && ab < ee && E.abilityCount < F7_AISWAP_ABILITIES_MAX) {
            const char* as = strchr(ab, '{'); if (!as || as >= ee) break;
            int adepth = 0; const char* ae = nullptr;
            for (const char* q = as; q && q <= ee; ++q) {
                if (*q == '{') adepth++;
                else if (*q == '}') { adepth--; if (adepth == 0) { ae = q; break; } }
            }
            if (!ae) break;
            int aid = JsonInt(as, "abilityId", -1);
            if (aid >= 0) {
                F7AiSwapAbility& A = E.abilities[E.abilityCount];
                memset(&A, 0, sizeof(A));
                A.abilityId = (uint16_t)aid;
                const char* stKey = strstr(as, "\"status\"");
                if (stKey && stKey < ae) {
                    const char* sb = strchr(stKey, '[');
                    const char* sbe = sb ? strchr(sb, ']') : nullptr;
                    if (sb && sbe && sbe < ae) {
                        const char* q = sb + 1; int idx = 0;
                        while (q < sbe && idx < F7_AISWAP_STATUS_COUNT) {
                            while (q < sbe && (*q==' '||*q==','||*q=='\n'||*q=='\r'||*q=='\t')) ++q;
                            if (q >= sbe) break;
                            A.statusOnHit[idx] = (uint8_t)atoi(q);
                            ++idx;
                            while (q < sbe && *q != ',' && *q != ']') ++q;
                        }
                    }
                }
                E.abilityCount++;
            }
            ab = ae + 1;
        }
        g_cfg.entryCount++;
        cursor = ee + 1;
    }
}

const F7AiSwapConfig& F7AiSwap_GetConfig() { return g_cfg; }

bool F7AiSwap_SaveConfig() {
    char path[MAX_PATH] = {}; ResolveConfigPath(path, sizeof(path));
    char tmp[MAX_PATH] = {}; _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", path);
    char buf[32768] = {};
    size_t used = 0;
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
        "{\n  \"$schema\": \"./f7_aiswap.schema.json\",\n  \"version\": 1,\n  \"enabled\": %s,\n  \"entries\": [\n",
        g_cfg.enabled ? "true" : "false");
    for (int e = 0; e < g_cfg.entryCount; ++e) {
        const F7AiSwapEntry& EN = g_cfg.entries[e];
        used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
            "    { \"monsterId\": \"0x%04X\", \"abilities\": [\n", (int)EN.monsterId);
        for (int a = 0; a < EN.abilityCount; ++a) {
            const F7AiSwapAbility& A = EN.abilities[a];
            used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
                "      { \"abilityId\": \"0x%04X\", \"status\": [", (int)A.abilityId);
            for (int s = 0; s < F7_AISWAP_STATUS_COUNT; ++s)
                used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "%s%d", s ? "," : "", (int)A.statusOnHit[s]);
            used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "] }%s\n", (a + 1 < EN.abilityCount) ? "," : "");
        }
        used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
            "    ] }%s\n", (e + 1 < g_cfg.entryCount) ? "," : "");
    }
    used += (size_t)_snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE, "  ]\n}\n");
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { F7AiSwap_Log("[ffx-hooks] AiSwap: save open fail (err=%lu)\n", GetLastError()); return false; }
    DWORD written = 0; WriteFile(h, buf, (DWORD)used, &written, nullptr); CloseHandle(h);
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) { F7AiSwap_Log("[ffx-hooks] AiSwap: save move fail\n"); return false; }
    F7AiSwap_Log("[ffx-hooks] AiSwap: config salva (%zu bytes)\n", used);
    return true;
}

static uint8_t* EnemyListBase() {
    if (!g_base) return nullptr;
    uint32_t ptr = *(volatile uint32_t*)(g_base + RVA_ENEMY_LIST_PTR);
    if (ptr < g_base || ptr >= g_base + 0x08000000u) {
        uint32_t alt = *(volatile uint32_t*)(g_base + RVA_ENEMY_LIST_ALT);
        if (alt >= g_base && alt < g_base + 0x08000000u) return (uint8_t*)alt;
        return nullptr;
    }
    return (uint8_t*)ptr;
}

static F7AiSwapEntry* FindEntry(uint16_t monsterId) {
    for (int e = 0; e < g_cfg.entryCount; ++e) if (g_cfg.entries[e].monsterId == monsterId) return &g_cfg.entries[e];
    return nullptr;
}
static F7AiSwapAbility* FindAbility(F7AiSwapEntry* E, uint16_t abilityId) {
    if (!E) return nullptr;
    for (int a = 0; a < E->abilityCount; ++a) if (E->abilities[a].abilityId == abilityId) return &E->abilities[a];
    return nullptr;
}

static int ApplyStatusToTarget(uint8_t* target, const F7AiSwapAbility& A) {
    if (!target) return 0;
    int applied = 0;
    uint16_t suffer = *(volatile uint16_t*)(target + F7_OFF_SUFFER);
    uint8_t turns[13] = {};
    memcpy(turns, target + F7_OFF_SUFFER_TURNS, sizeof(turns));
    for (int s = 0; s < F7_AISWAP_STATUS_COUNT; ++s) {
        if (A.statusOnHit[s] == 0) continue;
        suffer |= (uint16_t)(1u << s);
        if (F7_AS_DUR_IDX[s] >= 0)
            turns[F7_AS_DUR_IDX[s]] = A.statusOnHit[s];
        applied++;
    }
    *(volatile uint16_t*)(target + F7_OFF_SUFFER) = suffer;
    memcpy(target + F7_OFF_SUFFER_TURNS, turns, sizeof(turns));
    return applied;
}

int F7AiSwap_ApplyAbilityNow(uint16_t monsterId, uint16_t abilityId) {
    uint8_t* list = EnemyListBase();
    if (!list || !g_cfg.enabled) return 0;
    for (int s = 0; s < 8; ++s) {
        uint8_t* entry = list + F7_ENEMY_STRIDE * s;
        if (*(volatile uint16_t*)(entry + F7_OFF_MONSTER_ID) == 0xFFFFu) continue;
        if (*(volatile uint16_t*)(entry + F7_OFF_MONSTER_ID) != monsterId) continue;
        F7AiSwapEntry* E = FindEntry(monsterId);
        F7AiSwapAbility* A = FindAbility(E, abilityId);
        if (!A) return 0;
        int n = ApplyStatusToTarget(entry, *A);
        F7AiSwap_Log("[ffx-hooks] AiSwap: manual apply mon=0x%04X ab=0x%04X -> %d status (slot %d)\n",
            (int)monsterId, (int)abilityId, n, s);
        return n;
    }
    return 0;
}

void F7AiSwap_Tick() {
    if (!g_enabled || !g_cfg.enabled) return;
    uint8_t* list = EnemyListBase();
    if (!list) return;
    for (int s = 0; s < 8; ++s) {
        uint8_t* entry = list + F7_ENEMY_STRIDE * s;
        uint16_t monId = *(volatile uint16_t*)(entry + F7_OFF_MONSTER_ID);
        if (monId == 0xFFFFu) { g_lastAction[s] = 0; continue; }
        uint8_t action = *(volatile uint8_t*)(entry + F7_OFF_STAT_ACTION);
        if (action == g_lastAction[s]) continue;
        g_lastAction[s] = action;
        F7AiSwapEntry* E = FindEntry(monId);
        if (!E) continue;
        F7AiSwapAbility* A = FindAbility(E, action);
        if (!A) continue;
        uint8_t* target = entry;
        uint8_t tid = *(volatile uint8_t*)(entry + F7_OFF_SECK_TARGET);
        if (tid < 8) { uint8_t* alt = list + F7_ENEMY_STRIDE * tid; if (*(volatile uint16_t*)(alt + F7_OFF_MONSTER_ID) != 0xFFFFu) target = alt; }
        int n = ApplyStatusToTarget(target, *A);
        F7AiSwap_Log("[ffx-hooks] AiSwap: mon=0x%04X acao=0x%02X -> %d status no alvo (slot %d, tid=%d)\n",
            (int)monId, (int)action, n, s, (int)tid);
    }
}

bool F7AiSwap_Install(uintptr_t base, void (*log)(const char*)) {
    if (g_enabled) return true;
    if (!base) return false;
    g_base = base; g_log = log;
    const bool flagGate = ModuleFileExists("modules\\config\\f7_aiswap.flag") ||
                          ModuleFileExists("modules\\f7_aiswap.flag") ||
                          EnvFlagOn("FFXHOOKS_ENABLE_F7_AISWAP");
    if (!flagGate) {
        F7AiSwap_Log("[ffx-hooks] AiSwap: desabilitado (modules\\config\\f7_aiswap.flag ou FFXHOOKS_ENABLE_F7_AISWAP=1)\n");
        return false;
    }
    g_enabled = true;
    char path[MAX_PATH] = {}; ResolveConfigPath(path, sizeof(path));
    char json[16384] = {};
    if (ReadSmallFile(path, json, sizeof(json))) LoadConfigFromJson(json);
    else g_cfg = F7AiSwapConfig{};
    memset(g_lastAction, 0, sizeof(g_lastAction));
    F7AiSwap_Log("[ffx-hooks] AiSwap: instalado (enabled=%s, entries=%d)\n", g_cfg.enabled ? "ON" : "OFF", g_cfg.entryCount);
    return true;
}

bool F7AiSwap_Reload() {
    if (!g_base) return false;
    char path[MAX_PATH] = {}; ResolveConfigPath(path, sizeof(path));
    char json[16384] = {};
    if (!ReadSmallFile(path, json, sizeof(json))) { F7AiSwap_Log("[ffx-hooks] AiSwap: reload — arquivo nao encontrado\n"); return false; }
    LoadConfigFromJson(json);
    memset(g_lastAction, 0, sizeof(g_lastAction));
    F7AiSwap_Log("[ffx-hooks] AiSwap: reload OK (enabled=%s, entries=%d)\n", g_cfg.enabled ? "ON" : "OFF", g_cfg.entryCount);
    return true;
}

void F7AiSwap_Remove() {
    g_enabled = false; g_base = 0; g_log = nullptr;
    F7AiSwap_Log("[ffx-hooks] AiSwap: removido\n");
}

} // namespace FfxHooks
