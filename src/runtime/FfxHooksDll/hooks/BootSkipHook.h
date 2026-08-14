// BootSkipHook — fast boot / title skip / continue-to-save (lab, OFF by default).
// Gate: config\fast_boot_skip.flag OR FFXHOOKS_ENABLE_FAST_BOOT_SKIP=1
// Observe default: FFXHOOKS_FAST_BOOT_OBSERVE_ONLY=1 (no mutations)
// Apply: FFXHOOKS_FAST_BOOT_SKIP_APPLY=1 (stub — RT2 gated)
//
// NOT wired in dllmain InstallHooks until WIRE-ME block is uncommented.

#pragma once

#include <stdint.h>

namespace FfxHooks {

using BootSkipLogFn = void (*)(const char*);

enum class BootPhase : uint32_t {
    kUnknown = 0,
    kBootOrTitle,
    kIntroScene,
    kSceneTransition,
    kMenuActive,
    kInField,
    kDone,
};

struct BootSkipConfig {
    bool observeOnly;     // true = log only (default)
    bool applySkip;       // FMV/title skip (stub)
    bool applyContinue;   // load save + warp (stub)
    int  saveSlot;        // 0..6, or -1 = newest ffx_NNN by mtime
};

struct BootSkipInstallResult {
    bool     ok;
    uint32_t reasonCode; // 0=ok, 1=disabled, 2=already_installed, 3=no_polyhook, 4=detour_failed
};

BootSkipInstallResult InstallBootSkipHook(uintptr_t base, BootSkipLogFn log, const BootSkipConfig& cfg);
void                  RemoveBootSkipHook();
bool                  IsBootSkipHookInstalled();
BootPhase             BootSkipCurrentPhase();
long                  BootSkipTickLogCount();

BootSkipConfig        BootSkipConfigFromEnvironment();

} // namespace FfxHooks
