#include "FieldProbeHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

using MsBattleEncountExeFn = int(__cdecl*)(int selector, int group, float walkDelta);
using DecodeEncounterGroupFn = int(__cdecl*)(int polyMetaDword);
using ResolveZoneIndicesFn = void(*)();
using BuildTextureSlotFn = int(__cdecl*)(int64_t a0, int a1, int a2, uint32_t* a3, char* source, int a5);

static bool                 g_installed = false;
static bool                 g_logEncounter = false;
static bool                 g_logTexture = false;
static FieldProbeLogFn      g_logFn = nullptr;
static uintptr_t            g_base = 0;

static volatile LONG        g_encounterLogCount = 0;
static volatile LONG        g_decodeLogCount = 0;
static volatile LONG        g_zoneLogCount = 0;
static volatile LONG        g_textureLogCount = 0;

#ifdef FFXHOOKS_HAVE_POLYHOOK
static MsBattleEncountExeFn       g_encounterTrampoline = nullptr;
static DecodeEncounterGroupFn     g_decodeTrampoline = nullptr;
static ResolveZoneIndicesFn       g_zoneTrampoline = nullptr;
static BuildTextureSlotFn         g_textureTrampoline = nullptr;
static PLH::x86Detour*            g_encounterDetour = nullptr;
static PLH::x86Detour*            g_decodeDetour = nullptr;
static PLH::x86Detour*            g_zoneDetour = nullptr;
static PLH::x86Detour*            g_textureDetour = nullptr;
static uint64_t                   g_encounterTrampolineVa = 0;
static uint64_t                   g_decodeTrampolineVa = 0;
static uint64_t                   g_zoneTrampolineVa = 0;
static uint64_t                   g_textureTrampolineVa = 0;
#endif

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[768] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static bool PathContains(const char* path, const char* needle) {
    if (!path || !needle) return false;
    return strstr(path, needle) != nullptr;
}

static uint8_t ReadZoneByte(uintptr_t rva) {
    if (!g_base) return 0;
    __try {
        return *reinterpret_cast<const uint8_t*>(g_base + rva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

#ifdef FFXHOOKS_HAVE_POLYHOOK

static int __cdecl MsBattleEncountExe_FieldProbeHook(int selector, int group, float walkDelta) {
    if (g_logEncounter && walkDelta != 0.0f) {
        const long n = InterlockedIncrement(&g_encounterLogCount);
        if (n <= 400) {
            const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
            const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
            HookLog(
                "[field-probe] encounter #%ld sel=%d group=%d walk=%.3f zones=%u/%u",
                n,
                selector,
                group,
                static_cast<double>(walkDelta),
                static_cast<unsigned>(zoneA),
                static_cast<unsigned>(zoneB));
        }
    }
    return g_encounterTrampoline(selector, group, walkDelta);
}

static int __cdecl DecodeEncounterGroup_FieldProbeHook(int polyMetaDword) {
    const int decoded = g_decodeTrampoline(polyMetaDword);
    if (g_logEncounter) {
        const long n = InterlockedIncrement(&g_decodeLogCount);
        if (n <= 400) {
            HookLog(
                "[field-probe] polyMeta=0x%08X decoded=%d (shift17=0x%04X)",
                static_cast<unsigned>(polyMetaDword),
                decoded,
                static_cast<unsigned>((static_cast<unsigned>(polyMetaDword) >> 17) & 0x7FFFu));
        }
    }
    return decoded;
}

static void ResolveZoneIndices_FieldProbeHook() {
    g_zoneTrampoline();
    if (g_logEncounter) {
        const long n = InterlockedIncrement(&g_zoneLogCount);
        if (n <= 400) {
            const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
            const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
            HookLog("[field-probe] zoneResolve #%ld zones=%u/%u", n,
                static_cast<unsigned>(zoneA),
                static_cast<unsigned>(zoneB));
        }
    }
}

static int __cdecl BuildTextureSlot_FieldProbeHook(
    int64_t a0,
    int a1,
    int a2,
    uint32_t* a3,
    char* source,
    int a5) {
    if (g_logTexture && source && PathContains(source, "/map/") && PathContains(source, "/tex/")) {
        const long n = InterlockedIncrement(&g_textureLogCount);
        if (n <= 800) {
            HookLog("[field-probe] mapTex #%ld %s", n, source);
        }
    }
    return g_textureTrampoline(a0, a1, a2, a3, source, a5);
}

static bool InstallDetour(
    uintptr_t targetVa,
    uint64_t* trampolineOut,
    void* hookFn,
    void** origOut,
    PLH::x86Detour** detourOut,
    const char* label) {
    auto detour = new PLH::x86Detour(targetVa, reinterpret_cast<uint64_t>(hookFn), trampolineOut);
    if (!detour->hook()) {
        HookLog("[ffx-hooks] ERROR FieldProbe %s detour hook() failed @0x%08X", label, static_cast<unsigned>(targetVa));
        delete detour;
        return false;
    }
    *origOut = reinterpret_cast<void*>(*trampolineOut);
    *detourOut = detour;
    HookLog("[ffx-hooks] FieldProbe %s ok target=0x%08X trampoline=0x%llX",
        label,
        static_cast<unsigned>(targetVa),
        static_cast<unsigned long long>(*trampolineOut));
    return true;
}

static void UnhookDetour(PLH::x86Detour** detourOut) {
    if (!detourOut || !*detourOut) return;
    (*detourOut)->unHook();
    delete *detourOut;
    *detourOut = nullptr;
}

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace

FieldProbeInstallResult InstallFieldProbeHook(
    uintptr_t moduleBase,
    bool enableEncounterLog,
    bool enableTextureLog,
    FieldProbeLogFn log) {
    FieldProbeInstallResult result = { false, 0 };
    g_base = moduleBase;
    g_logFn = log;
    g_logEncounter = enableEncounterLog;
    g_logTexture = enableTextureLog;

    if (!enableEncounterLog && !enableTextureLog) {
        if (log) log("[ffx-hooks] FieldProbe install skipped: nothing enabled");
        return result;
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_installed)
        RemoveFieldProbeHook(log);

    unsigned hooked = 0;

    if (enableEncounterLog) {
        if (InstallDetour(moduleBase + RVA_FFX_BATTLE_ENCOUNTER_EXE,
                &g_encounterTrampolineVa,
                reinterpret_cast<void*>(&MsBattleEncountExe_FieldProbeHook),
                reinterpret_cast<void**>(&g_encounterTrampoline),
                &g_encounterDetour,
                "MsBattleEncountExe"))
            ++hooked;

        if (InstallDetour(moduleBase + RVA_FFX_FIELDMAP_DECODE_ENCOUNTER_GROUP,
                &g_decodeTrampolineVa,
                reinterpret_cast<void*>(&DecodeEncounterGroup_FieldProbeHook),
                reinterpret_cast<void**>(&g_decodeTrampoline),
                &g_decodeDetour,
                "DecodeEncounterGroup"))
            ++hooked;

        if (InstallDetour(moduleBase + RVA_FFX_FIELD_RESOLVE_ENCOUNTER_ZONE_INDICES,
                &g_zoneTrampolineVa,
                reinterpret_cast<void*>(&ResolveZoneIndices_FieldProbeHook),
                reinterpret_cast<void**>(&g_zoneTrampoline),
                &g_zoneDetour,
                "ResolveEncounterZoneIndices"))
            ++hooked;
    }

    if (enableTextureLog) {
        if (InstallDetour(moduleBase + RVA_FFX_PSDATA_BUILD_TEXTURE_SLOT_LOADTIME,
                &g_textureTrampolineVa,
                reinterpret_cast<void*>(&BuildTextureSlot_FieldProbeHook),
                reinterpret_cast<void**>(&g_textureTrampoline),
                &g_textureDetour,
                "BuildTextureSlotLoadTime"))
            ++hooked;
    }

    g_installed = hooked > 0;
    result.ok = g_installed;
    result.hookedCount = hooked;
    HookLog("[ffx-hooks] FieldProbe installed encounter=%d texture=%d hooks=%u base=0x%08X",
        enableEncounterLog ? 1 : 0,
        enableTextureLog ? 1 : 0,
        hooked,
        static_cast<unsigned>(moduleBase));
#else
    if (log) log("[ffx-hooks] WARN FieldProbe requires FFXHOOKS_HAVE_POLYHOOK");
#endif
    return result;
}

bool RemoveFieldProbeHook(FieldProbeLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    UnhookDetour(&g_encounterDetour);
    UnhookDetour(&g_decodeDetour);
    UnhookDetour(&g_zoneDetour);
    UnhookDetour(&g_textureDetour);
    g_encounterTrampoline = nullptr;
    g_decodeTrampoline = nullptr;
    g_zoneTrampoline = nullptr;
    g_textureTrampoline = nullptr;
#endif
    g_installed = false;
    if (log) log("[ffx-hooks] FieldProbe removed");
    return true;
}

bool IsFieldProbeHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
