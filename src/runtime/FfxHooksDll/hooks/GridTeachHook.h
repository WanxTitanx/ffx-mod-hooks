#pragma once
#include <cstdint>
namespace FfxHooks {
// Shared native copy observation, independent of command learning.
bool StartNativeSaveLoadEvents(std::uintptr_t module);
bool NativeSaveLoadEventsReady() noexcept;
void RequestNativeSaveLoadEventsStop() noexcept;
bool RemoveNativeSaveLoadEvents() noexcept;
}
// GridTeachHook — sphere-grid node activation teaches commands (NOT born-with grant).
// Save/character-bound sidecar records extended learned commands without native bank writes.
// Menu bound 374; ids 320-373 use validated rows and the shared learned-state owner.

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
    bool StartGridTeachSaveTracking(uintptr_t base, GridTeachLogFn log);
    bool IsGridTeachLearningReady();
    void RequestGridTeachStop() noexcept;

} // namespace FfxHooks
