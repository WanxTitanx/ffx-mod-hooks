#include "MonsterAiObserverCore.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace FfxHooks::MonsterAiObserver {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

bool CheckedAdd(uintptr_t base, size_t offset, uintptr_t* resultOut) {
    if (!resultOut || offset > (std::numeric_limits<uintptr_t>::max)() - base) return false;
    *resultOut = base + offset;
    return true;
}

bool ReadBytes(const ReadOnlyMemory& memory, uintptr_t address, void* out, size_t length) {
    return memory.read && out && length > 0u &&
           memory.read(memory.context, address, static_cast<uint8_t*>(out), length);
}

bool ReadU8(const ReadOnlyMemory& memory, uintptr_t address, uint8_t* valueOut) {
    return ReadBytes(memory, address, valueOut, sizeof(*valueOut));
}

bool ReadU16(const ReadOnlyMemory& memory, uintptr_t address, uint16_t* valueOut) {
    uint8_t bytes[2] = {};
    if (!ReadBytes(memory, address, bytes, sizeof(bytes))) return false;
    *valueOut = static_cast<uint16_t>(bytes[0]) |
                static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8u);
    return true;
}

bool ReadU32(const ReadOnlyMemory& memory, uintptr_t address, uint32_t* valueOut) {
    uint8_t bytes[4] = {};
    if (!ReadBytes(memory, address, bytes, sizeof(bytes))) return false;
    *valueOut = static_cast<uint32_t>(bytes[0]) |
                (static_cast<uint32_t>(bytes[1]) << 8u) |
                (static_cast<uint32_t>(bytes[2]) << 16u) |
                (static_cast<uint32_t>(bytes[3]) << 24u);
    return true;
}

bool ReadAt(const ReadOnlyMemory& memory, uintptr_t base, size_t offset,
            void* out, size_t length) {
    uintptr_t address = 0u;
    return CheckedAdd(base, offset, &address) && ReadBytes(memory, address, out, length);
}

bool ReadU8At(const ReadOnlyMemory& memory, uintptr_t base, size_t offset,
              uint8_t* valueOut) {
    uintptr_t address = 0u;
    return CheckedAdd(base, offset, &address) && ReadU8(memory, address, valueOut);
}

bool ReadU16At(const ReadOnlyMemory& memory, uintptr_t base, size_t offset,
               uint16_t* valueOut) {
    uintptr_t address = 0u;
    return CheckedAdd(base, offset, &address) && ReadU16(memory, address, valueOut);
}

bool ReadU32At(const ReadOnlyMemory& memory, uintptr_t base, size_t offset,
               uint32_t* valueOut) {
    uintptr_t address = 0u;
    return CheckedAdd(base, offset, &address) && ReadU32(memory, address, valueOut);
}

bool HashRange(const ReadOnlyMemory& memory, uintptr_t address, size_t length,
               uint64_t* hashOut) {
    if (!hashOut || length == 0u || length > kMaximumMonsterFileSize) return false;
    uint64_t hash = kFnvOffset;
    std::array<uint8_t, 256> buffer{};
    size_t consumed = 0u;
    while (consumed < length) {
        const size_t chunk = (std::min)(buffer.size(), length - consumed);
        uintptr_t chunkAddress = 0u;
        if (!CheckedAdd(address, consumed, &chunkAddress) ||
            !ReadAt(memory, chunkAddress, 0u, buffer.data(), chunk)) {
            return false;
        }
        for (size_t index = 0u; index < chunk; ++index) {
            hash ^= buffer[index];
            hash *= kFnvPrime;
        }
        consumed += chunk;
    }
    *hashOut = hash;
    return true;
}

void ResetSlot(size_t slot, SlotSnapshot* snapshot) {
    *snapshot = SlotSnapshot{};
    snapshot->slot = static_cast<uint8_t>(slot);
}

bool PopulateSlot(const ReadOnlyMemory& memory, uintptr_t actor,
                  size_t slotIndex, SlotSnapshot* slot) {
    ResetSlot(slotIndex, slot);
    if (!ReadU16At(memory, actor, kMonsterIdOffset, &slot->monsterId)) return false;
    if (slot->monsterId == 0xFFFFu) return true;

    uint32_t whole32 = 0u;
    uint32_t ai32 = 0u;
    uint32_t worker32 = 0u;
    if (!ReadU32At(memory, actor, kWholeFileOffset, &whole32) ||
        !ReadU32At(memory, actor, kAiFileOffset, &ai32) ||
        !ReadU32At(memory, actor, kWorkerFileOffset, &worker32) ||
        !ReadU8At(memory, actor, kWorkerCountOffset, &slot->workerCount)) {
        return true;
    }
    if (slot->workerCount == 0u || slot->workerCount > kMaximumWorkerCount || whole32 == 0u) {
        return true;
    }

    const uintptr_t whole = static_cast<uintptr_t>(whole32);
    const uintptr_t ai = static_cast<uintptr_t>(ai32);
    const uintptr_t worker = static_cast<uintptr_t>(worker32);
    uint32_t headerAi = 0u;
    uint32_t headerWorker = 0u;
    uint32_t headerStat = 0u;
    uint32_t fileSize = 0u;
    if (!ReadU32At(memory, whole, kHeaderAiOffset, &headerAi) ||
        !ReadU32At(memory, whole, kHeaderWorkerOffset, &headerWorker) ||
        !ReadU32At(memory, whole, kHeaderStatOffset, &headerStat) ||
        !ReadU32At(memory, whole, kHeaderFileSizeOffset, &fileSize)) {
        return true;
    }
    if (fileSize < kMinimumMonsterFileSize || fileSize > kMaximumMonsterFileSize ||
        headerAi < kMinimumMonsterFileSize || headerAi >= headerWorker ||
        headerWorker >= headerStat || headerStat > fileSize) {
        return true;
    }

    uintptr_t expectedAi = 0u;
    uintptr_t expectedWorker = 0u;
    uintptr_t stat = 0u;
    uintptr_t end = 0u;
    if (!CheckedAdd(whole, headerAi, &expectedAi) ||
        !CheckedAdd(whole, headerWorker, &expectedWorker) ||
        !CheckedAdd(whole, headerStat, &stat) ||
        !CheckedAdd(whole, fileSize, &end) ||
        ai != expectedAi || worker != expectedWorker ||
        !(whole < ai && ai < worker && worker < stat && stat <= end)) {
        return true;
    }

    const size_t aiLength = static_cast<size_t>(worker - ai);
    const size_t workerLength = static_cast<size_t>(stat - worker);
    if (aiLength > (std::numeric_limits<uint32_t>::max)() ||
        workerLength > (std::numeric_limits<uint32_t>::max)()) {
        return true;
    }
    uint64_t aiHash = 0u;
    uint64_t workerHash = 0u;
    if (!HashRange(memory, ai, aiLength, &aiHash) ||
        !HashRange(memory, worker, workerLength, &workerHash)) {
        return true;
    }
    slot->aiLength = static_cast<uint32_t>(aiLength);
    slot->aiHash = aiHash;
    slot->workerLength = static_cast<uint32_t>(workerLength);
    slot->workerHash = workerHash;
    slot->valid = true;
    return true;
}

bool ValidDetourIo(const DetourIo& io) {
    return io.create && io.remove;
}

void ClearOwner(DetourOwner* owner) {
    *owner = DetourOwner{};
}

bool RemoveCreated(const DetourIo& io, DetourOwner* owner) {
    bool removed = true;
    if (owner->dispatchCreated && io.remove(io.context, owner->dispatchTarget)) {
        owner->dispatchCreated = false;
    } else if (owner->dispatchCreated) {
        removed = false;
    }
    if (owner->cleanupCreated && io.remove(io.context, owner->cleanupTarget)) {
        owner->cleanupCreated = false;
    } else if (owner->cleanupCreated) {
        removed = false;
    }
    if (owner->registrationCreated && io.remove(io.context, owner->registrationTarget)) {
        owner->registrationCreated = false;
    } else if (owner->registrationCreated) {
        removed = false;
    }
    if (!removed) {
        return false;
    }
    ClearOwner(owner);
    return true;
}

bool RollbackNeverApplied(const DetourIo& io, DetourOwner* owner) {
    // This helper is restricted to create-only or coordinator-untouched states. Once a queued
    // transaction starts, even queue-enable recovery crosses ApplyQueued and retains trampolines.
    // Failed removals keep exact ownership for a later normal-context retry.
    if (owner->applyAttempted) return false;
    owner->active = false;
    return RemoveCreated(io, owner);
}

bool DrainCallbacks(const DrainIo& drain, LifecycleState* lifecycle) {
    if (!lifecycle) return false;
    uint32_t waited = 0u;
    while (ActiveCallbacks(*lifecycle) != 0u) {
        if (!drain.pause || waited >= drain.timeoutMs) return false;
        drain.pause(drain.context, 1u);
        ++waited;
    }
    return true;
}

struct NeutralizationFenceContext {
    const DrainIo* drain = nullptr;
    LifecycleState* lifecycle = nullptr;
};

bool CloseAdmissionAndDrain(void* context) {
    auto* fence = static_cast<NeutralizationFenceContext*>(context);
    if (!fence || !fence->drain || !fence->lifecycle) return false;
    RequestStop(fence->lifecycle);
    return DrainCallbacks(*fence->drain, fence->lifecycle);
}

bool DrainAfterDisable(void* context) {
    auto* fence = static_cast<NeutralizationFenceContext*>(context);
    return fence && fence->drain && fence->lifecycle &&
           DrainCallbacks(*fence->drain, fence->lifecycle);
}

MinHookBatch::NeutralizationFence MakeNeutralizationFence(
    NeutralizationFenceContext* context) {
    return {context, &CloseAdmissionAndDrain, &DrainAfterDisable};
}

uint32_t ReserveGeneration(LifecycleState* state) {
    uint32_t observed = state->generation.load(std::memory_order_acquire);
    for (;;) {
        const uint32_t desired = NextGeneration(observed);
        if (state->generation.compare_exchange_weak(
                observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return desired;
        }
    }
}

void InvokeOriginal(const OriginalCall& original) {
    if (original.invoke) original.invoke(original.context);
}

} // namespace

CaptureResult CaptureSnapshot(
    const ReadOnlyMemory& memory, uintptr_t actorList, uint32_t generation,
    uint32_t threadId, Snapshot* snapshotOut) {
    if (!snapshotOut || !memory.read) return CaptureResult::InvalidArgument;
    *snapshotOut = Snapshot{};
    snapshotOut->generation = generation;
    snapshotOut->threadId = threadId;
    for (size_t slot = 0u; slot < snapshotOut->slots.size(); ++slot) {
        ResetSlot(slot, &snapshotOut->slots[slot]);
    }
    if (actorList == 0u) return CaptureResult::InvalidActorList;

    for (size_t slot = 0u; slot < snapshotOut->slots.size(); ++slot) {
        if (slot > (std::numeric_limits<size_t>::max)() / kActorStride) {
            return CaptureResult::ActorListUnreadable;
        }
        uintptr_t actor = 0u;
        if (!CheckedAdd(actorList, slot * kActorStride, &actor) ||
            !PopulateSlot(memory, actor, slot, &snapshotOut->slots[slot])) {
            *snapshotOut = Snapshot{};
            snapshotOut->generation = generation;
            snapshotOut->threadId = threadId;
            for (size_t reset = 0u; reset < snapshotOut->slots.size(); ++reset) {
                ResetSlot(reset, &snapshotOut->slots[reset]);
            }
            return CaptureResult::ActorListUnreadable;
        }
    }
    snapshotOut->listValid = true;

    // Duplicate non-empty raw IDs make slot-to-script identity ambiguous. Invalidate every
    // matching slot and clear its hashes so downstream dispatch correlation fails closed rather
    // than choosing the first occurrence or inventing a masked/name-based identity.
    for (size_t left = 0u; left < snapshotOut->slots.size(); ++left) {
        if (snapshotOut->slots[left].monsterId == 0xFFFFu) continue;
        for (size_t right = left + 1u; right < snapshotOut->slots.size(); ++right) {
            if (snapshotOut->slots[left].monsterId != snapshotOut->slots[right].monsterId) continue;
            snapshotOut->slots[left].valid = false;
            snapshotOut->slots[left].aiHash = 0u;
            snapshotOut->slots[left].workerHash = 0u;
            snapshotOut->slots[right].valid = false;
            snapshotOut->slots[right].aiHash = 0u;
            snapshotOut->slots[right].workerHash = 0u;
        }
    }
    return CaptureResult::Captured;
}

bool FormatSlotLine(const Snapshot& snapshot, size_t slot, char* out, size_t capacity) {
    if (!out || capacity == 0u || slot >= snapshot.slots.size()) return false;
    const SlotSnapshot& item = snapshot.slots[slot];
    const int written = std::snprintf(
        out, capacity,
        "generation=%u thread=%u slot=%u monster_id=0x%04X valid=%u workers=%u "
        "ai_length=%u ai_hash=%016llX worker_length=%u worker_hash=%016llX",
        snapshot.generation, snapshot.threadId, static_cast<unsigned>(item.slot),
        static_cast<unsigned>(item.monsterId), item.valid ? 1u : 0u,
        static_cast<unsigned>(item.workerCount), item.aiLength,
        static_cast<unsigned long long>(item.aiHash),
        item.workerLength,
        static_cast<unsigned long long>(item.workerHash));
    return written >= 0 && static_cast<size_t>(written) < capacity;
}

void InitializeLifecycle(LifecycleState* state) {
    if (!state) return;
    // Admission remains closed until all three observer hooks have been enabled as one owned MinHook batch.
    // This prevents a partially applied installation from entering the read-only observer.
    state->admission.store(0u, std::memory_order_release);
    state->activeCallbacks.store(0u, std::memory_order_release);
    state->observationBusy.store(0u, std::memory_order_release);
    state->generation.store(0u, std::memory_order_release);
    state->activeGeneration.store(0u, std::memory_order_release);
}

void OpenAdmission(LifecycleState* state) {
    if (state) state->admission.store(1u, std::memory_order_release);
}

CallbackLease::CallbackLease(LifecycleState* state) : state_(state) {
    if (state_) state_->activeCallbacks.fetch_add(1u, std::memory_order_acq_rel);
}

CallbackLease::~CallbackLease() {
    if (state_) state_->activeCallbacks.fetch_sub(1u, std::memory_order_acq_rel);
}

uint32_t NextGeneration(uint32_t current) {
    return current == (std::numeric_limits<uint32_t>::max)() ? 1u : current + 1u;
}

void RequestStop(LifecycleState* state) {
    if (state) state->admission.store(0u, std::memory_order_release);
}

bool ObservationAdmitted(const LifecycleState& state) {
    return state.admission.load(std::memory_order_acquire) != 0u;
}

uint32_t ActiveCallbacks(const LifecycleState& state) {
    return state.activeCallbacks.load(std::memory_order_acquire);
}

void ObserveRegistration(
    CallbackLease& callback, const ReadOnlyMemory& memory, uintptr_t actorList,
    uint32_t threadId, const EventSink& sink, const OriginalCall& original) {
    LifecycleState* state = callback.State();
    if (!state) {
        InvokeOriginal(original);
        return;
    }
    bool ownsObservation = false;
    uint32_t expected = 0u;
    if (ObservationAdmitted(*state)) {
        ownsObservation = state->observationBusy.compare_exchange_strong(
            expected, 1u, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    if (!ownsObservation) {
        InvokeOriginal(original);
        return;
    }

    const uint32_t generation = ReserveGeneration(state);
    state->activeGeneration.store(generation, std::memory_order_release);
    Snapshot before{};
    (void)CaptureSnapshot(memory, actorList, generation, threadId, &before);
    if (sink.snapshot) sink.snapshot(sink.context, SnapshotPhase::BeforeRegistration, before);

    InvokeOriginal(original);

    Snapshot after{};
    (void)CaptureSnapshot(memory, actorList, generation, threadId, &after);
    if (sink.snapshot) sink.snapshot(sink.context, SnapshotPhase::AfterRegistration, after);
    state->observationBusy.store(0u, std::memory_order_release);
}

void ObserveCleanup(
    CallbackLease& callback, uint32_t threadId, const EventSink& sink,
    const OriginalCall& original) {
    LifecycleState* state = callback.State();
    if (!state) {
        InvokeOriginal(original);
        return;
    }
    bool ownsObservation = false;
    uint32_t expected = 0u;
    if (ObservationAdmitted(*state)) {
        ownsObservation = state->observationBusy.compare_exchange_strong(
            expected, 1u, std::memory_order_acq_rel, std::memory_order_acquire);
    }
    if (ownsObservation) {
        const uint32_t generation = state->activeGeneration.exchange(0u, std::memory_order_acq_rel);
        if (generation != 0u && sink.teardown) sink.teardown(sink.context, generation, threadId);
    }
    InvokeOriginal(original);
    if (ownsObservation) state->observationBusy.store(0u, std::memory_order_release);
}

InstallResult InstallDetourSet(
    const DetourIo& io, MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo, const DrainIo& drain,
    LifecycleState* lifecycle,
    uintptr_t registrationTarget, uintptr_t registrationDetour,
    uintptr_t cleanupTarget, uintptr_t cleanupDetour,
    uintptr_t dispatchTarget, uintptr_t dispatchDetour, DetourOwner* owner) {
    if (!owner || !lifecycle || !coordinator || !ValidDetourIo(io) ||
        registrationTarget == 0u ||
        registrationDetour == 0u || cleanupTarget == 0u || cleanupDetour == 0u ||
        dispatchTarget == 0u || dispatchDetour == 0u) {
        return InstallResult::InvalidArgument;
    }
    if (owner->registrationCreated || owner->cleanupCreated || owner->dispatchCreated ||
        owner->applyAttempted || owner->retainedInert || owner->coordinatorPoisoned ||
        owner->active) {
        return InstallResult::AlreadyOwned;
    }
    RequestStop(lifecycle);
    ClearOwner(owner);
    owner->registrationTarget = registrationTarget;
    owner->cleanupTarget = cleanupTarget;
    owner->dispatchTarget = dispatchTarget;
    uintptr_t registrationOriginal = 0u;
    uintptr_t cleanupOriginal = 0u;
    uintptr_t dispatchOriginal = 0u;
    if (!io.create(io.context, registrationTarget, registrationDetour, &registrationOriginal)) {
        ClearOwner(owner);
        return InstallResult::CreateFailed;
    }
    owner->registrationCreated = true;
    // The trampoline must be visible before the queued hooks can become executable. Ownership is
    // retained from this point until an exact rollback or normal-context teardown succeeds.
    owner->registrationOriginal = registrationOriginal;
    if (!io.create(io.context, cleanupTarget, cleanupDetour, &cleanupOriginal)) {
        return RollbackNeverApplied(io, owner)
            ? InstallResult::CreateFailed
            : InstallResult::RollbackFailed;
    }
    owner->cleanupCreated = true;
    owner->cleanupOriginal = cleanupOriginal;
    if (!io.create(io.context, dispatchTarget, dispatchDetour, &dispatchOriginal)) {
        return RollbackNeverApplied(io, owner)
            ? InstallResult::CreateFailed
            : InstallResult::RollbackFailed;
    }
    owner->dispatchCreated = true;
    owner->dispatchOriginal = dispatchOriginal;
    const std::array<uintptr_t, 3u> targets = {
        registrationTarget,
        cleanupTarget,
        dispatchTarget,
    };
    NeutralizationFenceContext fenceContext{&drain, lifecycle};
    const MinHookBatch::BatchReport report = MinHookBatch::EnableBatch(
        coordinator, batchIo, MinHookBatch::Owner::MonsterAiObserver,
        targets.data(), targets.size(), MakeNeutralizationFence(&fenceContext));
    owner->applyAttempted = report.applyAttempted;

    if (report.result == MinHookBatch::BatchResult::Applied) {
        owner->active = true;
        OpenAdmission(lifecycle);
        return InstallResult::Installed;
    }
    if (report.result == MinHookBatch::BatchResult::EnableFailedNeutralized) {
        // Queue rollback itself uses ApplyQueued, so an uncounted prologue entrant is possible
        // even when the primary failure happened while queueing. Retain all three trampolines.
        owner->active = false;
        owner->retainedInert = true;
        return report.primaryFailure == MinHookBatch::FailureStage::QueueEnable
            ? InstallResult::QueueFailed
            : InstallResult::ApplyFailed;
    }
    if (report.result == MinHookBatch::BatchResult::Busy ||
        report.result == MinHookBatch::BatchResult::InvalidArgument ||
        report.result == MinHookBatch::BatchResult::NotInitialized) {
        const bool removed = RollbackNeverApplied(io, owner);
        if (!removed) return InstallResult::RollbackFailed;
        if (report.result == MinHookBatch::BatchResult::Busy) {
            return InstallResult::BatchBusy;
        }
        return report.result == MinHookBatch::BatchResult::NotInitialized
            ? InstallResult::CoordinatorNotInitialized
            : InstallResult::InvalidArgument;
    }

    if (!report.applyAttempted && report.queuedEnableCount == 0u) {
        // The coordinator was already poisoned before this owner touched its queue. These newly
        // created hooks were never published and remain safe to remove exactly.
        return RollbackNeverApplied(io, owner)
            ? InstallResult::CoordinatorPoisoned
            : InstallResult::RollbackFailed;
    }
    owner->coordinatorPoisoned = true;
    owner->active = !report.exactDisabled;
    owner->retainedInert = report.exactDisabled;
    return InstallResult::CoordinatorPoisoned;
}

TeardownResult StopDrainAndRetainDetourSet(
    const DetourIo& io, MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo, const DrainIo& drain,
    LifecycleState* lifecycle, DetourOwner* owner) {
    if (!owner || !lifecycle || !coordinator || !ValidDetourIo(io)) {
        return TeardownResult::InvalidArgument;
    }

    // The pre-disable drain is deliberately conservative for a partially successful
    // MH_ApplyQueued call. A second drain after disabling closes the small race between observing
    // zero callbacks and making all owned entry points unreachable.
    RequestStop(lifecycle);
    if (!owner->registrationCreated && !owner->cleanupCreated && !owner->dispatchCreated) {
        ClearOwner(owner);
        return TeardownResult::Removed;
    }
    if (!owner->applyAttempted && !owner->coordinatorPoisoned) {
        // A create-only failure never made any owned detour reachable or entered the global queue.
        // This is the only state in which freeing a trampoline is provably safe.
        return RemoveCreated(io, owner)
            ? TeardownResult::Removed
            : TeardownResult::RemoveFailed;
    }
    if (owner->coordinatorPoisoned) return TeardownResult::CoordinatorPoisoned;

    std::array<uintptr_t, 3u> targets{};
    size_t targetCount = 0u;
    if (owner->registrationCreated) targets[targetCount++] = owner->registrationTarget;
    if (owner->cleanupCreated) targets[targetCount++] = owner->cleanupTarget;
    if (owner->dispatchCreated) targets[targetCount++] = owner->dispatchTarget;
    NeutralizationFenceContext fenceContext{&drain, lifecycle};
    const MinHookBatch::BatchReport report = MinHookBatch::NeutralizeBatch(
        coordinator, batchIo, MinHookBatch::Owner::MonsterAiObserver,
        targets.data(), targetCount, MakeNeutralizationFence(&fenceContext));
    owner->applyAttempted = owner->applyAttempted || report.applyAttempted;
    if (report.result == MinHookBatch::BatchResult::Busy) {
        return TeardownResult::BatchBusy;
    }
    if (report.result != MinHookBatch::BatchResult::Neutralized) {
        owner->coordinatorPoisoned =
            report.result == MinHookBatch::BatchResult::Poisoned;
        owner->active = !report.exactDisabled;
        owner->retainedInert = report.exactDisabled;
        return report.result == MinHookBatch::BatchResult::Poisoned
            ? TeardownResult::CoordinatorPoisoned
            : TeardownResult::InvalidArgument;
    }
    owner->active = false;
    // Do not call MH_RemoveHook or clear any trampoline after MH_ApplyQueued was attempted.
    // A CPU redirected before disable can still be paused in machine prologue, before the first
    // C++ CallbackLease increment. Retaining disabled trampolines until process exit is the only
    // conservative lifetime fence available without suspending and inspecting every thread.
    owner->retainedInert = true;
    return TeardownResult::RetainedInert;
}

} // namespace FfxHooks::MonsterAiObserver
