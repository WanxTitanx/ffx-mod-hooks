#pragma once
// AbilitySfxHook — read-only log of battle streaming SFX (FmodSfx::playBattleStreaming).
// Evidence: docs/reverse/FFX_ABILITY_SFX_FMOD_STREAMING_INFERNO_2026-06-15.md

#include <cstdint>

namespace FfxHooks {

    typedef void (*AbilitySfxLogFn)(const char* message);

    struct AbilitySfxInstallResult {
        bool    ok;
        uintptr_t playBattleStreamingTrampoline;
        uintptr_t handoffTrampoline;
    };

    AbilitySfxInstallResult InstallAbilitySfxHook(
        uintptr_t moduleBase,
        bool enableLog,
        AbilitySfxLogFn log);

    bool RemoveAbilitySfxHook(AbilitySfxLogFn log);
    bool IsAbilitySfxHookInstalled();

} // namespace FfxHooks
