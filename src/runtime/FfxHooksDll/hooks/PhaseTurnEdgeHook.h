// PhaseTurnEdgeHook — log-only probe on the CTB turn-edge event dispatcher.
//
// Target: FFX_Battle_CtbEdgeOverdriveEvent @ PE RVA 0x003B13D0 (IDA flat 0x7B13D0).
// This function is called by the main battle tick (FFX_Btl_MainBattleTick) whenever
// a valid CTB turn edge occurs — i.e., "any valid turn passed". It is NOT called
// per frame, and it does NOT fire during battle init/setup.
//
// In vanilla this function routes to FFX_Battle_OverdriveAddClamp (0x3B15A0) to
// add overdrive charge to the next actor in line. We detour it to observe the
// edge without interfering with the overdrive charge logic.
//
// Gate: phase_turn_edge.flag (file) OR FFXHOOKS_ENABLE_PHASE_TURN_EDGE=1 (env).
//
// Phase 1 proved cadence/context; the current implementation also forwards the
// live actor slot/pointer to a runtime callback so dllmain can do guarded
// command dispatch from a sidecar.
//
// Reversibility: removing the flag (or DLL) is a clean revert; the hook only
// observes by default and never mutates the battle state.

#pragma once

#include <stdint.h>

namespace FfxHooks {

using PhaseTurnEdgeLogFn = void (*)(const char*);

struct PhaseTurnEdgeEvent {
    uint32_t        battleActiveFlag; // read from g_FFX_BattleActive at fire time
    uint32_t        actorSlot;        // slot/id of the actor at this CTB edge
    uintptr_t       actorPtr;         // actor record pointer passed by the vanilla dispatcher
    long            sequenceNo;       // 1-indexed, monotonic per hook lifetime
};

using PhaseTurnEdgeCallback = void (*)(const PhaseTurnEdgeEvent&);

struct PhaseTurnEdgeInstallResult {
    bool     ok;
    uint32_t reasonCode; // 0=ok, 1=disabled, 2=already_installed, 3=no_polyhook, 4=detour_failed
};

PhaseTurnEdgeInstallResult InstallPhaseTurnEdgeHook(uintptr_t base, PhaseTurnEdgeLogFn log);
void                       RemovePhaseTurnEdgeHook();
bool                       IsPhaseTurnEdgeHookInstalled();
long                       PhaseTurnEdgeHookFireCount();

// Get the actor slot index from the most recent CTB edge fire.
// Zero during idle (no battle).
uint32_t PhaseTurnEdgeLastActorIndex();
uintptr_t PhaseTurnEdgeLastActorPtr();

// Callback registration. The latest registered callback wins; pass nullptr to clear.
// Callbacks are invoked AFTER the vanilla trampoline returns, on the original (game)
// thread; they must be lock-free / non-blocking / non-throwing.
void SetPhaseTurnEdgeCallback(PhaseTurnEdgeCallback cb);

} // namespace FfxHooks
