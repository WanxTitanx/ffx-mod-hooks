#pragma once
// NovaSuperDamageHook — scoped bypass of the 99.999 single-hit damage cap for Kimahri Nova.
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

    /* Independent Nova damage and Ronso Mana gates share atomic installation.
       Only actor 3 / cmd 0x3073 / HP component may bypass the upper cap. */
    NovaSuperDamageInstallResult InstallNovaSuperDamageHook(
        uintptr_t base,
        bool bypass,
        bool logHits,
        bool ronsoMana,
        NovaSuperDamageLogFn log);
    bool RemoveNovaSuperDamageHook(NovaSuperDamageLogFn log);
    void RequestNovaSuperDamageStop(); // atomic admission close; safe for detach fallback
    bool IsNovaSuperDamageHookInstalled();

} // namespace FfxHooks
