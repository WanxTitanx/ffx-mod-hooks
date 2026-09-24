#include "MinHookBatchCoordinator.h"

#include <algorithm>

#ifdef FFXHOOKS_HAVE_POLYHOOK
#include <MinHook.h>
#endif

namespace FfxHooks::MinHookBatch {
namespace {

bool ValidIo(const BatchIo& io) {
    return io.queueEnable && io.queueDisable && io.applyQueued && io.disable;
}

bool ValidInitializationIo(const InitializationIo& io) {
    return io.initialize != nullptr;
}

bool ValidTargets(Owner owner, const uintptr_t* targets, size_t targetCount) {
    const bool knownOwner = owner == Owner::MonsterAiObserver ||
                            owner == Owner::FieldScout ||
                            owner == Owner::Difficulty ||
                            owner == Owner::SeymourBattle ||
                            owner == Owner::NovaSuperDamage ||
                            owner == Owner::ArenaPositions ||
                            owner == Owner::EquipmentWorkshop;
    if (!knownOwner || !targets || targetCount == 0u ||
        targetCount > kMaximumTargets) {
        return false;
    }
    for (size_t index = 0u; index < targetCount; ++index) {
        if (targets[index] == 0u) return false;
        for (size_t previous = 0u; previous < index; ++previous) {
            if (targets[previous] == targets[index]) return false;
        }
    }
    return true;
}

void RecordNeutralizationFailure(BatchReport* report, FailureStage stage) {
    if (report->neutralizationFailure == FailureStage::None) {
        report->neutralizationFailure = stage;
    }
}

bool NeutralizeLocked(const BatchIo& io, const uintptr_t* targets,
                      size_t targetCount, const NeutralizationFence& fence,
                      BatchReport* report) {
    if (fence.closeAndDrain && !fence.closeAndDrain(fence.context)) {
        RecordNeutralizationFailure(report, FailureStage::PreNeutralizeFence);
        return false;
    }
    bool complete = true;
    for (size_t index = 0u; index < targetCount; ++index) {
        if (io.queueDisable(io.context, targets[index])) {
            ++report->queuedDisableCount;
        } else {
            complete = false;
            RecordNeutralizationFailure(report, FailureStage::QueueDisable);
        }
    }

    // An ApplyQueued attempt is a one-way lifetime boundary: MinHook can have changed a subset
    // before reporting failure. Every target must therefore be treated as may-have-run.
    report->applyAttempted = true;
    report->mayHaveRun = true;
    if (!io.applyQueued(io.context)) {
        complete = false;
        RecordNeutralizationFailure(report, FailureStage::ApplyDisable);
    }

    bool allExactlyDisabled = true;
    for (size_t index = 0u; index < targetCount; ++index) {
        if (!io.disable(io.context, targets[index])) {
            allExactlyDisabled = false;
            complete = false;
            RecordNeutralizationFailure(report, FailureStage::ExactDisable);
        }
    }
    report->exactDisabled = allExactlyDisabled;
    if (fence.drainAfterDisable && !fence.drainAfterDisable(fence.context)) {
        complete = false;
        RecordNeutralizationFailure(report, FailureStage::PostNeutralizeFence);
    }
    report->neutralized = complete;
    return complete;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
bool RuntimeInitialize(void*) {
    const MH_STATUS status = MH_Initialize();
    return status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED;
}

bool RuntimeQueueEnable(void*, uintptr_t target) {
    return MH_QueueEnableHook(reinterpret_cast<void*>(target)) == MH_OK;
}

bool RuntimeQueueDisable(void*, uintptr_t target) {
    return MH_QueueDisableHook(reinterpret_cast<void*>(target)) == MH_OK;
}

bool RuntimeApplyQueued(void*) {
    return MH_ApplyQueued() == MH_OK;
}

bool RuntimeDisable(void*, uintptr_t target) {
    const MH_STATUS status = MH_DisableHook(reinterpret_cast<void*>(target));
    return status == MH_OK || status == MH_ERROR_DISABLED;
}
#endif

} // namespace

void Coordinator::BeginOwnedBatch(
    Owner owner, const uintptr_t* targets, size_t targetCount) {
    state_ = State::Batch;
    owner_ = owner;
    targetCount_ = targetCount;
    std::copy_n(targets, targetCount, targets_.begin());
}

void Coordinator::FinishOwnedBatch(bool poisoned) {
    if (poisoned) {
        // Retain owner and targets for diagnostics. Poison is process-lifetime because a failed
        // neutralization leaves MinHook's global queue state unknowable without a restart.
        state_ = State::Poisoned;
        return;
    }
    state_ = State::Idle;
    owner_ = Owner::None;
    targetCount_ = 0u;
    targets_.fill(0u);
}

InitializationResult EnsureInitialized(
    Coordinator* coordinator, const InitializationIo& io) {
    if (!coordinator || !ValidInitializationIo(io)) {
        return InitializationResult::InvalidArgument;
    }

    std::unique_lock<std::mutex> lock(coordinator->mutex_, std::try_to_lock);
    if (!lock.owns_lock() || coordinator->state_ == State::Batch) {
        return InitializationResult::Busy;
    }
    if (coordinator->state_ == State::Poisoned) {
        return InitializationResult::Poisoned;
    }
    if (coordinator->initialized_) {
        return InitializationResult::Ready;
    }

    // A failed process-global initialization leaves no feature with authority to guess whether
    // MinHook's heap is usable. Poisoning forces a restart instead of allowing partial ownership.
    if (!io.initialize(io.context)) {
        coordinator->FinishOwnedBatch(true);
        return InitializationResult::FailedPoisoned;
    }
    coordinator->initialized_ = true;
    return InitializationResult::Ready;
}

BatchReport EnableBatch(
    Coordinator* coordinator, const BatchIo& io, Owner owner,
    const uintptr_t* targets, size_t targetCount, NeutralizationFence fence) {
    BatchReport report{};
    if (!coordinator || !ValidIo(io) || !ValidTargets(owner, targets, targetCount)) {
        return report;
    }

    std::unique_lock<std::mutex> lock(coordinator->mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        report.result = BatchResult::Busy;
        return report;
    }
    if (coordinator->state_ == State::Poisoned) {
        report.result = BatchResult::Poisoned;
        return report;
    }
    if (!coordinator->initialized_) {
        report.result = BatchResult::NotInitialized;
        return report;
    }
    if (coordinator->state_ != State::Idle) {
        report.result = BatchResult::Busy;
        return report;
    }
    coordinator->BeginOwnedBatch(owner, targets, targetCount);

    for (size_t index = 0u; index < targetCount; ++index) {
        if (io.queueEnable(io.context, targets[index])) {
            ++report.queuedEnableCount;
            continue;
        }
        report.primaryFailure = FailureStage::QueueEnable;
        const bool neutralized = NeutralizeLocked(
            io, targets, targetCount, fence, &report);
        coordinator->FinishOwnedBatch(!neutralized);
        report.result = neutralized
            ? BatchResult::EnableFailedNeutralized
            : BatchResult::Poisoned;
        return report;
    }

    report.applyAttempted = true;
    report.mayHaveRun = true;
    if (io.applyQueued(io.context)) {
        coordinator->FinishOwnedBatch(false);
        report.result = BatchResult::Applied;
        return report;
    }

    report.primaryFailure = FailureStage::ApplyEnable;
    const bool neutralized = NeutralizeLocked(
        io, targets, targetCount, fence, &report);
    // A failed MH_ApplyQueued can have mutated an unknowable subset of MinHook's global queue.
    // Compensating disables make targets inert, but cannot prove queue ownership is reusable.
    // Poison is therefore absorbing until process restart even when neutralization succeeds.
    (void)neutralized;
    coordinator->FinishOwnedBatch(true);
    report.result = BatchResult::Poisoned;
    return report;
}

BatchReport NeutralizeBatch(
    Coordinator* coordinator, const BatchIo& io, Owner owner,
    const uintptr_t* targets, size_t targetCount, NeutralizationFence fence) {
    BatchReport report{};
    if (!coordinator || !ValidIo(io) || !ValidTargets(owner, targets, targetCount)) {
        return report;
    }

    std::unique_lock<std::mutex> lock(coordinator->mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        report.result = BatchResult::Busy;
        return report;
    }
    if (coordinator->state_ == State::Poisoned) {
        report.result = BatchResult::Poisoned;
        return report;
    }
    if (!coordinator->initialized_) {
        report.result = BatchResult::NotInitialized;
        return report;
    }
    if (coordinator->state_ != State::Idle) {
        report.result = BatchResult::Busy;
        return report;
    }
    coordinator->BeginOwnedBatch(owner, targets, targetCount);
    const bool neutralized = NeutralizeLocked(
        io, targets, targetCount, fence, &report);
    coordinator->FinishOwnedBatch(!neutralized);
    report.result = neutralized ? BatchResult::Neutralized : BatchResult::Poisoned;
    return report;
}

Snapshot GetSnapshot(Coordinator& coordinator) {
    std::lock_guard<std::mutex> lock(coordinator.mutex_);
    return {coordinator.state_, coordinator.owner_, coordinator.targetCount_,
            coordinator.initialized_};
}

Coordinator& ProcessCoordinator() {
    // Intentionally omit destruction. Dynamic unload is unsupported, and running a mutex
    // destructor during CRT detach would contradict the retained-trampoline lifetime boundary.
    // A poisoned queue also cannot be reset safely by one subsystem.
    static Coordinator* const coordinator = new Coordinator();
    return *coordinator;
}

#ifdef FFXHOOKS_HAVE_POLYHOOK
InitializationResult EnsureProcessInitialized() {
    const InitializationIo io{nullptr, &RuntimeInitialize};
    return EnsureInitialized(&ProcessCoordinator(), io);
}

BatchIo RuntimeBatchIo() {
    return {nullptr, &RuntimeQueueEnable, &RuntimeQueueDisable,
            &RuntimeApplyQueued, &RuntimeDisable};
}
#endif

} // namespace FfxHooks::MinHookBatch
