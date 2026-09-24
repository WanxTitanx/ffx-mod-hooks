#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::FieldScoutAdmission {

// Keep one explicit family per production detour. Besides making delayed-entry tests exhaustive,
// this prevents a newly added shim from silently bypassing the process-sticky admission check.
enum class ShimFamily : uint8_t {
    BuildTextureSlot = 0,
    GraphicFieldMapLoad,
    LoadAndActivateDriver,
    GetInstanceNameByIndex,
    BattleEncounter,
    WireInstanceToSceneNodes,
    CommitInstanceMappings,
    ChrSetWorldPosition,
    TakaraLoad,
    WarpActor,
    SampleZoneSlot,
    Count,
};

inline constexpr size_t kShimFamilyCount =
    static_cast<size_t>(ShimFamily::Count);

enum class PathTransition : uint8_t {
    QuiesceBattle = 0,
    ResumeField,
};

struct State {
    // Shutdown and quiesce share one atomic word so a path callback cannot clear quiesce after
    // teardown has made shutdown sticky. Bitwise close is an absorbing process-lifetime state.
    std::atomic<uint32_t> word{0x3u};
    std::atomic<uint32_t> threadStarts{0u};
};

struct WaitIo {
    void* context = nullptr;
    void (*pause)(void*, uint32_t milliseconds) = nullptr;
    uint32_t timeoutMs = 0u;
};

void InitializeClosed(State*);
bool Open(State*);
void RequestClose(State*);
bool CloseAndDrainThreadStarts(State*, const WaitIo&);
bool IsShuttingDown(const State&);
bool IsQuiesced(const State&);
bool TryEnterAfterPrologue(State*, ShimFamily);
bool ShouldSkipCapture(const State&, bool battleActive);
bool ApplyPathTransition(State*, PathTransition, bool battleActive);
bool TryAcquireThreadStart(State*);
void ReleaseThreadStart(State*);
uint32_t ActiveThreadStarts(const State&);

} // namespace FfxHooks::FieldScoutAdmission
