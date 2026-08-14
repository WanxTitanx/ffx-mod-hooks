#pragma once
// SinCurseHook — runtime SIN curse application on field zone transitions.
// V1: field transition logging + infection selection + scale on monster spawn.
//
// Flags:
//   sin_curse.flag              — enable SIN curse runtime
//   sin_f7_intensity.flag       — percentage 10..100 (e.g. "60")
//
// Log: %TEMP%\ffx-hooks.log (via FfxHooks logger)

#include <cstdint>

namespace FfxHooks {

    struct SinCurseInstallResult {
        bool ok;
        unsigned hookedCount;
    };

    SinCurseInstallResult InstallSinCurseHook(uintptr_t moduleBase, void* logFn);
    bool RemoveSinCurseHook();
    bool IsSinCurseHookInstalled();

    // Runtime state accessors (for F7 SIN submenu)
    const char* GetCurrentRegion();
    int GetCurrentThreatCap();
    const char* GetCurrentField();

} // namespace FfxHooks
