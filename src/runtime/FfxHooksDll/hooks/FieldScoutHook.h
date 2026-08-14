#pragma once
// FieldScoutHook — walk-driven asset manifest (textures + player anchor + field token).
// Evidence:
//   docs/reverse/FFX_PHYRE_TEXTURE_LOAD_HOOK_SPEC_2026-06-07.md
//   docs/reverse/FFX_FIELD_SCOUT_WALK_MANIFEST_2026-06-17.md

#include <cstdint>

namespace FfxHooks {

    typedef void (*FieldScoutLogFn)(const char* message);

    /// <summary>
    /// ULTRA HEAVY categories. Each sub-flag is honored only when BOTH
    /// <c>field_scout_heavy.flag</c> AND <c>field_scout_ultra.flag</c> are present.
    /// Hooks that are RE-pending emit stub/sample lines instead of pretending full capture.
    /// </summary>
    struct FieldScoutUltraOptions {
        bool master = false;
        bool fieldLogic = false;   // triggers/warps/save, chest payload/state, NPC id, doors
        bool collision = false;    // walkmesh, battle camera zones (RE blocked)
        bool encounters = false;   // zone runtime calibration, polyMeta link, zone shapes
        bool sceneEnv = false;     // lights, weather, music, 2D layer hierarchy
        bool pipelineHints = false; // offline ingest: asset copy, rich shards (manifest hint)
    };

    struct FieldScoutInstallResult {
        bool ok;
        char sessionPath[512];
        unsigned uniqueAssets;
    };

    /// <summary>
    /// MAX MODE: requires <c>field_scout_max.flag</c> plus heavy + ultra master.
    /// Arms RE hooks (obtainTreasure/takara, warpToPoint, zone slot calibration) instead of ultra stubs.
    /// </summary>
    FieldScoutInstallResult InstallFieldScoutHook(
        uintptr_t moduleBase,
        void* moduleHandle,
        bool mapTexturesOnly,
        bool heavyMode,
        FieldScoutUltraOptions ultraOptions,
        bool maxMode,
        FieldScoutLogFn log);

    bool RemoveFieldScoutHook(FieldScoutLogFn log);
    bool ApplyFieldScoutQueuedHooks(FieldScoutLogFn log);
    bool IsFieldScoutHookInstalled();

} // namespace FfxHooks
