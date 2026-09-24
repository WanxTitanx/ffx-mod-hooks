#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace FfxHooks::MinHookBatch {

inline constexpr size_t kMaximumTargets = 16u;

enum class Owner : uint8_t {
    None = 0,
    MonsterAiObserver,
    FieldScout,
    // Difficulty and Seymour are distinct shared owners even before their separately reviewed
    // runtime adapters compose. Neither may borrow MonsterAiObserver ownership for its targets.
    Difficulty,
    SeymourBattle,
    NovaSuperDamage,
    ArenaPositions,
    EquipmentWorkshop,
};

enum class State : uint8_t {
    Idle = 0,
    Batch,
    Poisoned,
};

enum class InitializationResult : uint8_t {
    Ready = 0,
    InvalidArgument,
    Busy,
    FailedPoisoned,
    Poisoned,
};

struct InitializationIo {
    void* context = nullptr;
    bool (*initialize)(void*) = nullptr;
};

enum class BatchResult : uint8_t {
    Applied = 0,
    Neutralized,
    EnableFailedNeutralized,
    InvalidArgument,
    NotInitialized,
    Busy,
    Poisoned,
};

enum class FailureStage : uint8_t {
    None = 0,
    QueueEnable,
    ApplyEnable,
    QueueDisable,
    ApplyDisable,
    ExactDisable,
    PreNeutralizeFence,
    PostNeutralizeFence,
};

struct BatchIo {
    void* context = nullptr;
    bool (*queueEnable)(void*, uintptr_t) = nullptr;
    bool (*queueDisable)(void*, uintptr_t) = nullptr;
    bool (*applyQueued)(void*) = nullptr;
    bool (*disable)(void*, uintptr_t) = nullptr;
};

struct BatchReport {
    BatchResult result = BatchResult::InvalidArgument;
    FailureStage primaryFailure = FailureStage::None;
    FailureStage neutralizationFailure = FailureStage::None;
    size_t queuedEnableCount = 0u;
    size_t queuedDisableCount = 0u;
    bool applyAttempted = false;
    bool mayHaveRun = false;
    bool exactDisabled = false;
    bool neutralized = false;
};

struct NeutralizationFence {
    void* context = nullptr;
    bool (*closeAndDrain)(void*) = nullptr;
    bool (*drainAfterDisable)(void*) = nullptr;
};

struct Snapshot {
    State state = State::Idle;
    Owner owner = Owner::None;
    size_t targetCount = 0u;
    bool initialized = false;
};

// MinHook's queued operations are process-global rather than per subsystem. Holding this mutex
// across the callbacks intentionally prevents one feature from applying another feature's queue.
// try_lock is used by the public operations so accidental reentrancy fails Busy instead of
// deadlocking inside MinHook.
class Coordinator {
public:
    Coordinator() = default;
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

private:
    friend BatchReport EnableBatch(
        Coordinator*, const BatchIo&, Owner, const uintptr_t*, size_t,
        NeutralizationFence);
    friend BatchReport NeutralizeBatch(
        Coordinator*, const BatchIo&, Owner, const uintptr_t*, size_t,
        NeutralizationFence);
    friend Snapshot GetSnapshot(Coordinator&);
    friend InitializationResult EnsureInitialized(
        Coordinator*, const InitializationIo&);

    void BeginOwnedBatch(Owner, const uintptr_t*, size_t);
    void FinishOwnedBatch(bool poisoned);

    std::mutex mutex_;
    State state_ = State::Idle;
    Owner owner_ = Owner::None;
    std::array<uintptr_t, kMaximumTargets> targets_{};
    size_t targetCount_ = 0u;
    bool initialized_ = false;
};

InitializationResult EnsureInitialized(Coordinator*, const InitializationIo&);
BatchReport EnableBatch(
    Coordinator*, const BatchIo&, Owner, const uintptr_t*, size_t,
    NeutralizationFence fence = {});
BatchReport NeutralizeBatch(
    Coordinator*, const BatchIo&, Owner, const uintptr_t*, size_t,
    NeutralizationFence fence = {});
Snapshot GetSnapshot(Coordinator&);
Coordinator& ProcessCoordinator();

#ifdef FFXHOOKS_HAVE_POLYHOOK
// MinHook owns one process-global heap. Call this before any feature starts, regardless of which
// feature gates are enabled; feature-private initialization creates install-order dependencies.
InitializationResult EnsureProcessInitialized();
// This adapter is the only production surface allowed to call MinHook's process-global queue.
BatchIo RuntimeBatchIo();
#endif

} // namespace FfxHooks::MinHookBatch
