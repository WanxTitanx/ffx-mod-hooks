#pragma once
// F7InLive.h - "FFX Editor - In-Live" (F7): Difficulty (RAM), Force Last Battle, Music.
//
// Lane: Jarvis-HOOK. Gate: modules\config\f7_inlive.flag OR FFXHOOKS_ENABLE_F7=1.
// Project standard: MinHook C++ hook + atomic JSON sidecar config (tmp + MoveFileEx)
// + MMF block (FFXHooksBlock_v1) for music override (same contract as the C# editor).
//
// Difficulty writes only fields with exact width/xref evidence in F7DifficultyCore.
// Element/status fields remain configuration-compatible but quarantined from RAM writes.
#include <stdint.h>
#include "F7DifficultyCore.h"
#include "SharedBattleRuntime.h"
#include "../shared/ffx_hooks_block.h"

namespace FfxHooks {
namespace SinAi {struct Context;}
namespace SinNatural {struct Evidence;}

// ── Persisted config (modules\config\f7_inlive.json) ─────────────────────
#define F7_AREA_RULES_MAX   16
#define F7_PLAYLIST_MAX     8
#define F7_STATUS_COUNT     25

using F7DifficultyPreset = F7Difficulty::Preset;
using F7AreaRule = F7Difficulty::AreaRule;

struct F7MusicConfig {
    int  lockTrack;        // -1 = none; else 0..0xB5 (FMOD runtime id)
    int  battleTrack;      // -1 = none (changes the battle ENTRY music)
    bool randomizer;       // picks from the playlist each battle
    int  fadeFrames;       // 0..600 (0 = the game's default)
    int  playlist[F7_PLAYLIST_MAX];
    int  playlistCount;
};

struct F7ForceConfig {
    int  lastField;        // field row key (ResolveEncounterToken *a2)
    int  lastGroup;        // group index (*a3)
    int  lastFormation;
    bool hasLast;
    int  repeatCount;      // 1..9 (how many force repeats to chain)
};

struct F7Config {
    F7DifficultyPreset diffGlobal;
    bool diffByArea;                    // N2 on/off
    F7AreaRule areas[F7_AREA_RULES_MAX];
    int  areaCount;
    F7MusicConfig music;
    F7ForceConfig force;
};

struct F7ConfigStateSnapshot {
    F7Config config{};
    F7Difficulty::DifficultyConfig difficulty{};
    bool difficultyValid = true;
    SinRam::Config sinRam{};
    bool sinRamValid = true;
    uint64_t revision = 0;
};

using F7ConfigSnapshotMutator = void (*)(
    F7Config*, F7Difficulty::DifficultyConfig*, bool*, void*);

// ── API (used by dllmain.cpp / menus) ──────────────────────────────────
bool F7_IsEnabled();
bool F7_InstallHooks(uintptr_t base, FFXHooksBlock* block, void (*log)(const char*),
                     bool sharedBattleRuntimeRequested, bool arenaMixRequested = false);
bool F7_ArenaMixEnabled();
bool F7_SharedBattleRuntimeReady(uintptr_t expectedModuleBase);
uintptr_t F7_SharedBattleInitSceneTarget();
SharedBattleRuntime::ComposerSlotResult F7_RegisterInitSceneComposer(
    const SharedBattleRuntime::ComposerIo* composer);
SharedBattleRuntime::ComposerSlotResult F7_UnregisterInitSceneComposer(
    const SharedBattleRuntime::ComposerIo* composer);
void F7_RequestStop();             // loader-lock safe: closes admission only
void F7_RemoveHooks();
void F7_TickMainThread();          // menu pump advances bounded Force and CustomMix deadlines only
F7ConfigStateSnapshot F7_GetConfigSnapshot();
void F7_ReplaceConfigSnapshot(
    const F7Config& config, const F7Difficulty::DifficultyConfig& difficulty,
    bool difficultyValid);
void F7_ReplaceConfigSnapshot(
    const F7Config& config, const F7Difficulty::DifficultyConfig& difficulty,
    bool difficultyValid, const SinRam::Config& sinRam, bool sinRamValid);
F7ConfigStateSnapshot F7_UpdateConfigSnapshot(
    F7ConfigSnapshotMutator mutator, void* context);
bool F7_SetSinRamConfig(const SinRam::Config& config);
void F7_SetDifficultyGlobal(const F7DifficultyPreset& preset);
bool F7_SaveConfig();              // atomic (.tmp + MoveFileEx)
void F7_Log(const char* fmt, ...);

// Music
void F7_SetMusicLock(int track);      // -1 = none
void F7_SetMusicBattleTrack(int track);
void F7_SetMusicRandomizer(bool on);
void F7_SetMusicFade(int frames);
void F7_SetDifficultyLevel(int level);        // KEYSTONE B (2026-08-02): F7 lever — 0..5 -> hpMul 1000..2000
void F7_MusicApplyLock();             // applies override on the block now
void F7_MusicClearOverride();
void F7_MusicPreview(int track);      // plays the track now (override + soundcmd) without persisting
bool F7_ResetMusic();                 // defaults: no lock/battle/randomizer/fade + saves
const char* F7_StatusName(int i);     // status name 0..24 (DIFF AUTO column)

// Force
void F7_ForceLastBattle();            // 1 click: MsBattleEncountExe(field, group, 0.0f) on the main thread
void F7_ForceFieldBattle(int field, int group);  // KEYSTONE B: force with arbitrary field/group
void F7_SetRepeatCount(int n);
int  F7_LastEncounterField();
int  F7_LastEncounterGroup();
bool F7_HasLastEncounter();

// Difficulty
void F7_DifficultyApplyNow();         // publishes a structured outcome for the current generation
bool F7_DifficultyInBattle();
bool F7_SinRequestedFromDisk();
int  F7_DifficultyAppliedCount();

enum class F7SinRamState : uint8_t {
    Invalid = 0,
    Off,
    Unavailable,
    WaitNatural,
    CurrentNatural,
};

struct F7SinRamRuntimeStatus {
    F7SinRamState state = F7SinRamState::Unavailable;
    SinRam::Config config{};
    bool configValid = true;
    uint64_t generation = 0;
    uint32_t request = 0;
    uint32_t areaVisit = 0;
    uint16_t areaField = 0;
    SinRam::Config battleConfig{};
    bool currentAssignment = false;
};

struct F7DifficultyRuntimeStatus {
    bool configured = false;
    bool difficultyValid = true;
    // Dedicated gate: a validated preset enables Difficulty behavior even when the broad
    // F7 master (Force/Music/S.I.N./AI) is OFF.
    bool difficultyBehaviorEnabled = false;
    // Shared detour batch state — independent of which consumers asked for it.
    bool infrastructureInstalled = false;
    bool callbackAdmissionOpen = false;
    // True while the runtime still owns writable fields, so an OFF preset can restore.
    bool ownedFieldsPresent = false;
    F7Difficulty::AdapterGateCode infrastructureGate =
        F7Difficulty::AdapterGateCode::InvalidArgument;
    F7Difficulty::RuntimeResult last{};
    uint64_t generation = 0;
    int32_t currentBattleField = -1;
    F7Difficulty::BattleFieldSource currentBattleFieldSource =
        F7Difficulty::BattleFieldSource::Missing;
    size_t pointersRejected = 0;
};

F7DifficultyRuntimeStatus F7_DifficultyStatus();
// Bounded read-only probe for startup planning: does the on-disk f7_inlive.json already
// carry a validated preset that enables Difficulty? Does not mutate runtime state.
bool F7_DifficultyRequestedFromDisk();
F7SinRamRuntimeStatus F7_SinRamStatus();
bool F7_SinAiContext(bool commandsReady,SinAi::Context*);
void F7_SinAiRegistered(unsigned actorSlot,std::uint64_t generation);
bool F7_SinObserveNaturalEncounter(const SinNatural::Evidence&);
void F7_SinObserveLocation(); // read-only location epoch; no actor/save writer
const char* F7_SinRamStateName(F7SinRamState state);
const char* F7_DifficultyResultName(F7Difficulty::ResultCode code);
const char* F7_DifficultyGateName(F7Difficulty::AdapterGateCode code);
void F7_PublishPendingBattleField(
    int32_t fieldRow, F7Difficulty::BattleFieldSource source);
F7Difficulty::BattleFieldRequest F7_BeginPendingBattleFieldRequest();
bool F7_CommitPendingBattleFieldRequest(
    F7Difficulty::BattleFieldRequest request, int32_t fieldRow,
    F7Difficulty::BattleFieldSource source);
void F7_CancelPendingBattleFieldRequest(
    F7Difficulty::BattleFieldRequest request);
void F7_BeginExplicitLaunchCapture();
void F7_EndExplicitLaunchCapture();

class F7ExplicitLaunchCaptureScope {
public:
    F7ExplicitLaunchCaptureScope() { F7_BeginExplicitLaunchCapture(); }
    ~F7ExplicitLaunchCaptureScope() { F7_EndExplicitLaunchCapture(); }
    F7ExplicitLaunchCaptureScope(const F7ExplicitLaunchCaptureScope&) = delete;
    F7ExplicitLaunchCaptureScope& operator=(const F7ExplicitLaunchCaptureScope&) = delete;
};

} // namespace FfxHooks
