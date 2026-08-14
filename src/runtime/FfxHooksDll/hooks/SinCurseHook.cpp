#include "SinCurseHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

// ── Types ──────────────────────────────────────────────────────────────────
using GraphicFieldMapLoadFn = void(__cdecl*)(char* mapPath, int slot);

// ── State ───────────────────────────────────────────────────────────────────
static bool                     g_installed = false;
static uintptr_t                g_base = 0;
static void (*g_logFn)(const char*) = nullptr;

static char                     g_currentRegion[32] = {};
static char                     g_currentField[32] = {};
static int                      g_currentThreatCap = 0;
static int                      g_infectionSeed = 0;

// ── Area threat lookup ──────────────────────────────────────────────────────
struct AreaEntry {
    const char* fieldPrefix;
    const char* regionId;
    int threatMin;
    int threatMax;
};

static const AreaEntry kAreaTable[] = {
    // ── Tutorial (cap=0) ──────────────────────────────────────────────────
    {"map/titl",              "title_screen",          0, 0},
    {"map/zanar",             "zanarkand_tutorial",    0, 0},

    // ── Thunder Plains (cap=1) ────────────────────────────────────────────
    {"map/kami",              "thunder_plains",        0, 1},

    // ── Macalania Woods / Field (cap=2) ───────────────────────────────────
    {"map/mcyt",              "macalania_woods",       0, 2},
    {"map/mcfr",              "macalania_field",       0, 2},
    {"map/maca",              "macalania_woods_north", 0, 2},
    {"map/mala",              "macalania_lake",        0, 2},

    // ── Bikanel / Al Bhed (cap=3) ────────────────────────────────────────
    {"map/bika",              "bikanel",               0, 3},

    // ── Home (cap=4) ──────────────────────────────────────────────────────
    {"map/hiku",              "home",                  0, 4},

    // ── Guadosalam (cap=4) ────────────────────────────────────────────────
    {"map/gua",               "guadosalam",            0, 4},

    // ── Macalania Temple / Lake (cap=5) ───────────────────────────────────
    {"map/mctr",              "macalania_temple",      0, 5},

    // ── Sanubia Desert (cap=6) ────────────────────────────────────────────
    {"map/sabu",              "sanubia_desert",        0, 6},

    // ── Moonflow / Djose (cap=6) ─────────────────────────────────────────
    {"map/djyt",              "moonflow",              0, 6},

    // ── Mt. Gagazet (cap=6) ───────────────────────────────────────────────
    {"map/gaga",              "gagazet",               0, 6},

    // ── Zanarkand (cap=7) ─────────────────────────────────────────────────
    // NOTE: placed after map/zanar so tutorial matches first
    {"map/zan",               "zanarkand",             0, 7},

    // ── Calm Lands (cap=8) ────────────────────────────────────────────────
    {"map/calm",              "calm_lands",            0, 8},

    // ── Omega Ruins (cap=8) ───────────────────────────────────────────────
    {"map/omeg",              "omega_ruins",           0, 8},

    // ── CSV regions WITHOUT unique field prefix (cannot auto-detect) ──────
    // macalania_bosses  (cap=2) — shares prefix with mcfr/mctr
    // thunder_plains_cross (cap=5) — shares prefix map/kami
    // postgame  (cap=9) — no unique prefix; uses calm/omega/zan maps
    // ───────────────────────────────────────────────────────────────────────

    // ── Fallback ──────────────────────────────────────────────────────────
    {"map/",                  "unknown_region",        0, 0},
};

// ── Helpers ────────────────────────────────────────────────────────────────

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static bool ModuleFileExists(const char* relativePath) {
    char full[1024];
    HMODULE hm = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&ModuleFileExists), &hm))
        return false;

    DWORD len = GetModuleFileNameA(hm, full, sizeof(full) - 64);
    if (len == 0) return false;

    char* sep = strrchr(full, '\\');
    if (!sep) return false;
    size_t dirLen = sep - full;
    full[dirLen] = '\0';

    _snprintf_s(full + dirLen, sizeof(full) - dirLen, _TRUNCATE, "\\%s", relativePath);
    DWORD attr = GetFileAttributesA(full);
    return attr != INVALID_FILE_ATTRIBUTES;
}

static bool EnvFlagEnabled(const char* name) {
    char val[8] = {};
    return GetEnvironmentVariableA(name, val, sizeof(val)) > 0;
}

static bool ModuleFlagEnabled(const char* name) {
    return ModuleFileExists(name);
}

static bool SinCurseFlagEnabled() {
    return EnvFlagEnabled("FFXHOOKS_ENABLE_SIN_CURSE") ||
        ModuleFlagEnabled("sin_curse.flag") ||
        ModuleFlagEnabled("config\\sin_curse.flag");
}

static int ReadIntensity() {
    if (EnvFlagEnabled("FFXHOOKS_SIN_INTENSITY")) {
        char val[16] = {};
        if (GetEnvironmentVariableA("FFXHOOKS_SIN_INTENSITY", val, sizeof(val)) > 0) {
            int pct = atoi(val);
            if (pct >= 10 && pct <= 100) return pct;
        }
    }

    const char* candidates[] = {
        "sin_f7_intensity.flag",
        "config\\sin_f7_intensity.flag",
    };

    for (const char* path : candidates) {
        if (!ModuleFileExists(path)) continue;
        HMODULE hm = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&ReadIntensity), &hm);
        char full[1024];
        GetModuleFileNameA(hm, full, sizeof(full));
        char* sep = strrchr(full, '\\');
        if (!sep) continue;
        *sep = '\0';
        _snprintf_s(full + (sep - full), sizeof(full) - (sep - full), _TRUNCATE, "\\%s", path);
        FILE* f = nullptr;
        if (fopen_s(&f, full, "r") == 0 && f) {
            char val[32] = {};
            if (fgets(val, sizeof(val), f)) {
                fclose(f);
                size_t vlen = strlen(val);
                while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r' || val[vlen-1] == ' '))
                    val[--vlen] = '\0';
                int pct = atoi(val);
                if (pct >= 10 && pct <= 100) return pct;
            }
            fclose(f);
        }
        break;
    }
    return 60;
}

static const AreaEntry* ResolveArea(const char* mapPath) {
    if (!mapPath || !mapPath[0]) return nullptr;
    for (const auto& entry : kAreaTable) {
        if (strstr(mapPath, entry.fieldPrefix) != nullptr)
            return &entry;
    }
    return nullptr;
}

// ── XorShift32 ──────────────────────────────────────────────────────────────
static uint32_t g_rngState = 0;

static uint32_t XorShift32() {
    uint32_t x = g_rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rngState = x;
    return x;
}

// ── Hook functions ─────────────────────────────────────────────────────────

#ifdef FFXHOOKS_HAVE_POLYHOOK
static PLH::x86Detour*          g_fieldLoadDetour = nullptr;
static uint64_t                 g_fieldLoadTrampolineVa = 0;
#endif

// ── GraphicFieldMapLoad hook — calls SinScaleInject.exe ────────────────────
// This is the ONLY hook. All SIN logic (HP/stats/ATEL/name) is handled by
// the .exe, which modifies .bins on disk before the next battle loads them.
static void __cdecl GraphicFieldMapLoad_SinCurseHook(char* mapPath, int slot) {
    // Call original first — critical not to delay field loading
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_fieldLoadTrampolineVa)
        ((GraphicFieldMapLoadFn)g_fieldLoadTrampolineVa)(mapPath, slot);
#endif

    if (!mapPath || !mapPath[0]) return;

    const AreaEntry* area = ResolveArea(mapPath);
    if (!area) {
        HookLog("SinCurseHook field=%s region=<unknown>", mapPath);
        return;
    }

    // Skip battle map loads (btlmap/) — only process on field transitions
    if (strstr(mapPath, "btlmap/") != nullptr || strstr(mapPath, "btlmap\\") != nullptr) {
        HookLog("SinCurseHook field=%s region=%s T=%d — battle map, skip", mapPath, area->regionId, area->threatMin);
        return;
    }

    // Read runtime intensity
    int intensity = ReadIntensity();
    if (intensity <= 0 && !EnvFlagEnabled("FFXHOOKS_SIN_CURSE_FORCE")) {
        HookLog("SinCurseHook field=%s region=%s range=T%d~T%d intensity=off — SKIP",
            mapPath, area->regionId, area->threatMin, area->threatMax);
        return;
    }

    // Generate seed from map path FIRST, then roll T
    g_rngState = 0;
    uint32_t h = 0x811C9DC5u;
    const char* p = mapPath;
    while (*p) { h = (h ^ (uint8_t)*p) * 0x01000193u; p++; }
    g_rngState = h ^ (h << 16);

    // Roll a random threat cap within the area's range (after seeding)
    int rolledT = area->threatMin;
    if (area->threatMax > area->threatMin) {
        uint32_t range = (uint32_t)(area->threatMax - area->threatMin + 1);
        rolledT = area->threatMin + (int)(XorShift32() % range);
    }

    strncpy_s(g_currentRegion, sizeof(g_currentRegion), area->regionId, _TRUNCATE);
    strncpy_s(g_currentField, sizeof(g_currentField), mapPath, _TRUNCATE);
    g_currentThreatCap = rolledT;
    g_infectionSeed = (int)h;

    HookLog("SinCurseHook field=%s region=%s range=T%d~T%d rolled=T%d intensity=%d%% seed=%u",
        mapPath, area->regionId, area->threatMin, area->threatMax,
        rolledT, intensity, g_infectionSeed);

    // Skip areas with NO SIN capability at all (title screen, tutorial, menus, unknown).
    // Areas with threatMax > 0 but rolled T=0 still spawn the injector in restore-only mode,
    // so monster names/bins from a previous SIN area get cleaned up on transition.
    if (area->threatMax == 0) {
        HookLog("SinCurseHook field=%s region=%s T=%d — non-SIN area, skip",
            mapPath, area->regionId, rolledT);
        return;
    }

    // Launch SinScaleInject.exe to modify .bins for this area
    // Path: {DLL_DIR}tools\SinScaleInject.exe — funciona em qualquer PC
    char exePath[1024] = {};
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GraphicFieldMapLoad_SinCurseHook), &hm))
    {
        GetModuleFileNameA(hm, exePath, sizeof(exePath));
        char* sep = strrchr(exePath, '\\');
        if (sep) {
            *(sep + 1) = '\0';
            _snprintf_s(exePath + strlen(exePath), sizeof(exePath) - strlen(exePath), _TRUNCATE,
                "..\\data\\modules\\tools\\SinScaleInject\\SinScaleInject.exe");   // padrao tools do usuario (2026-08-02)
        }
    }

    char cmd[1024];
    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE,
        "\"%s\" --area %s --seed %u --t %d --intensity %d",
        exePath, area->regionId, h, rolledT, intensity);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 1000); // timeout 1s (async — process continues in bg)
        DWORD exitCode = 0;
        bool gotExitCode = GetExitCodeProcess(pi.hProcess, &exitCode);

        if (waitResult == WAIT_TIMEOUT) {
            HookLog("SinCurseHook SinScaleInject TIMEOUT for area=%s seed=%u T=%d", area->regionId, h, rolledT);
        } else if (gotExitCode && exitCode == 0) {
            HookLog("SinCurseHook SinScaleInject SUCCESS for area=%s seed=%u T=%d", area->regionId, h, rolledT);
        } else if (gotExitCode) {
            HookLog("SinCurseHook SinScaleInject FAILED area=%s seed=%u T=%d exitCode=%lu", area->regionId, h, rolledT, exitCode);
        } else {
            HookLog("SinCurseHook SinScaleInject UNKNOWN area=%s seed=%u T=%d (GetExitCodeProcess failed)", area->regionId, h, rolledT);
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        HookLog("SinCurseHook FAILED to launch SinScaleInject: %lu", GetLastError());
    }
}

// ── Install / Remove ────────────────────────────────────────────────────────

#ifdef FFXHOOKS_HAVE_POLYHOOK

static bool InstallDetour(const char* tag, uint64_t targetVa, void* hookFn, uint64_t* trampOut,
    PLH::x86Detour** detourOut)
{
    *detourOut = nullptr;
    *trampOut = 0;

    auto detour = new PLH::x86Detour(targetVa, (uint64_t)hookFn, trampOut);
    if (!detour->hook()) {
        HookLog("SinCurseHook FAIL detour %s @ 0x%llX", tag, targetVa);
        delete detour;
        return false;
    }
    *detourOut = detour;
    return true;
}

#endif

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

SinCurseInstallResult InstallSinCurseHook(uintptr_t moduleBase, void* logFn) {
    SinCurseInstallResult result = {};
    g_base = moduleBase;
    g_logFn = (void (*)(const char*))logFn;

    if (!SinCurseFlagEnabled()) {
        return result;
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    unsigned hooked = 0;

    if (InstallDetour(
        "GraphicFieldMapLoad",
        moduleBase + RVA_FFX_FIELDMAP_LOAD_ENTRY_GRAPHIC_FIELDMAP,
        (void*)&GraphicFieldMapLoad_SinCurseHook,
        &g_fieldLoadTrampolineVa,
        &g_fieldLoadDetour))
    {
        ++hooked;
    }

    if (hooked > 0) {
        g_installed = true;
        HookLog("SinCurseHook installed (v2 .bin-first, %u hook(s))", hooked);
        result.ok = true;
        result.hookedCount = hooked;
    }
#else
    HookLog("SinCurseHook not available (no PolyHook)");
#endif

    return result;
}

bool RemoveSinCurseHook() {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_fieldLoadDetour) {
        g_fieldLoadDetour->unHook();
        delete g_fieldLoadDetour;
        g_fieldLoadDetour = nullptr;
    }
#endif
    g_installed = false;
    g_logFn = nullptr;
    return true;
}

bool IsSinCurseHookInstalled() {
    return g_installed;
}

const char* GetCurrentRegion() {
    return g_currentRegion;
}

int GetCurrentThreatCap() {
    return g_currentThreatCap;
}

const char* GetCurrentField() {
    return g_currentField;
}

} // namespace FfxHooks
