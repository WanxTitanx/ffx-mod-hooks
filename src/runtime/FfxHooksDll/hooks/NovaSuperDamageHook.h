#pragma once
// NovaSuperDamageHook — scoped bypass of the 99.999 single-hit damage cap for Kimahri Nova.
// Status: LAB — inline patch @ FFX_Battle_DamageCap_ClampJle (RVA 0x78EDD5).
//
// See docs/reverse/FFX_NOVA_SUPER_DAMAGE_HOOK_LAB_SPEC_2026-06-15.md

#include <stdint.h>

namespace FfxHooks {

    typedef void (*NovaSuperDamageLogFn)(const char* message);

    struct NovaSuperDamageInstallResult {
        bool ok;
        uintptr_t stub;
    };

    /* bypass=true removes clamp for cmd 0x3073; logOnly=true logs pre-clamp without bypass. */
    NovaSuperDamageInstallResult InstallNovaSuperDamageHook(
        uintptr_t base,
        bool bypass,
        bool logHits,
        NovaSuperDamageLogFn log);
    bool RemoveNovaSuperDamageHook(NovaSuperDamageLogFn log);
    bool IsNovaSuperDamageHookInstalled();

} // namespace FfxHooks
