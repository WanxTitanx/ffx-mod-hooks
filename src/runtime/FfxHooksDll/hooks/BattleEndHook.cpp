// BattleEndHook — Arena+ Lane 3 scaffold implementation. See BattleEndHook.h
// for the contract and the doc at docs/reverse/FFX_ARENA_PLUS_BATTLE_END_HOOK_RE_2026-06-16.md
// for the IDA evidence behind the chosen target.

#include "BattleEndHook.h"
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

static PLH::x86Detour*   g_detour = nullptr;
static uint64_t          g_trampoline = 0;
static BattleEndLogFn    g_logFn = nullptr;
static BattleEndCallback g_callback = nullptr;
static bool              g_installed = false;
static uintptr_t         g_baseAddr = 0;
static volatile LONG     g_fireCount = 0;
static uint32_t          g_lastHandle = 0;

// FFX_Battle_EndCleanupDispatcher signature is bare (int __cdecl() with no args);
// the function returns the last `result` from sub_6871B0 (an int). Matching the
// vanilla prologue with a parameterless detour is mandatory for the PolyHook
// x86Detour stack to land cleanly.
typedef int (__cdecl *EndCleanup_t)();

static void HookLog(const char* fmt, ...) {
    if (!g_logFn) return;
    char line[256] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_logFn(line);
}

// Read the pre-call snapshot of the battle-effect handle. Returns 0 if we cannot
// reach it for any reason; the rest of the shim treats 0 as "skip event".
static uint32_t ReadHandlePreCall() {
    if (!g_baseAddr) return 0;
    const uintptr_t va = g_baseAddr + RVA_FFX_BATTLE_END_EFFECT_HANDLE;
    __try {
        return *reinterpret_cast<volatile uint32_t*>(va);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Snapshot the "next encounter token" via the renamed accessor. Read-only call;
// returns 0 if absent or if we have not located the base, which the hook treats
// as "no chained battle, returning to field".
static uint32_t ReadNextEncounterTokenSnapshot() {
    if (!g_baseAddr) return 0;
    typedef int (__cdecl *GetTok_t)();
    const uintptr_t va = g_baseAddr + RVA_FFX_BATTLE_GET_NEXT_ENCOUNTER_TOKEN;
    __try {
        return static_cast<uint32_t>(((GetTok_t)(va))());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int __cdecl BattleEnd_Shim() {
    const uint32_t handle = ReadHandlePreCall();
    const int rv = ((EndCleanup_t)g_trampoline)();

    // Debounce: the cleanup wrapper is called once per battle teardown, but the
    // handle stays put for a single dispatcher tick. We use g_lastHandle to
    // collapse rapid repeats — if the handle was 0 (no real teardown), skip.
    if (handle == 0) {
        return rv;
    }
    if (handle == g_lastHandle) {
        g_lastHandle = 0;
        return rv;
    }
    g_lastHandle = handle;

    const long seq = InterlockedIncrement(&g_fireCount);
    const uint32_t nextTok = ReadNextEncounterTokenSnapshot();

    HookLog(
        "[ffx-hooks] BattleEnd #%ld handle=0x%08X nextTok=0x%08X result=unknown",
        seq, handle, nextTok);

    if (g_callback) {
        BattleEndEvent ev = {};
        ev.effectHandle      = handle;
        ev.nextEncounterTok  = nextTok;
        ev.result            = BattleEndResult::kUnknown;
        ev.sequenceNo        = seq;
        g_callback(ev);
    }
    return rv;
}

} // namespace

BattleEndInstallResult InstallBattleEndHook(uintptr_t base, BattleEndLogFn log) {
    BattleEndInstallResult result = { false, 0 };
    g_logFn = log;
    g_baseAddr = base;

    if (g_installed) {
        result.reasonCode = 2;
        return result;
    }

    const uint64_t targetVa = static_cast<uint64_t>(base + RVA_FFX_BATTLE_END_CLEANUP_DISPATCHER);
    HookLog("[ffx-hooks] BattleEnd installing detour at VA=0x%08X (RVA=0x%08X)",
        static_cast<unsigned>(targetVa),
        static_cast<unsigned>(RVA_FFX_BATTLE_END_CLEANUP_DISPATCHER));

    try {
        g_detour = new PLH::x86Detour(
            targetVa,
            reinterpret_cast<uint64_t>(&BattleEnd_Shim),
            &g_trampoline);
        const bool hooked = g_detour->hook();
        if (!hooked) {
            HookLog("[ffx-hooks] BattleEnd hook() returned false; aborting install");
            delete g_detour;
            g_detour = nullptr;
            g_trampoline = 0;
            result.reasonCode = 4;
            return result;
        }
        g_installed = true;
        result.ok = true;
        HookLog("[ffx-hooks] BattleEnd detour installed; trampoline=0x%llX",
            static_cast<unsigned long long>(g_trampoline));
    } catch (const std::exception& ex) {
        HookLog("[ffx-hooks] BattleEnd install exception: %s", ex.what());
        delete g_detour; g_detour = nullptr; g_trampoline = 0;
        result.reasonCode = 4;
    } catch (...) {
        HookLog("[ffx-hooks] BattleEnd install unknown exception");
        delete g_detour; g_detour = nullptr; g_trampoline = 0;
        result.reasonCode = 4;
    }
    return result;
}

void RemoveBattleEndHook() {
    if (!g_installed) return;
    if (g_detour) { g_detour->unHook(); delete g_detour; g_detour = nullptr; }
    g_trampoline = 0;
    g_installed = false;
    g_callback = nullptr;
    g_lastHandle = 0;
}

bool IsBattleEndHookInstalled() { return g_installed; }
long BattleEndHookFireCount()   { return static_cast<long>(InterlockedCompareExchange(&g_fireCount, 0, 0)); }
void SetBattleEndCallback(BattleEndCallback cb) { g_callback = cb; }

#else // !FFXHOOKS_HAVE_POLYHOOK

BattleEndInstallResult InstallBattleEndHook(uintptr_t /*base*/, BattleEndLogFn /*log*/) {
    return { false, 3 };
}
void RemoveBattleEndHook() {}
bool IsBattleEndHookInstalled() { return false; }
long BattleEndHookFireCount()   { return 0; }
void SetBattleEndCallback(BattleEndCallback /*cb*/) {}

#endif

} // namespace FfxHooks
