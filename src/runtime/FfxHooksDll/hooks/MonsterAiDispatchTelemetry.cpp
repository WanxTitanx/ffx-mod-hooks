#include "MonsterAiDispatchTelemetry.h"

#include <cstddef>
#include <limits>

namespace FfxHooks::MonsterAiDispatchTelemetry {
namespace {

constexpr uint32_t kIdentityReadAttempts = 4u;

uint32_t NextNonzero(uint32_t current) noexcept {
    return current == (std::numeric_limits<uint32_t>::max)() ? 1u : current + 1u;
}

uint32_t ReserveSerial(DispatchTelemetryState* state) noexcept {
    uint32_t observed = state->serial.load(std::memory_order_acquire);
    for (;;) {
        const uint32_t desired = NextNonzero(observed);
        if (state->serial.compare_exchange_weak(
                observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return desired;
        }
    }
}

void StoreSlot(PublishedSlot* destination, uint32_t generation,
               const MonsterAiObserver::SlotSnapshot* source) noexcept {
    destination->sequence.fetch_add(1u, std::memory_order_acq_rel);
    const bool valid = source && source->valid;
    destination->generation.store(valid ? generation : 0u, std::memory_order_relaxed);
    destination->rawId.store(valid ? source->monsterId : 0u, std::memory_order_relaxed);
    destination->aiLength.store(valid ? source->aiLength : 0u, std::memory_order_relaxed);
    destination->aiHashLow.store(
        valid ? static_cast<uint32_t>(source->aiHash) : 0u, std::memory_order_relaxed);
    destination->aiHashHigh.store(
        valid ? static_cast<uint32_t>(source->aiHash >> 32u) : 0u,
        std::memory_order_relaxed);
    destination->workerLength.store(
        valid ? source->workerLength : 0u, std::memory_order_relaxed);
    destination->workerHashLow.store(
        valid ? static_cast<uint32_t>(source->workerHash) : 0u,
        std::memory_order_relaxed);
    destination->workerHashHigh.store(
        valid ? static_cast<uint32_t>(source->workerHash >> 32u) : 0u,
        std::memory_order_relaxed);
    destination->sequence.fetch_add(1u, std::memory_order_release);
}

bool LoadIdentity(const DispatchTelemetryState& state, size_t slot, uint32_t generation,
                  MonsterAiShadow::MonsterProgramIdentity* identityOut) noexcept {
    if (!identityOut || slot >= state.slots.size() || generation == 0u) return false;
    *identityOut = MonsterAiShadow::MonsterProgramIdentity{};
    const PublishedSlot& source = state.slots[slot];
    for (uint32_t attempt = 0u; attempt < kIdentityReadAttempts; ++attempt) {
        const uint32_t before = source.sequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const uint32_t observedGeneration = source.generation.load(std::memory_order_relaxed);
        MonsterAiShadow::MonsterProgramIdentity candidate{};
        candidate.rawId = static_cast<uint16_t>(
            source.rawId.load(std::memory_order_relaxed) & 0xFFFFu);
        candidate.aiLength = source.aiLength.load(std::memory_order_relaxed);
        const uint64_t aiLow = source.aiHashLow.load(std::memory_order_relaxed);
        const uint64_t aiHigh = source.aiHashHigh.load(std::memory_order_relaxed);
        candidate.aiFnv1a64 = aiLow | (aiHigh << 32u);
        candidate.workerLength = source.workerLength.load(std::memory_order_relaxed);
        const uint64_t workerLow = source.workerHashLow.load(std::memory_order_relaxed);
        const uint64_t workerHigh = source.workerHashHigh.load(std::memory_order_relaxed);
        candidate.workerFnv1a64 = workerLow | (workerHigh << 32u);
        const uint32_t after = source.sequence.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u && observedGeneration == generation) {
            *identityOut = candidate;
            return true;
        }
    }
    return false;
}

int32_t InvokeOriginal(const DispatchOriginalCall& original, int32_t actorIndex,
                       int32_t commandStack32, uint32_t targetMask, int32_t force,
                       int32_t n64) noexcept {
    return original.invoke
        ? original.invoke(original.context, actorIndex, commandStack32, targetMask, force, n64)
        : 0;
}

} // namespace

void Initialize(DispatchTelemetryState* state) noexcept {
    if (!state) return;
    state->serial.store(0u, std::memory_order_release);
    for (PublishedSlot& slot : state->slots) StoreSlot(&slot, 0u, nullptr);
}

void PublishSnapshot(DispatchTelemetryState* state,
                     const MonsterAiObserver::Snapshot& snapshot) noexcept {
    if (!state) return;
    for (size_t slot = 0u; slot < state->slots.size(); ++slot) {
        const MonsterAiObserver::SlotSnapshot* source =
            snapshot.listValid ? &snapshot.slots[slot] : nullptr;
        StoreSlot(&state->slots[slot], snapshot.generation, source);
    }
}

void RetireGeneration(DispatchTelemetryState* state, uint32_t generation) noexcept {
    if (!state || generation == 0u) return;
    for (PublishedSlot& slot : state->slots) {
        // Registration and cleanup are serialized by observationBusy. This equality check avoids
        // retiring a newer battle identity if an old teardown event is observed out of order.
        if (slot.generation.load(std::memory_order_acquire) == generation) {
            StoreSlot(&slot, 0u, nullptr);
        }
    }
}

int32_t ObserveDispatch(
    MonsterAiObserver::CallbackLease& callback, DispatchTelemetryState* telemetry,
    uint32_t threadId, uint32_t callerReturnPreferredVa, int32_t actorIndex,
    int32_t commandStack32, uint32_t targetMask, int32_t force, int32_t n64,
    const DispatchEventSink& sink, const DispatchOriginalCall& original) noexcept {
    MonsterAiObserver::LifecycleState* lifecycle = callback.State();
    const bool admitted = lifecycle && telemetry &&
        MonsterAiObserver::ObservationAdmitted(*lifecycle);

    MonsterAiShadow::DispatchShadowDecision decision{};
    uint32_t generation = 0u;
    uint32_t serial = 0u;
    if (admitted) {
        generation = lifecycle->activeGeneration.load(std::memory_order_acquire);
        serial = ReserveSerial(telemetry);
        MonsterAiShadow::DispatchSample sample{};
        sample.mode = MonsterAiShadow::ShadowMode::ObserveOnly;
        sample.callerReturnPreferredVa = callerReturnPreferredVa;
        sample.actorIndex = actorIndex;
        sample.commandStack32 = commandStack32;
        sample.resolvedTargetMask = targetMask;
        sample.force = force;
        sample.n64 = n64;
        if (actorIndex >= MonsterAiShadow::kFirstMonsterActorIndex &&
            actorIndex <= MonsterAiShadow::kLastMonsterActorIndex) {
            const size_t slot = static_cast<size_t>(
                actorIndex - MonsterAiShadow::kFirstMonsterActorIndex);
            (void)LoadIdentity(*telemetry, slot, generation, &sample.monster);
        }
        decision = MonsterAiShadow::EvaluateDispatch(sample);
    }

    // The observer never has an effective-command channel. Vanilla is invoked exactly once with
    // the five incoming DWORD arguments, and its return is forwarded without reinterpretation.
    const int32_t queueReadback = InvokeOriginal(
        original, actorIndex, commandStack32, targetMask, force, n64);

    if (admitted && sink.emit) {
        DispatchEvent event{};
        event.generation = generation;
        event.threadId = threadId;
        event.serial = serial;
        event.actorSlot = decision.monsterSlot;
        event.monsterRawId = decision.observation.monster.rawId;
        event.command = decision.observedCommand;
        event.targetMask = targetMask;
        event.force = force;
        event.queueReadback = queueReadback;
        event.reason = decision.reason;
        event.proposalAvailable = decision.proposalAvailable;
        event.proposedCommand = decision.proposedCommand;
        sink.emit(sink.context, event);
    }
    return queueReadback;
}

} // namespace FfxHooks::MonsterAiDispatchTelemetry
