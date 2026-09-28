#pragma once
// NulWardHook — Radiant Ward (320 / 0x3140) + Umbral Ward (321 / 0x3141).
// Status: LAB — wired in dllmain.cpp when nul_ward.flag / nul_ward_apply.flag.

#include <stdint.h>

namespace FfxHooks {

    typedef void (*NulWardLogFn)(const char* message);

    struct NulWardInstallOptions {
        bool nativeSlots = false;   /* compatibility option; external charges only */
        bool experimentP16 = false; /* compatibility option; shared Nul entry */
        bool p16Apply = false;      /* compatibility option; shared Nul entry */
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
    void RequestNulWardDetachStop() noexcept;
    bool IsNulWardHookInstalled();

} // namespace FfxHooks
