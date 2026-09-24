#pragma once
// Legacy S.I.N. writer containment boundary.
//
// The old implementation launched an external disk materializer from a field hook. This adapter
// remains deliberately unavailable on every profile so old call sites stay source-compatible
// without retaining disk or RAM write authority. Production S.I.N. is separately wired through
// SinRamScalingCore and F7Difficulty's sole RAM writer; it never falls back through this API.

#include <cstdint>

namespace FfxHooks {

    struct SinCurseInstallResult {
        bool ok;
        unsigned hookedCount;
        const char* reason;
    };

    SinCurseInstallResult InstallSinCurseHook(uintptr_t moduleBase, void* logFn);
    bool RemoveSinCurseHook();
    bool IsSinCurseHookInstalled();

    // Compatibility accessors remain neutral while the legacy writer is inert.
    const char* GetCurrentRegion();
    int GetCurrentThreatCap();
    const char* GetCurrentField();

} // namespace FfxHooks
