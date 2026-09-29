#include "FieldProbeHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <atomic>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#include "RecoveryNative.h"
#include "FieldProbeEvidence.h"
#endif

namespace FfxHooks {

namespace {

using MsBattleEncountExeFn = int(__cdecl*)(int selector, int group, float walkDelta);
using DecodeEncounterGroupFn = int(__cdecl*)(int polyMetaDword);
using ResolveZoneIndicesFn = void(*)();
using BuildTextureSlotFn = int(__cdecl*)(int64_t a0, int a1, int a2, uint32_t* a3, char* source, int a5);

static std::atomic<bool>    g_installed{false};
static std::atomic<bool>    g_stopping{false};
static_assert(std::atomic<bool>::is_always_lock_free, "FieldProbe detach stop must not wait");
// These values are immutable once a publication attempt begins. A new request
// needs a process restart rather than replacing context used by old callbacks.
static bool                 g_logEncounter = false;
static bool                 g_logTexture = false;
static FieldProbeLogFn      g_logFn = nullptr;
static uintptr_t            g_base = 0;

static volatile LONG        g_encounterLogCount = 0;
static volatile LONG        g_decodeLogCount = 0;
static volatile LONG        g_zoneLogCount = 0;
static volatile LONG        g_textureLogCount = 0;

#ifdef FFXHOOKS_HAVE_POLYHOOK
static SRWLOCK                   g_adminLock = SRWLOCK_INIT;
static bool                      g_attempted = false;
static bool                      g_retired = false;
static bool                      g_retireResult = true;
static unsigned                  g_hookedCount = 0;
static PLH::x86Detour*            g_encounterDetour = nullptr;
static PLH::x86Detour*            g_decodeDetour = nullptr;
static PLH::x86Detour*            g_zoneDetour = nullptr;
static PLH::x86Detour*            g_textureDetour = nullptr;
// PLH's output storage is also the callback's forwarding source. Do not create a
// second pointer published only after hook() returns, or clear it during stop.
static uint64_t                   g_encounterTrampolineVa = 0;
static uint64_t                   g_decodeTrampolineVa = 0;
static uint64_t                   g_zoneTrampolineVa = 0;
static uint64_t                   g_textureTrampolineVa = 0;

struct AdminLock {
    bool held;
    AdminLock() noexcept : held(TryAcquireSRWLockExclusive(&g_adminLock) != FALSE) {}
    ~AdminLock() { if (held) ReleaseSRWLockExclusive(&g_adminLock); }
    AdminLock(const AdminLock&) = delete;
    AdminLock& operator=(const AdminLock&) = delete;
};
#endif

static bool Active() noexcept {
    return !g_stopping.load(std::memory_order_acquire) &&
        g_installed.load(std::memory_order_acquire);
}

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    const DWORD error = GetLastError();
    char line[768] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    try { g_logFn(line); } catch (...) { /* Diagnostics cannot replace native execution. */ }
    SetLastError(error);
}

static bool CopyDiagnosticPath(const char* source, char* destination, size_t capacity) {
    if (!source || !destination || capacity < 2) return false;
    destination[0] = '\0';
    __try {
        for (size_t i = 0; i < capacity; ++i) {
            const char value = source[i];
            destination[i] = value;
            if (value == '\0') return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        destination[0] = '\0';
        return false;
    }
    destination[0] = '\0';
    return false;
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

static bool ValidationOnlyOrInvalid() {
    char value[16] = {};
    const DWORD length = GetEnvironmentVariableA("FFXHOOKS_VALIDATE_ONLY", value, sizeof(value));
    if (length == 0) return false;
    // An unknown/truncated validation flag must not accidentally publish a hook.
    return length != 1 || value[0] != '0';
}

static int __cdecl MsBattleEncountExe_FieldProbeHook(int selector, int group, float walkDelta) {
    const DWORD error = GetLastError();
    if (Active() && g_logEncounter && walkDelta != 0.0f) {
        const long n = InterlockedIncrement(&g_encounterLogCount);
        if (n <= 400) {
            const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
            const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
            HookLog("[field-probe] encounter #%ld sel=%d group=%d walk=%.3f zones=%u/%u",
                n, selector, group, static_cast<double>(walkDelta),
                static_cast<unsigned>(zoneA), static_cast<unsigned>(zoneB));
        }
    }
    SetLastError(error);
    return reinterpret_cast<MsBattleEncountExeFn>(static_cast<uintptr_t>(g_encounterTrampolineVa))(
        selector, group, walkDelta);
}

static int __cdecl DecodeEncounterGroup_FieldProbeHook(int polyMetaDword) {
    const int decoded = reinterpret_cast<DecodeEncounterGroupFn>(
        static_cast<uintptr_t>(g_decodeTrampolineVa))(polyMetaDword);
    const DWORD error = GetLastError();
    if (Active() && g_logEncounter) {
        const long n = InterlockedIncrement(&g_decodeLogCount);
        if (n <= 400) {
            HookLog("[field-probe] polyMeta=0x%08X decoded=%d (shift17=0x%04X)",
                static_cast<unsigned>(polyMetaDword), decoded,
                static_cast<unsigned>((static_cast<unsigned>(polyMetaDword) >> 17) & 0x7FFFu));
        }
    }
    SetLastError(error);
    return decoded;
}

static void ResolveZoneIndices_FieldProbeHook() {
    reinterpret_cast<ResolveZoneIndicesFn>(static_cast<uintptr_t>(g_zoneTrampolineVa))();
    const DWORD error = GetLastError();
    if (Active() && g_logEncounter) {
        const long n = InterlockedIncrement(&g_zoneLogCount);
        if (n <= 400) {
            const uint8_t zoneA = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_A);
            const uint8_t zoneB = ReadZoneByte(RVA_FFX_FIELD_ENCOUNTER_ZONE_BYTE_B);
            HookLog("[field-probe] zoneResolve #%ld zones=%u/%u", n,
                static_cast<unsigned>(zoneA), static_cast<unsigned>(zoneB));
        }
    }
    SetLastError(error);
}

static int __cdecl BuildTextureSlot_FieldProbeHook(
    int64_t a0, int a1, int a2, uint32_t* a3, char* source, int a5) {
    const DWORD error = GetLastError();
    char path[512] = {};
    if (Active() && g_logTexture && CopyDiagnosticPath(source, path, sizeof(path)) &&
        strstr(path, "/map/") && strstr(path, "/tex/")) {
        const long n = InterlockedIncrement(&g_textureLogCount);
        if (n <= 800) HookLog("[field-probe] mapTex #%ld %s", n, path);
    }
    SetLastError(error);
    return reinterpret_cast<BuildTextureSlotFn>(static_cast<uintptr_t>(g_textureTrampolineVa))(
        a0, a1, a2, a3, source, a5);
}

static bool InstallDetour(
    uintptr_t targetVa, uint64_t* trampolineOut, void* hookFn,
    PLH::x86Detour** detourOut, const char* label) {
    // Retain even a failed publication attempt: hook() failure alone is not proof
    // that no thread can reach its generated storage. The pinned module owns it.
    *detourOut = new PLH::x86Detour(targetVa, reinterpret_cast<uint64_t>(hookFn), trampolineOut);
    if (!(*detourOut)->hook()) {
        HookLog("[ffx-hooks] ERROR FieldProbe %s detour hook() failed @0x%08X",
            label, static_cast<unsigned>(targetVa));
        return false;
    }
    HookLog("[ffx-hooks] FieldProbe %s ok target=0x%08X trampoline=0x%llX",
        label, static_cast<unsigned>(targetVa), static_cast<unsigned long long>(*trampolineOut));
    return true;
}

static bool RetireDetours() noexcept {
    if (g_retired) return g_retireResult;
    // PolyHook's Detour::unHook() frees m_trampoline internally. Retaining the
    // outer object does not retain forwarding code. Until an owned non-freeing
    // unpatch and drain protocol is validated, stop closes diagnostics but keeps
    // the pinned pass-through detours. It is not successful live removal.
    const bool result = !g_textureDetour && !g_zoneDetour &&
        !g_decodeDetour && !g_encounterDetour;
    g_retired = true;
    g_retireResult = result;
    return result;
}

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace

FieldProbeInstallResult InstallFieldProbeHook(
    uintptr_t moduleBase, bool enableEncounterLog, bool enableTextureLog, FieldProbeLogFn log) {
    FieldProbeInstallResult result = { false, 0 };
    if (!enableEncounterLog && !enableTextureLog) {
        if (log) log("[ffx-hooks] FieldProbe install skipped: nothing enabled");
        return result;
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    AdminLock lock;
    if (!lock.held) {
        if (log) log("[ffx-hooks] FieldProbe administration busy; request rejected");
        return result;
    }
    if (g_attempted) {
        if (Active() && moduleBase == g_base && enableEncounterLog == g_logEncounter &&
            enableTextureLog == g_logTexture && log == g_logFn) {
            result.ok = true;
            result.hookedCount = g_hookedCount;
        } else if (log) log("[ffx-hooks] FieldProbe request rejected; process restart required");
        return result;
    }
    if (!moduleBase || g_stopping.load(std::memory_order_acquire) || ValidationOnlyOrInvalid()) {
        if (log) log("[ffx-hooks] FieldProbe unavailable: base, stop or validation admission");
        return result;
    }
    // Validate all requested targets before creating any detour. This gate is
    // independent of F7/Scout; texture-only mode does not inspect encounter sites.
    const bool profile = RecoveryNative::Profile(moduleBase);
    const bool encounter = !enableEncounterLog || (profile &&
        RecoveryNative::Match(moduleBase,FieldProbeEvidence::Encounter) &&
        RecoveryNative::Match(moduleBase,FieldProbeEvidence::Decode) &&
        RecoveryNative::Match(moduleBase,FieldProbeEvidence::Zones));
    const bool texture = !enableTextureLog || (profile &&
        RecoveryNative::Match(moduleBase,FieldProbeEvidence::Texture));
    if (!profile || !encounter || !texture || g_stopping.load(std::memory_order_acquire)) {
        if (log) log("[ffx-hooks] FieldProbe unavailable: executable profile or requested target signature mismatch");
        return result;
    }
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(&InstallFieldProbeHook), &self) || !self) {
        if (log) log("[ffx-hooks] FieldProbe unavailable: callback module could not be pinned");
        return result;
    }
    g_attempted = true;
    g_base = moduleBase;
    g_logFn = log;
    g_logEncounter = enableEncounterLog;
    g_logTexture = enableTextureLog;
    unsigned hooked = 0;
    bool complete = true;
    try {
        if (enableEncounterLog) {
            complete = InstallDetour(moduleBase + RVA_FFX_BATTLE_ENCOUNTER_EXE,
                &g_encounterTrampolineVa, reinterpret_cast<void*>(&MsBattleEncountExe_FieldProbeHook),
                &g_encounterDetour, "MsBattleEncountExe");
            if (complete) ++hooked;
            if (complete) {
                complete = InstallDetour(moduleBase + RVA_FFX_FIELDMAP_DECODE_ENCOUNTER_GROUP,
                    &g_decodeTrampolineVa, reinterpret_cast<void*>(&DecodeEncounterGroup_FieldProbeHook),
                    &g_decodeDetour, "DecodeEncounterGroup");
                if (complete) ++hooked;
            }
            if (complete) {
                complete = InstallDetour(moduleBase + RVA_FFX_FIELD_RESOLVE_ENCOUNTER_ZONE_INDICES,
                    &g_zoneTrampolineVa, reinterpret_cast<void*>(&ResolveZoneIndices_FieldProbeHook),
                    &g_zoneDetour, "ResolveEncounterZoneIndices");
                if (complete) ++hooked;
            }
        }
        if (complete && enableTextureLog) {
            complete = InstallDetour(moduleBase + RVA_FFX_PSDATA_BUILD_TEXTURE_SLOT_LOADTIME,
                &g_textureTrampolineVa, reinterpret_cast<void*>(&BuildTextureSlot_FieldProbeHook),
                &g_textureDetour, "BuildTextureSlotLoadTime");
            if (complete) ++hooked;
        }
    } catch (...) {
        complete = false;
        HookLog("[ffx-hooks] FieldProbe publication threw; closing the partial request");
    }
    const unsigned required = (enableEncounterLog ? 3u : 0u) + (enableTextureLog ? 1u : 0u);
    if (!complete || hooked != required || g_stopping.load(std::memory_order_acquire)) {
        g_stopping.store(true, std::memory_order_release);
        g_installed.store(false, std::memory_order_release);
        const bool neutralized = RetireDetours();
        HookLog("[ffx-hooks] FieldProbe incomplete: hooked=%u required=%u neutralized=%d; diagnostics closed, storage retained, restart required",
            hooked, required, neutralized ? 1 : 0);
        return result;
    }
    g_hookedCount = hooked;
    g_installed.store(true, std::memory_order_release);
    result.ok = Active();
    result.hookedCount = result.ok ? hooked : 0;
    HookLog("[ffx-hooks] FieldProbe admitted encounter=%d texture=%d hooks=%u base=0x%08X; live reconfiguration requires restart",
        enableEncounterLog ? 1 : 0, enableTextureLog ? 1 : 0,
        result.hookedCount, static_cast<unsigned>(moduleBase));
    // The external logger may request a nonblocking stop. Do not return an
    // installation result captured before that callback revoked admission.
    result.ok = Active();
    result.hookedCount = result.ok ? hooked : 0;
#else
    if (log) log("[ffx-hooks] WARN FieldProbe requires FFXHOOKS_HAVE_POLYHOOK");
#endif
    return result;
}

void RequestFieldProbeStop() noexcept {
    g_stopping.store(true, std::memory_order_release);
    g_installed.store(false, std::memory_order_release);
}

bool RemoveFieldProbeHook(FieldProbeLogFn log) {
    // Nonblocking admission closure precedes normal-context retirement bookkeeping.
    RequestFieldProbeStop();
#ifdef FFXHOOKS_HAVE_POLYHOOK
    AdminLock lock;
    if (!lock.held) {
        if (log) log("[ffx-hooks] FieldProbe stop admission closed; administration busy, retry cleanup in normal context");
        return false;
    }
    const bool result = RetireDetours();
    if (log) log(result
        ? "[ffx-hooks] FieldProbe stopped without published detour storage"
        : "[ffx-hooks] FieldProbe diagnostics stopped; native pass-through detours retained, process restart required");
    return result;
#else
    if (log) log("[ffx-hooks] FieldProbe diagnostic admission closed");
    return true;
#endif
}

bool IsFieldProbeHookInstalled() {
    return Active();
}

} // namespace FfxHooks
