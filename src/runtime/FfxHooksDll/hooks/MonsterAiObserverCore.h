#pragma once

#include "MinHookBatchCoordinator.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::MonsterAiObserver {

inline constexpr size_t kActorSlotCount = 8u;
inline constexpr size_t kActorStride = 0xF90u;
inline constexpr size_t kMonsterIdOffset = 0x0Eu;
inline constexpr size_t kWholeFileOffset = 0x48u;
inline constexpr size_t kAiFileOffset = 0xF78u;
inline constexpr size_t kWorkerFileOffset = 0xF7Cu;
inline constexpr size_t kWorkerCountOffset = 0xDF6u;
inline constexpr size_t kHeaderAiOffset = 0x04u;
inline constexpr size_t kHeaderWorkerOffset = 0x08u;
inline constexpr size_t kHeaderStatOffset = 0x0Cu;
inline constexpr size_t kHeaderFileSizeOffset = 0x20u;
inline constexpr size_t kMinimumMonsterFileSize = 0x30u;
inline constexpr size_t kMaximumMonsterFileSize = 8u * 1024u * 1024u;
inline constexpr uint8_t kMaximumWorkerCount = 64u;

// No mutation candidate is currently compatible. In particular, the closest corpus pair
// (m342/m343) differs in command and motion contracts, so it is deliberately absent.
inline constexpr std::array<uint32_t, 0> kValidatedSwapPairs{};
inline constexpr size_t kValidatedSwapPairCount = kValidatedSwapPairs.size();

struct ReadOnlyMemory {
    void* context = nullptr;
    bool (*read)(void*, uintptr_t, uint8_t*, size_t) = nullptr;
};

struct SlotSnapshot {
    uint8_t slot = 0u;
    // Raw little-endian u16 formation word at actor +0x0E. It is never masked, renamed, or
    // treated as a species alias because dispatch correlation needs one unambiguous slot owner.
    uint16_t monsterId = 0xFFFFu;
    bool valid = false;
    uint8_t workerCount = 0u;
    uint32_t aiLength = 0u;
    uint64_t aiHash = 0u;
    uint32_t workerLength = 0u;
    uint64_t workerHash = 0u;
};

struct Snapshot {
    uint32_t generation = 0u;
    uint32_t threadId = 0u;
    bool listValid = false;
    std::array<SlotSnapshot, kActorSlotCount> slots{};
};

enum class CaptureResult : uint8_t {
    Captured = 0,
    InvalidArgument,
    InvalidActorList,
    ActorListUnreadable,
};

CaptureResult CaptureSnapshot(
    const ReadOnlyMemory&, uintptr_t actorList, uint32_t generation,
    uint32_t threadId, Snapshot* snapshotOut);
bool FormatSlotLine(const Snapshot&, size_t slot, char* out, size_t capacity);

enum class SnapshotPhase : uint8_t {
    BeforeRegistration = 0,
    AfterRegistration,
};

struct EventSink {
    void* context = nullptr;
    void (*snapshot)(void*, SnapshotPhase, const Snapshot&) = nullptr;
    void (*teardown)(void*, uint32_t generation, uint32_t threadId) = nullptr;
};

struct OriginalCall {
    void* context = nullptr;
    void (*invoke)(void*) = nullptr;
};

struct LifecycleState {
    std::atomic<uint32_t> admission{0u};
    std::atomic<uint32_t> activeCallbacks{0u};
    std::atomic<uint32_t> observationBusy{0u};
    std::atomic<uint32_t> generation{0u};
    std::atomic<uint32_t> activeGeneration{0u};
};

// Every detour shim constructs this object as its first C++ statement. Once construction starts,
// activeCallbacks fences every later target, trampoline, and shared-state access. A CPU can still
// be paused in the preceding machine prologue, which is why applied trampolines are never freed.
class CallbackLease {
public:
    explicit CallbackLease(LifecycleState*);
    ~CallbackLease();
    CallbackLease(const CallbackLease&) = delete;
    CallbackLease& operator=(const CallbackLease&) = delete;

    LifecycleState* State() const { return state_; }

private:
    LifecycleState* state_ = nullptr;
};

void InitializeLifecycle(LifecycleState*);
void OpenAdmission(LifecycleState*);
uint32_t NextGeneration(uint32_t current);
void RequestStop(LifecycleState*);
bool ObservationAdmitted(const LifecycleState&);
uint32_t ActiveCallbacks(const LifecycleState&);
void ObserveRegistration(
    CallbackLease&, const ReadOnlyMemory&, uintptr_t actorList, uint32_t threadId,
    const EventSink&, const OriginalCall&);
void ObserveCleanup(
    CallbackLease&, uint32_t threadId, const EventSink&, const OriginalCall&);

struct DetourIo {
    void* context = nullptr;
    bool (*create)(void*, uintptr_t target, uintptr_t detour, uintptr_t* originalOut) = nullptr;
    bool (*remove)(void*, uintptr_t target) = nullptr;
};

struct DetourOwner {
    uintptr_t registrationTarget = 0u;
    uintptr_t cleanupTarget = 0u;
    uintptr_t dispatchTarget = 0u;
    uintptr_t registrationOriginal = 0u;
    uintptr_t cleanupOriginal = 0u;
    uintptr_t dispatchOriginal = 0u;
    bool registrationCreated = false;
    bool cleanupCreated = false;
    bool dispatchCreated = false;
    // Once MH_ApplyQueued has been attempted, a CPU may already be executing a detour machine
    // prologue without having reached CallbackLease. The trampolines must then remain allocated
    // for the rest of the process, even after all owned entry points are disabled and callbacks drain.
    bool applyAttempted = false;
    bool retainedInert = false;
    bool coordinatorPoisoned = false;
    bool active = false;
};

struct DrainIo {
    void* context = nullptr;
    void (*pause)(void*, uint32_t milliseconds) = nullptr;
    uint32_t timeoutMs = 0u;
};

enum class TeardownResult : uint8_t {
    Removed = 0,
    RetainedInert,
    InvalidArgument,
    RemoveFailed,
    BatchBusy,
    CoordinatorPoisoned,
};

enum class InstallResult : uint8_t {
    Installed = 0,
    InvalidArgument,
    AlreadyOwned,
    CreateFailed,
    QueueFailed,
    ApplyFailed,
    RollbackFailed,
    BatchBusy,
    CoordinatorNotInitialized,
    CoordinatorPoisoned,
};

InstallResult InstallDetourSet(
    const DetourIo&, MinHookBatch::Coordinator*, const MinHookBatch::BatchIo&,
    const DrainIo&, LifecycleState*,
    uintptr_t registrationTarget, uintptr_t registrationDetour,
    uintptr_t cleanupTarget, uintptr_t cleanupDetour,
    uintptr_t dispatchTarget, uintptr_t dispatchDetour, DetourOwner* owner);
TeardownResult StopDrainAndRetainDetourSet(
    const DetourIo&, MinHookBatch::Coordinator*, const MinHookBatch::BatchIo&,
    const DrainIo&, LifecycleState*, DetourOwner* owner);

} // namespace FfxHooks::MonsterAiObserver
