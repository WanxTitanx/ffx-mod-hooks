#include "AbilitySfxHook.h"
#include "../shared/ffx_addresses.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <polyhook2/Detour/x86Detour.hpp>
#endif

namespace FfxHooks {

namespace {

using PlayBattleStreamingFn = void(__thiscall*)(void* self, int ctx, int sequenceId, int p3, int p4);
using BattleStreamingHandoffFn = int(__cdecl*)(int pendingA, int pendingB);

static bool                      g_installed = false;
static bool                      g_logEvents = false;
static AbilitySfxLogFn           g_logFn = nullptr;
static uintptr_t                 g_base = 0;

static volatile LONG             g_playLogCount = 0;
static volatile LONG             g_handoffLogCount = 0;

#ifdef FFXHOOKS_HAVE_POLYHOOK
static PlayBattleStreamingFn     g_playTrampoline = nullptr;
static BattleStreamingHandoffFn  g_handoffTrampoline = nullptr;
static PLH::x86Detour*           g_playDetour = nullptr;
static PLH::x86Detour*           g_handoffDetour = nullptr;
static uint64_t                  g_playTrampolineVa = 0;
static uint64_t                  g_handoffTrampolineVa = 0;
#endif

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[512] = {};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logFn(line);
}

static int ReadMagicId() {
    if (!g_base) return -1;
    const int32_t* p = reinterpret_cast<const int32_t*>(g_base + RVA_FFX_MAGIC_CURRENT_MAGIC_ID);
    __try {
        return *p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static int ReadStreamingSequenceId() {
    if (!g_base) return -1;
    const int32_t* p = reinterpret_cast<const int32_t*>(g_base + RVA_FFX_BATTLE_STREAMING_SEQUENCE_ID);
    __try {
        return *p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

#ifdef FFXHOOKS_HAVE_POLYHOOK

static void __fastcall PlayBattleStreaming_Hook(
    void* self,
    void* /*edx*/,
    int ctx,
    int sequenceId,
    int p3,
    int p4) {
    if (g_logEvents && InterlockedIncrement(&g_playLogCount) <= 500) {
        const int magicId = ReadMagicId();
        const int pendingSeq = ReadStreamingSequenceId();
        HookLog(
            "[ffx-hooks] AbilitySfx play #%ld magic=%d pendingSeq=%d sequenceId=%d ctx=%d p3=%d p4=%d",
            static_cast<long>(g_playLogCount),
            magicId,
            pendingSeq,
            sequenceId,
            ctx,
            p3,
            p4);
    }
    g_playTrampoline(self, ctx, sequenceId, p3, p4);
}

static int __cdecl BattleStreamingHandoff_Hook(int pendingA, int pendingB) {
    if (g_logEvents && InterlockedIncrement(&g_handoffLogCount) <= 500) {
        int seId = 0;
        int pick = pendingA ? pendingA : pendingB;
        if (pick) {
            __try {
                seId = *reinterpret_cast<const int*>(pick + 8);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                seId = -1;
            }
        }
        HookLog(
            "[ffx-hooks] AbilitySfx handoff #%ld magic=%d seId@+8=%d pendingA=0x%08X pendingB=0x%08X",
            static_cast<long>(g_handoffLogCount),
            ReadMagicId(),
            seId,
            static_cast<unsigned>(pendingA),
            static_cast<unsigned>(pendingB));
    }
    return g_handoffTrampoline(pendingA, pendingB);
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
        HookLog("[ffx-hooks] ERROR AbilitySfx %s detour hook() failed @0x%08X", label, static_cast<unsigned>(targetVa));
        delete detour;
        return false;
    }
    *origOut = reinterpret_cast<void*>(*trampolineOut);
    *detourOut = detour;
    HookLog("[ffx-hooks] AbilitySfx %s detour ok target=0x%08X trampoline=0x%llX",
        label,
        static_cast<unsigned>(targetVa),
        static_cast<unsigned long long>(*trampolineOut));
    return true;
}

#endif // FFXHOOKS_HAVE_POLYHOOK

} // namespace

AbilitySfxInstallResult InstallAbilitySfxHook(
    uintptr_t moduleBase,
    bool enableLog,
    AbilitySfxLogFn log) {
    AbilitySfxInstallResult result = { false, 0, 0 };
    g_base = moduleBase;
    g_logFn = log;
    g_logEvents = enableLog;

    if (!enableLog) {
        if (log) log("[ffx-hooks] AbilitySfx install skipped: log not requested");
        return result;
    }

#ifdef FFXHOOKS_HAVE_POLYHOOK
    if (g_installed)
        RemoveAbilitySfxHook(log);

    const uintptr_t playVa = moduleBase + RVA_FMOD_SFX_PLAY_BATTLE_STREAMING;
    const uintptr_t handoffVa = moduleBase + RVA_MAGIC_BATTLE_STREAMING_HANDOFF;

    if (!InstallDetour(playVa, &g_playTrampolineVa, &PlayBattleStreaming_Hook,
            reinterpret_cast<void**>(&g_playTrampoline), &g_playDetour, "playBattleStreaming"))
        return result;

    if (!InstallDetour(handoffVa, &g_handoffTrampolineVa, &BattleStreamingHandoff_Hook,
            reinterpret_cast<void**>(&g_handoffTrampoline), &g_handoffDetour, "handoff")) {
        RemoveAbilitySfxHook(log);
        return result;
    }

    g_installed = true;
    result.ok = true;
    result.playBattleStreamingTrampoline = static_cast<uintptr_t>(g_playTrampolineVa);
    result.handoffTrampoline = static_cast<uintptr_t>(g_handoffTrampolineVa);
    HookLog("[ffx-hooks] AbilitySfx installed log=1 base=0x%08X", static_cast<unsigned>(moduleBase));
#else
    if (log) log("[ffx-hooks] WARN AbilitySfx requires FFXHOOKS_HAVE_POLYHOOK");
#endif
    return result;
}

bool RemoveAbilitySfxHook(AbilitySfxLogFn log) {
#ifdef FFXHOOKS_HAVE_POLYHOOK
    bool ok = true;
    if (g_playDetour) {
        ok &= g_playDetour->unHook();
        delete g_playDetour;
        g_playDetour = nullptr;
    }
    if (g_handoffDetour) {
        ok &= g_handoffDetour->unHook();
        delete g_handoffDetour;
        g_handoffDetour = nullptr;
    }
    g_playTrampoline = nullptr;
    g_handoffTrampoline = nullptr;
    g_installed = false;
    if (log) log(ok ? "[ffx-hooks] AbilitySfx removed ok" : "[ffx-hooks] AbilitySfx remove FAILED");
    return ok;
#else
    (void)log;
    g_installed = false;
    return true;
#endif
}

bool IsAbilitySfxHookInstalled() {
    return g_installed;
}

} // namespace FfxHooks
