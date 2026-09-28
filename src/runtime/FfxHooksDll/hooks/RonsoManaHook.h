#pragma once
// RonsoManaHook — Kimahri-only partial Overdrive pool (Ronso Mana Blue Mage).
// Status: LAB — wired via kimahri_ronso_mana.flag (log-only default).
//
// See docs/reverse/FFX_RONSO_MANA_HOOK_IDA_2026-06-15.md
// Wiring: docs/reverse/FFX_RONSO_MANA_DLL_WIRING_PENDING.md

#include <stdint.h>

namespace FfxHooks {

    typedef void (*RonsoManaLogFn)(const char* message);

    struct RonsoManaInstallResult {
        bool ok;
        uintptr_t stubGate;
        uintptr_t stubGreyout;
        uintptr_t stubDrain;
    };

    /* Phase A: logOnly=true installs detours that only log (no behavior change). */
    RonsoManaInstallResult InstallRonsoManaHook(
        uintptr_t base,
        bool enableGate,
        bool enableGreyout,
        bool enableDrain,
        bool logOnly,
        RonsoManaLogFn log);

    bool RemoveRonsoManaHook(RonsoManaLogFn log);
    bool IsRonsoManaHookInstalled();

} // namespace FfxHooks
