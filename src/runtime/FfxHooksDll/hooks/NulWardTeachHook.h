#pragma once
// NulWardTeachHook — I20 lab: menu bound 320→322 + optional grant Radiant/Umbral on load.

#include <stdint.h>

namespace FfxHooks {

    typedef void (*NulWardTeachLogFn)(const char* message);

    struct NulWardTeachInstallResult {
        bool ok;
        uintptr_t menuBoundPatchVa;
        bool menuBoundPatched;
    };

    NulWardTeachInstallResult InstallNulWardTeachHook(uintptr_t base, bool grantOnLoad, NulWardTeachLogFn log);
    bool RemoveNulWardTeachHook(NulWardTeachLogFn log);
    bool IsNulWardTeachHookInstalled();

} // namespace FfxHooks
