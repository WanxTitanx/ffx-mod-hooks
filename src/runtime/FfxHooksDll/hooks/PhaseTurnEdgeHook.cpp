// PhaseTurnEdgeHook — CTB turn-edge observer with callback bridge for runtime dispatch.
// See PhaseTurnEdgeHook.h for the contract and docs/ai/MONSTER_AI_PHASE_ROTATION_CTB_TURN_EDGE_PLAN_2026-06-28.md
// for the full plan and IDA evidence behind the chosen target.

#include "PhaseTurnEdgeHook.h"
#include "../shared/ffx_addresses.h"

#ifdef FFXHOOKS_HAVE_POLYHOOK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <polyhook2/Detour/x86Detour.hpp>
#include <exception>
#include <stdarg.h>
#include <stdio.h>
#endif

namespace FfxHooks {

#ifdef FFXHOOKS_HAVE_POLYHOOK

namespace {

static PLH::x86Detour*     g_detour = nullptr;
static uint64_t            g_trampoline = 0;
static PhaseTurnEdgeLogFn    g_logFn = nullptr;
static PhaseTurnEdgeCallback g_callback = nullptr;
static bool                  g_installed = false;
static uintptr_t             g_baseAddr = 0;
static volatile LONG         g_fireCount = 0;

// FFX_Battle_CtbEdgeOverdriveEvent signature (from IDA decompile):
//   int __cdecl(unsigned int n6, int a2)
//   n6 = actor slot index (0..6) from CTB order array
//   a2 = actor character struct pointer from sub_794030(n6)
//   Called from main battle tick (sub_791000) when a valid CTB turn edge passes.
typedef int (__cdecl *CtbEdgeEvent_t)(unsigned int n6, int a2);

static uint32_t g_lastActorIndex = 0;
static uintptr_t g_lastActorPtr = 0;

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[256] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_logFn(line);
}

// Read the battle-active flag to confirm we are in a real battle.
static uint32_t ReadBattleActiveFlag() {
    if (!g_baseAddr) return 0;
    const uintptr_t va = g_baseAddr + RVA_FFX_BATTLE_ACTIVE_FLAG;
    __try {
        return *reinterpret_cast<volatile uint32_t*>(va);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int __cdecl CtbEdgeOverdriveEvent_Shim(unsigned int n6, int a2) {
    // Call trampoline first — we observe, we don't block
    const int rv = ((CtbEdgeEvent_t)g_trampoline)(n6, a2);

    const uint32_t battleActive = ReadBattleActiveFlag();
    const long seq = InterlockedIncrement(&g_fireCount);

    g_lastActorIndex = n6;
    g_lastActorPtr = static_cast<uintptr_t>(a2);

    HookLog(
        "[ffx-hooks] PhaseTurnEdge #%ld battleActive=%lu n6=%u a2=0x%08X\n",
        seq, battleActive, n6, a2);

    if (g_callback && battleActive != 0) {
        PhaseTurnEdgeEvent ev = {};
        ev.battleActiveFlag = battleActive;
        ev.actorSlot        = n6;
        ev.actorPtr         = static_cast<uintptr_t>(a2);
        ev.sequenceNo       = seq;
        g_callback(ev);
    }
    return rv;
}

} // namespace

PhaseTurnEdgeInstallResult InstallPhaseTurnEdgeHook(uintptr_t base, PhaseTurnEdgeLogFn log) {
    PhaseTurnEdgeInstallResult result = { false, 0 };
    g_logFn = log;
    g_baseAddr = base;

    if (g_installed) {
        result.reasonCode = 2;
        return result;
    }

    const uint64_t targetVa = static_cast<uint64_t>(base + RVA_FFX_BATTLE_CTB_EDGE_OVERDRIVE_EVENT);
    HookLog("[ffx-hooks] PhaseTurnEdge installing detour at VA=0x%08X (RVA=0x%08X)",
        static_cast<unsigned>(targetVa),
        static_cast<unsigned>(RVA_FFX_BATTLE_CTB_EDGE_OVERDRIVE_EVENT));

    try {
        g_detour = new PLH::x86Detour(
            targetVa,
            reinterpret_cast<uint64_t>(&CtbEdgeOverdriveEvent_Shim),
            &g_trampoline);
        const bool hooked = g_detour->hook();
        if (!hooked) {
            HookLog("[ffx-hooks] PhaseTurnEdge hook() returned false; aborting install");
            delete g_detour;
            g_detour = nullptr;
            g_trampoline = 0;
            result.reasonCode = 4;
            return result;
        }
        g_installed = true;
        result.ok = true;
        HookLog("[ffx-hooks] PhaseTurnEdge detour installed; trampoline=0x%llX",
            static_cast<unsigned long long>(g_trampoline));
    } catch (const std::exception& ex) {
        HookLog("[ffx-hooks] PhaseTurnEdge install exception: %s", ex.what());
        delete g_detour; g_detour = nullptr; g_trampoline = 0;
        result.reasonCode = 4;
    } catch (...) {
        HookLog("[ffx-hooks] PhaseTurnEdge install unknown exception");
        delete g_detour; g_detour = nullptr; g_trampoline = 0;
        result.reasonCode = 4;
    }
    return result;
}

void RemovePhaseTurnEdgeHook() {
    if (!g_installed) return;
    if (g_detour) { g_detour->unHook(); delete g_detour; g_detour = nullptr; }
    g_trampoline = 0;
    g_installed = false;
    g_callback = nullptr;
}

bool IsPhaseTurnEdgeHookInstalled() { return g_installed; }
long PhaseTurnEdgeHookFireCount()   { return static_cast<long>(InterlockedCompareExchange(&g_fireCount, 0, 0)); }
uint32_t PhaseTurnEdgeLastActorIndex() { return g_lastActorIndex; }
uintptr_t PhaseTurnEdgeLastActorPtr() { return g_lastActorPtr; }
void SetPhaseTurnEdgeCallback(PhaseTurnEdgeCallback cb) { g_callback = cb; }

#else // !FFXHOOKS_HAVE_POLYHOOK

PhaseTurnEdgeInstallResult InstallPhaseTurnEdgeHook(uintptr_t /*base*/, PhaseTurnEdgeLogFn /*log*/) {
    return { false, 3 };
}
void RemovePhaseTurnEdgeHook() {}
bool IsPhaseTurnEdgeHookInstalled() { return false; }
long PhaseTurnEdgeHookFireCount()   { return 0; }
uint32_t PhaseTurnEdgeLastActorIndex() { return 0; }
uintptr_t PhaseTurnEdgeLastActorPtr() { return 0; }
void SetPhaseTurnEdgeCallback(PhaseTurnEdgeCallback /*cb*/) {}

#endif

} // namespace FfxHooks
