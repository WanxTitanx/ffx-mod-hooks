#pragma once
// NulWardHook — Radiant Ward (320 / 0x3140) + Umbral Ward (321 / 0x3141).
// Status: LAB — wired in dllmain.cpp when nul_ward.flag / nul_ward_apply.flag.

#include <stdint.h>

namespace FfxHooks {

    typedef void (*NulWardLogFn)(const char* message);

    struct NulWardInstallOptions {
        bool nativeSlots = false;   /* actor+0x613/+0x614 instead of side-map */
        bool experimentP16 = false; /* detour ResolveHitDamagePrecheck (log) */
        bool p16Apply = false;      /* P16 returns 0 when ward blocks (needs experimentP16) */
    };

    struct NulWardInstallResult {
        bool ok;
        uintptr_t stubWriteback;
        uintptr_t detourAftermath;
        uintptr_t detourHitLoop;
        uintptr_t detourPrecheck;
    };

    /* applyBlocks=true consumes blocks and zeros damage; logEvents=true always logs (cap 128). */
    NulWardInstallResult InstallNulWardHook(
        uintptr_t base,
        bool applyBlocks,
        bool logEvents,
        NulWardLogFn log,
        const NulWardInstallOptions* options = nullptr);

    bool RemoveNulWardHook(NulWardLogFn log);
    bool IsNulWardHookInstalled();

} // namespace FfxHooks
