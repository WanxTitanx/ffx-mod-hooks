#pragma once
// FieldProbeHook — RT2 lab logger for overworld encounter zones + map texture streaming.
// Evidence:
//   docs/reverse/FFX_MAPOUT_VPA_ENCOUNTER_ZONES_IDA_2026-06-16.md
//   docs/reverse/FFX_PHYRE_TEXTURE_LOAD_HOOK_SPEC_2026-06-07.md

#include <cstdint>

namespace FfxHooks {

    typedef void (*FieldProbeLogFn)(const char* message);

    struct FieldProbeInstallResult {
        bool ok;
        unsigned hookedCount;
    };

    FieldProbeInstallResult InstallFieldProbeHook(
        uintptr_t moduleBase,
        bool enableEncounterLog,
        bool enableTextureLog,
        FieldProbeLogFn log);

    bool RemoveFieldProbeHook(FieldProbeLogFn log);
    bool IsFieldProbeHookInstalled();

} // namespace FfxHooks
