#pragma once
// GridTeachHook — sphere-grid node activation teaches commands (NOT born-with grant).
// Sidecar records ids learned via grid or RT2 lab; battle-init bank reload re-applies bits.
// v4: menu bound 367, BuildMenu detour, shadow sidecar words 16-17 (ids 352-383) actor-seeded.

#include <stdint.h>

namespace FfxHooks {

    typedef void (*GridTeachLogFn)(const char* message);

    struct GridTeachInstallResult {
        bool ok;
        uintptr_t menuBoundPatchVa;
        bool menuBoundPatched;
    };

    GridTeachInstallResult InstallGridTeachHook(uintptr_t base, GridTeachLogFn log);
    bool RemoveGridTeachHook(GridTeachLogFn log);
    bool IsGridTeachHookInstalled();

} // namespace FfxHooks
