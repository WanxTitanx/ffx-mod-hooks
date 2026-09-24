#include "F7InLive.h"
#include "SinRamConfigCore.h"

#include <algorithm>
#include <mutex>

namespace FfxHooks {
namespace {

std::mutex g_f7ConfigStateLock;

void CopyCanonicalDifficultyToConfig(
    F7Config* config, const F7Difficulty::DifficultyConfig& difficulty) {
    if (!config) return;
    config->diffGlobal = difficulty.global;
    config->diffByArea = difficulty.byArea;
    const size_t areaCount = std::min(
        difficulty.areaCount, static_cast<size_t>(F7_AREA_RULES_MAX));
    config->areaCount = static_cast<int>(areaCount);
    for (size_t i = 0; i < areaCount; ++i) config->areas[i] = difficulty.areas[i];
    for (size_t i = areaCount; i < F7_AREA_RULES_MAX; ++i) config->areas[i] = {};
}

uint64_t NextRevision(uint64_t current) {
    ++current;
    return current == 0 ? 1 : current;
}

F7ConfigStateSnapshot MakeInitialConfigState() {
    F7ConfigStateSnapshot initial{};
    initial.config.music.lockTrack = -1;
    initial.config.music.battleTrack = -1;
    initial.config.force.lastField = -1;
    initial.config.force.lastGroup = -1;
    initial.config.force.repeatCount = 1;
    initial.difficulty = F7Difficulty::MakeNeutralConfig();
    initial.difficultyValid = true;
    initial.sinRam = {};
    initial.sinRamValid = true;
    CopyCanonicalDifficultyToConfig(&initial.config, initial.difficulty);
    return initial;
}

F7ConfigStateSnapshot g_f7ConfigState = MakeInitialConfigState();

bool SinConfigValid(const SinRam::Config& config) {
    char canonical[128] = {};
    return SinRamConfig::SerializeValue(config, canonical, sizeof(canonical)).code ==
        SinRamConfig::Code::Ok;
}

void ReplaceConfigSnapshotLocked(
    const F7Config& config, const F7Difficulty::DifficultyConfig& difficulty,
    bool difficultyValid, const SinRam::Config& sinRam, bool sinRamValid) {
    F7ConfigStateSnapshot replacement{};
    replacement.config = config;
    replacement.difficulty = difficulty;
    replacement.difficultyValid = difficultyValid;
    replacement.sinRamValid = sinRamValid && SinConfigValid(sinRam);
    replacement.sinRam = replacement.sinRamValid ? sinRam : SinRam::Config{};
    replacement.revision = NextRevision(g_f7ConfigState.revision);
    CopyCanonicalDifficultyToConfig(&replacement.config, replacement.difficulty);
    g_f7ConfigState = replacement;
}

}  // namespace

F7ConfigStateSnapshot F7_GetConfigSnapshot() {
    std::lock_guard<std::mutex> guard(g_f7ConfigStateLock);
    return g_f7ConfigState;
}

void F7_ReplaceConfigSnapshot(
    const F7Config& config, const F7Difficulty::DifficultyConfig& difficulty,
    bool difficultyValid) {
    std::lock_guard<std::mutex> guard(g_f7ConfigStateLock);
    ReplaceConfigSnapshotLocked(
        config, difficulty, difficultyValid, g_f7ConfigState.sinRam,
        g_f7ConfigState.sinRamValid);
}

void F7_ReplaceConfigSnapshot(
    const F7Config& config, const F7Difficulty::DifficultyConfig& difficulty,
    bool difficultyValid, const SinRam::Config& sinRam, bool sinRamValid) {
    std::lock_guard<std::mutex> guard(g_f7ConfigStateLock);
    // WHY: independent validity prevents a malformed sinRam object from
    // poisoning Difficulty/music/force, while invalid S.I.N. can publish only OFF/T0.
    ReplaceConfigSnapshotLocked(
        config, difficulty, difficultyValid, sinRam, sinRamValid);
}

F7ConfigStateSnapshot F7_UpdateConfigSnapshot(
    F7ConfigSnapshotMutator mutator, void* context) {
    std::lock_guard<std::mutex> guard(g_f7ConfigStateLock);
    if (mutator) {
        // WHY: callers may edit only local POD values here. Filesystem, logging, game calls,
        // and runtime work stay outside this lock so readers always receive an immutable copy.
        mutator(
            &g_f7ConfigState.config, &g_f7ConfigState.difficulty,
            &g_f7ConfigState.difficultyValid, context);
        CopyCanonicalDifficultyToConfig(
            &g_f7ConfigState.config, g_f7ConfigState.difficulty);
        g_f7ConfigState.revision = NextRevision(g_f7ConfigState.revision);
    }
    return g_f7ConfigState;
}

bool F7_SetSinRamConfig(const SinRam::Config& config) {
    if (!SinConfigValid(config)) return false;
    std::lock_guard<std::mutex> guard(g_f7ConfigStateLock);
    // WHY: Enabled and Threat are one typed edit. Publishing them under the
    // existing config lock prevents a menu/runtime reader from seeing a torn pair.
    g_f7ConfigState.sinRam = config;
    g_f7ConfigState.sinRamValid = true;
    g_f7ConfigState.revision = NextRevision(g_f7ConfigState.revision);
    return true;
}

}  // namespace FfxHooks
