#pragma once
// Shared upper-clamp owner, retaining the opt-in legacy Kimahri Nova policy.
// Status: LAB — coordinated detour at the upper CMP (RVA 0x0038EDD3).
// The native writeback and incoming lower-floor branch remain intact.
//
// See docs/reverse/FFX_NOVA_SUPER_DAMAGE_HOOK_LAB_SPEC_2026-06-15.md

#include <stdint.h>

namespace FfxHooks {

    typedef void (*NovaSuperDamageLogFn)(const char* message);

    struct NovaSuperDamageInstallResult {
        bool ok;
        uintptr_t stub;
    };

    /* Nova, finite combat policies and Ronso Mana share atomic installation.
       The unbounded bypass remains restricted to actor 3 / 0x3073 / HP.
       Finite requests use the current shared producer context. */
    NovaSuperDamageInstallResult InstallNovaSuperDamageHook(
        uintptr_t base,
        bool bypass,
        bool logHits,
        bool ronsoMana,
        NovaSuperDamageLogFn log);
    bool RemoveNovaSuperDamageHook(NovaSuperDamageLogFn log);
    void RequestNovaSuperDamageStop(); // atomic admission close; safe for detach fallback
    bool IsNovaSuperDamageHookInstalled();
    bool IsCombatDamageClampInstalled();

} // namespace FfxHooks
