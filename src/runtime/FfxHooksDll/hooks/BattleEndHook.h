// BattleEndHook — Arena+ Lane 3 scaffold: read-only detour on the FFX battle-end
// cleanup dispatcher, designed to feed the ArenaProgressSidecar with "battle just
// finished" events.
//
// Target: FFX_Battle_EndCleanupDispatcher @ PE RVA 0x0039E650 (IDA flat 0x79E650).
// Gate:   arena_plus_victory_hook.flag (file) OR FFXHOOKS_ENABLE_ARENA_PLUS_VICTORY_HOOK=1 (env).
//
// Semantic contract (current iteration — see RE doc for caveats):
//   1. The detour calls a small probe BEFORE the trampoline. The probe reads
//      *(uint32_t*)(g_base + RVA_FFX_BATTLE_END_EFFECT_HANDLE) as a "battle
//      effect handle" — non-zero indicates the dispatcher will actually do work.
//   2. The detour then invokes the trampoline (vanilla cleanup) unchanged.
//   3. If a callback is registered AND the handle was non-zero AND the redirect
//      did not produce a duplicate edge (debounced per-handle), the callback is
//      fired with a BattleEndEvent containing the handle.
//
// EXPLICITLY NOT YET HANDLED (TODO RT2 spike with Halyson):
//   - Distinguishing victory / defeat / escape — the old sub_888CE0/sub_888AF0
//     hypothesis was reconciled as input/pad, so the real battle-end outcome
//     word still needs RT2 capture. The event.result field always reads kUnknown
//     today.
//   - Mapping the handle to a stable battle_id / formation_id — depends on
//     correlation with the latest FFX_Field_ResolveEncounterToken result, which
//     is captured by ResolverLogHook but not yet plumbed across hooks.
//
// Plan: this scaffold lands the wiring (target + gate + callback API + sidecar
// integration point) so that once the RT2 spike fills in the two TODOs above,
// the only new code is in the callback body — the hook surface, the gates, the
// build wiring, and the sidecar API are already in place.
//
// Reversibility: removing the flag (or DLL) is a clean revert; the hook only
// observes and never mutates the battle state.

#pragma once

#include <stdint.h>

namespace FfxHooks {

using BattleEndLogFn = void (*)(const char*);

enum class BattleEndResult : uint32_t {
    kUnknown = 0,
    kVictory = 1,
    kDefeat  = 2,
    kEscape  = 3,
};

struct BattleEndEvent {
    uint32_t        effectHandle;     // raw value of [g_base + RVA_FFX_BATTLE_END_EFFECT_HANDLE] pre-clear
    uint32_t        nextEncounterTok; // FFX_Battle_GetNextEncounterToken() snapshot (0 if returning to field)
    BattleEndResult result;           // currently always kUnknown until RT2 spike
    long            sequenceNo;       // 1-indexed, monotonic per hook lifetime
};

using BattleEndCallback = void (*)(const BattleEndEvent&);

struct BattleEndInstallResult {
    bool     ok;
    uint32_t reasonCode; // 0=ok, 1=disabled, 2=already_installed, 3=no_polyhook, 4=detour_failed
};

BattleEndInstallResult InstallBattleEndHook(uintptr_t base, BattleEndLogFn log);
void                   RemoveBattleEndHook();
bool                   IsBattleEndHookInstalled();
long                   BattleEndHookFireCount();

// Callback registration. The latest registered callback wins; pass nullptr to clear.
// Callbacks are invoked AFTER the vanilla trampoline returns, on the original (game)
// thread; they must be lock-free / non-blocking / non-throwing.
void SetBattleEndCallback(BattleEndCallback cb);

} // namespace FfxHooks
