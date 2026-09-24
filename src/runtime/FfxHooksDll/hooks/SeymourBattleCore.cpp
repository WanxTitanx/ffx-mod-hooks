#include "SeymourBattleCore.h"

#include "SharedBattleRuntime.h"

#include <algorithm>

namespace FfxHooks::SeymourBattle {
namespace {

using MembershipCounts = std::array<uint8_t, 256>;

MembershipCounts CountMembership(const RosterImage& image) {
    MembershipCounts counts{};
    for (const uint8_t value : image.state) ++counts[value];
    for (const uint8_t value : image.ability) ++counts[value];
    return counts;
}

bool SameRoster(const RosterImage& lhs, const RosterImage& rhs) {
    return lhs.party == rhs.party && lhs.state == rhs.state && lhs.ability == rhs.ability;
}

bool ValidServiceIo(const ServiceIo& io) {
    return io.context && io.stateLength == kStateListSize &&
           io.abilityLength == kAbilityListSize && io.read && io.assign && io.commandCurrent;
}

void SetTelemetry(Telemetry* telemetry, const Command& command, const Ownership& ownership,
                  ServiceOutcome outcome) {
    if (!telemetry) return;
    telemetry->generation = command.generation;
    telemetry->state = ownership.state;
    telemetry->outcome = outcome;
}

bool ReadImage(const ServiceIo& io, RosterImage* image, uint32_t* hashOut) {
    if (!image || !io.read(io.context, image)) return false;
    if (hashOut) *hashOut = HashRoster(*image);
    return true;
}

bool CleanupPersistent(const ServiceIo& io, Ownership* ownership, Telemetry* telemetry) {
    if (!ownership || !ownership->hasBaseline) return false;
    RosterImage current{};
    if (!ReadImage(io, &current, telemetry ? &telemetry->applyHash : nullptr)) return false;
    if (IsConservativeRestoredImage(ownership->baseline, current)) {
        ownership->persistentMayContainSeymour = false;
        if (telemetry) telemetry->restoreHash = HashRoster(current);
        return true;
    }
    if (!IsConservativeExitOwnedImage(ownership->baseline, current)) return false;

    ownership->persistentMayContainSeymour = true;
    (void)io.assign(io.context, kSeymourSlot, 0);
    RosterImage restored{};
    if (!ReadImage(io, &restored, telemetry ? &telemetry->restoreHash : nullptr) ||
        !IsConservativeRestoredImage(ownership->baseline, restored)) {
        return false;
    }
    ownership->persistentMayContainSeymour = false;
    return true;
}

void ClearBattleOwnership(Ownership* ownership) {
    ownership->activeGeneration = 0u;
    ownership->baseline = RosterImage{};
    ownership->hasBaseline = false;
    ownership->battleRosterOwned = false;
    ownership->persistentMayContainSeymour = false;
}

bool ValidExitDetourIo(const ExitDetourIo& io) {
    return io.create && io.publishOriginal;
}

void ClearExitOwner(ExitDetourOwner* owner) {
    if (owner) *owner = ExitDetourOwner{};
}

void RetainCreatedExitInert(ExitDetourOwner* owner) {
    if (!owner) return;
    owner->active = false;
    owner->retainedInert = owner->created;
}

} // namespace

bool IsAdmittedEntryReturnRva(uintptr_t returnRva) noexcept {
    return returnRva == SharedBattleRuntime::kBattleStateInitSceneReturnRva;
}

bool IsAdmittedExitReturnRva(uintptr_t returnRva) noexcept {
    return returnRva == kBattleExitSyncReturnRva;
}

PreflightResult ValidatePreflight(const RosterImage& image, uint8_t battleDiscriminator) {
    if (battleDiscriminator != kAcceptedBattleDiscriminator) {
        return PreflightResult::WrongDiscriminator;
    }
    // WHY: exact 0x10 is the only evidenced clean party byte. Broader bit acceptance would make
    // official Assign(7, 0) unable to prove restoration of the controlled formation state.
    if (image.party != 0x10u || (image.party & 0x02u) != 0u) {
        return PreflightResult::WrongPartyBaseline;
    }
    if (std::find(image.state.begin(), image.state.end(), kSeymourSlot) != image.state.end() ||
        std::find(image.ability.begin(), image.ability.end(), kSeymourSlot) != image.ability.end()) {
        return PreflightResult::SeymourAlreadyPresent;
    }
    if (std::find(image.ability.begin(), image.ability.end(), 0xFFu) == image.ability.end()) {
        return PreflightResult::NoFreeAbilitySlot;
    }
    return PreflightResult::Ready;
}

bool IsExactInitialApply(const RosterImage& baseline, const RosterImage& candidate) {
    if (baseline.party != 0x10u || candidate.party != 0x11u ||
        baseline.state != candidate.state) {
        return false;
    }
    size_t insertionCount = 0;
    for (size_t index = 0; index < baseline.ability.size(); ++index) {
        if (baseline.ability[index] == candidate.ability[index]) continue;
        if (baseline.ability[index] != 0xFFu || candidate.ability[index] != kSeymourSlot) {
            return false;
        }
        ++insertionCount;
    }
    return insertionCount == 1u;
}

bool IsConservativeOwnedImage(const RosterImage& baseline, const RosterImage& candidate) {
    if (baseline.party != 0x10u || candidate.party != 0x11u) return false;
    // The combined 3+17 multiset is exact because a legitimate Switch may move members between
    // the two lists. Exactly one raw 0xFF becomes raw slot 0x07; duplicates or foreign drift fail.
    MembershipCounts expected = CountMembership(baseline);
    if (expected[kSeymourSlot] != 0u || expected[0xFFu] == 0u) return false;
    --expected[0xFFu];
    ++expected[kSeymourSlot];
    const MembershipCounts actual = CountMembership(candidate);
    return actual[kSeymourSlot] == 1u && actual == expected;
}

bool IsConservativeExitOwnedImage(const RosterImage& baseline, const RosterImage& candidate) {
    if (baseline.party != 0x10u || (candidate.party != 0x10u && candidate.party != 0x11u)) {
        return false;
    }
    MembershipCounts expected = CountMembership(baseline);
    if (expected[kSeymourSlot] != 0u || expected[0xFFu] == 0u) return false;
    --expected[0xFFu];
    ++expected[kSeymourSlot];
    const MembershipCounts actual = CountMembership(candidate);
    return actual[kSeymourSlot] == 1u && actual == expected;
}

bool IsConservativeRestoredImage(const RosterImage& baseline, const RosterImage& candidate) {
    if (baseline.party != 0x10u || candidate.party != 0x10u) return false;
    const MembershipCounts expected = CountMembership(baseline);
    const MembershipCounts actual = CountMembership(candidate);
    return expected[kSeymourSlot] == 0u && actual[kSeymourSlot] == 0u && actual == expected;
}

uint32_t HashRoster(const RosterImage& image) {
    uint32_t hash = 2166136261u;
    const auto mix = [&hash](uint8_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    mix(image.party);
    for (const uint8_t value : image.state) mix(value);
    for (const uint8_t value : image.ability) mix(value);
    return hash;
}

Command ReadCommand(const AtomicCommandMailbox* mailbox) {
    Command command{};
    if (!mailbox) return command;
    const uint32_t word = mailbox->word.load(std::memory_order_acquire);
    command.generation = word >> 2u;
    command.stopping = (word & 0x02u) != 0u;
    command.requested = !command.stopping && (word & 0x01u) != 0u;
    return command;
}

bool PublishRequested(AtomicCommandMailbox* mailbox, bool requested) {
    if (!mailbox) return false;
    uint32_t observed = mailbox->word.load(std::memory_order_acquire);
    for (;;) {
        if ((observed & 0x02u) != 0u) return false;
        if (((observed & 0x01u) != 0u) == requested) return true;
        uint32_t generation = ((observed >> 2u) + 1u) & 0x3FFFFFFFu;
        if (generation == 0u) generation = 1u;
        const uint32_t desired = (generation << 2u) | (requested ? 0x01u : 0u);
        if (mailbox->word.compare_exchange_weak(
                observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }
}

void RequestStop(AtomicCommandMailbox* mailbox) {
    if (mailbox) mailbox->word.fetch_or(0x02u, std::memory_order_release);
}

void OpenAdmission(AtomicAdmission* admission) {
    if (admission) admission->accepting.store(1u, std::memory_order_release);
}

void CloseAdmission(AtomicAdmission* admission) {
    if (admission) admission->accepting.store(0u, std::memory_order_release);
}

AdmissionTicket EnterCallback(AtomicAdmission* admission) {
    AdmissionTicket ticket{};
    if (!admission) return ticket;
    admission->callbacks.fetch_add(1u, std::memory_order_acq_rel);
    ticket.counted = true;
    if (admission->accepting.load(std::memory_order_acquire) == 0u) return ticket;
    uint32_t expected = 0u;
    if (!admission->behaviorBusy.compare_exchange_strong(
            expected, 1u, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return ticket;
    }
    // Close may race the exclusive acquisition. Recheck before exposing behavior ownership.
    if (admission->accepting.load(std::memory_order_acquire) == 0u) {
        admission->behaviorBusy.store(0u, std::memory_order_release);
        return ticket;
    }
    ticket.behaviorAdmitted = true;
    return ticket;
}

void LeaveCallback(AtomicAdmission* admission, AdmissionTicket ticket) {
    if (!admission || !ticket.counted) return;
    if (ticket.behaviorAdmitted) {
        admission->behaviorBusy.store(0u, std::memory_order_release);
    }
    admission->callbacks.fetch_sub(1u, std::memory_order_acq_rel);
}

uint32_t ActiveCallbacks(const AtomicAdmission* admission) {
    return admission ? admission->callbacks.load(std::memory_order_acquire) : 0u;
}

void PublishTelemetry(AtomicTelemetryMailbox* mailbox, const Telemetry& telemetry) {
    if (!mailbox) return;
    mailbox->epoch.fetch_add(1u, std::memory_order_acq_rel);
    mailbox->state.store(static_cast<uint32_t>(telemetry.state), std::memory_order_relaxed);
    mailbox->outcome.store(static_cast<uint32_t>(telemetry.outcome), std::memory_order_relaxed);
    mailbox->generation.store(telemetry.generation, std::memory_order_relaxed);
    mailbox->threadId.store(telemetry.threadId, std::memory_order_relaxed);
    mailbox->callback.store(static_cast<uint32_t>(telemetry.callback), std::memory_order_relaxed);
    mailbox->returnAddress.store(telemetry.returnAddress, std::memory_order_relaxed);
    mailbox->battleDiscriminator.store(
        telemetry.battleDiscriminator, std::memory_order_relaxed);
    mailbox->beforeHash.store(telemetry.beforeHash, std::memory_order_relaxed);
    mailbox->applyHash.store(telemetry.applyHash, std::memory_order_relaxed);
    mailbox->restoreHash.store(telemetry.restoreHash, std::memory_order_relaxed);
    mailbox->epoch.fetch_add(1u, std::memory_order_release);
}

bool ReadTelemetry(const AtomicTelemetryMailbox* mailbox, Telemetry* telemetry) {
    if (!mailbox || !telemetry) return false;
    for (unsigned attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = mailbox->epoch.load(std::memory_order_acquire);
        if (before == 0u || (before & 1u) != 0u) continue;
        Telemetry candidate{};
        candidate.state = static_cast<State>(mailbox->state.load(std::memory_order_relaxed));
        candidate.outcome = static_cast<ServiceOutcome>(
            mailbox->outcome.load(std::memory_order_relaxed));
        candidate.generation = mailbox->generation.load(std::memory_order_relaxed);
        candidate.threadId = mailbox->threadId.load(std::memory_order_relaxed);
        candidate.callback = static_cast<CallbackKind>(
            mailbox->callback.load(std::memory_order_relaxed));
        candidate.returnAddress = mailbox->returnAddress.load(std::memory_order_relaxed);
        candidate.battleDiscriminator = mailbox->battleDiscriminator.load(
            std::memory_order_relaxed);
        candidate.beforeHash = mailbox->beforeHash.load(std::memory_order_relaxed);
        candidate.applyHash = mailbox->applyHash.load(std::memory_order_relaxed);
        candidate.restoreHash = mailbox->restoreHash.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t after = mailbox->epoch.load(std::memory_order_relaxed);
        if (before == after && (after & 1u) == 0u) {
            *telemetry = candidate;
            return true;
        }
    }
    return false;
}

EntryServiceResult ServiceEntry(
    const Command& command, uint8_t battleDiscriminator, const ServiceIo& io,
    const EntryOriginalIo& original, Ownership* ownership, Telemetry* telemetry) {
    EntryServiceResult result{};
    if (telemetry) *telemetry = Telemetry{};
    const auto callOriginal = [&]() {
        if (original.call) result.originalResult = original.call(original.context);
    };
    if (!ownership || !original.call || command.generation == 0u || !ValidServiceIo(io)) {
        callOriginal();
        result.outcome = ServiceOutcome::InvalidInput;
        if (ownership) SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }

    const bool requested = command.requested && !command.stopping;
    if (battleDiscriminator != kAcceptedBattleDiscriminator) {
        ownership->state = requested ? State::PendingBattle : State::Off;
        callOriginal();
        result.outcome = requested ? ServiceOutcome::DeferredBattle : ServiceOutcome::NoChange;
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }

    bool retiredPreviousBattle = false;
    if (ownership->battleRosterOwned || ownership->persistentMayContainSeymour) {
        // WHY: the game can skip the exit sync call at VA 0x00790F02. Reaching the exact
        // battle-state InitScene caller again proves that the prior local buffers have ended;
        // a failed post-Assign readback can also leave both persistent and copied local state
        // uncertain. Retry exact ownership cleanup before any new battle can reintroduce actor 7.
        if (!CleanupPersistent(io, ownership, telemetry)) {
            ownership->state = State::RestorePending;
            callOriginal();
            result.outcome = ServiceOutcome::RestorePending;
            SetTelemetry(telemetry, command, *ownership, result.outcome);
            return result;
        }
        ClearBattleOwnership(ownership);
        retiredPreviousBattle = true;
    }

    if (!requested) {
        ownership->state = State::Off;
        callOriginal();
        result.outcome = retiredPreviousBattle
            ? ServiceOutcome::Restored
            : ServiceOutcome::NoChange;
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }
    if (!io.commandCurrent(io.context, &command)) {
        ownership->state = State::PendingBattle;
        callOriginal();
        result.outcome = ServiceOutcome::CommandSuperseded;
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }

    RosterImage baseline{};
    if (!ReadImage(io, &baseline, telemetry ? &telemetry->beforeHash : nullptr) ||
        ValidatePreflight(baseline, battleDiscriminator) != PreflightResult::Ready) {
        ownership->state = State::RejectedPreflight;
        callOriginal();
        result.outcome = ServiceOutcome::RejectedPreflight;
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }

    ownership->baseline = baseline;
    ownership->hasBaseline = true;
    ownership->activeGeneration = command.generation;
    (void)io.assign(io.context, kSeymourSlot, 1);
    RosterImage applied{};
    if (!ReadImage(io, &applied, telemetry ? &telemetry->applyHash : nullptr)) {
        ownership->persistentMayContainSeymour = true;
        // WHY: vanilla InitScene still runs below. Without a trustworthy post-Assign readback,
        // the battle-local copy may inherit Seymour and must remain owned until exit/next-entry
        // readback proves official cleanup.
        ownership->battleRosterOwned = true;
        ownership->state = State::RestorePending;
        callOriginal();
        result.outcome = ServiceOutcome::RestorePending;
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }
    if (!IsExactInitialApply(baseline, applied)) {
        ownership->persistentMayContainSeymour = !SameRoster(baseline, applied);
        if (IsConservativeExitOwnedImage(baseline, applied) &&
            CleanupPersistent(io, ownership, telemetry)) {
            ClearBattleOwnership(ownership);
            ownership->state = State::RejectedPreflight;
            result.outcome = ServiceOutcome::ApplyFailedRolledBack;
        } else if (SameRoster(baseline, applied)) {
            ClearBattleOwnership(ownership);
            ownership->state = State::RejectedPreflight;
            result.outcome = ServiceOutcome::RejectedPreflight;
        } else {
            // Vanilla can copy this unproved persistent image into the battle-local arrays.
            ownership->battleRosterOwned = true;
            ownership->state = State::RestorePending;
            result.outcome = ServiceOutcome::RestorePending;
        }
        callOriginal();
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }

    ownership->persistentMayContainSeymour = true;
    if (!io.commandCurrent(io.context, &command)) {
        if (CleanupPersistent(io, ownership, telemetry)) {
            ClearBattleOwnership(ownership);
            ownership->state = State::Off;
            result.outcome = ServiceOutcome::CommandSupersededRolledBack;
        } else {
            ownership->battleRosterOwned = true;
            ownership->state = State::RestorePending;
            result.outcome = ServiceOutcome::RestorePending;
        }
        callOriginal();
        SetTelemetry(telemetry, command, *ownership, result.outcome);
        return result;
    }

    // KEY: the temporary persistent ON image must exist only while vanilla InitScene runs. Its
    // single ActorInit call then copies the roster locally and executes Difficulty/S.I.N. inside.
    callOriginal();
    ownership->battleRosterOwned = true;
    const bool cleaned = CleanupPersistent(io, ownership, telemetry);
    const bool stillRequested = io.commandCurrent(io.context, &command);
    if (cleaned && stillRequested) {
        ownership->state = State::AppliedBattleRoster;
        result.outcome = ServiceOutcome::Applied;
    } else {
        ownership->state = State::RestorePending;
        result.outcome = ServiceOutcome::RestorePending;
    }
    SetTelemetry(telemetry, command, *ownership, result.outcome);
    return result;
}

ServiceOutcome ServiceExit(
    const Command& command, const ServiceIo& io, const ExitOriginalIo& original,
    Ownership* ownership, Telemetry* telemetry) {
    if (telemetry) *telemetry = Telemetry{};
    // KEY: VA 0x790F02 calls SyncPartyStatsFromActors only when its global gate permits it and then
    // continues at 0x790F07 into FreeBuffers. Cleanup must therefore follow, never precede, vanilla.
    if (original.call) original.call(original.context);
    if (!ownership || !original.call || command.generation == 0u || !ValidServiceIo(io)) {
        if (ownership) SetTelemetry(telemetry, command, *ownership, ServiceOutcome::InvalidInput);
        return ServiceOutcome::InvalidInput;
    }
    const bool requested = command.requested && !command.stopping;
    if (!ownership->battleRosterOwned && !ownership->persistentMayContainSeymour) {
        ownership->state = requested ? State::PendingBattle : State::Off;
        SetTelemetry(telemetry, command, *ownership, ServiceOutcome::NoChange);
        return ServiceOutcome::NoChange;
    }

    RosterImage synced{};
    if (!ReadImage(io, &synced, telemetry ? &telemetry->applyHash : nullptr) ||
        (ownership->battleRosterOwned &&
         !IsConservativeExitOwnedImage(ownership->baseline, synced))) {
        // A clean persistent image while battle-local ownership is still marked can mean the
        // game skipped its exit sync. Keep the marker for next-battle cleanup instead of claiming
        // that the unseen local arrays were restored.
        ownership->persistentMayContainSeymour = true;
        ownership->state = State::RestorePending;
        SetTelemetry(telemetry, command, *ownership, ServiceOutcome::RestorePending);
        return ServiceOutcome::RestorePending;
    }
    // CleanupPersistent accepts only the exact restored baseline or the conservative owned
    // one-FF-to-7 multiset for persistent-only uncertainty.
    ownership->persistentMayContainSeymour = true;
    if (!CleanupPersistent(io, ownership, telemetry)) {
        ownership->state = State::RestorePending;
        SetTelemetry(telemetry, command, *ownership, ServiceOutcome::RestorePending);
        return ServiceOutcome::RestorePending;
    }
    ClearBattleOwnership(ownership);
    ownership->state = requested ? State::PendingBattle : State::Off;
    SetTelemetry(telemetry, command, *ownership, ServiceOutcome::Restored);
    return ServiceOutcome::Restored;
}

ProducerPublication PublishProducerState(
    std::atomic<uint32_t>* publication, bool ready, bool terminalFailure) {
    if (!publication) return ProducerPublication::Terminal;
    uint32_t observed = publication->load(std::memory_order_acquire);
    for (;;) {
        const ProducerPublication current =
            static_cast<ProducerPublication>(observed);
        if (current == ProducerPublication::Terminal) {
            return ProducerPublication::Terminal;
        }
        const bool validCurrent = current == ProducerPublication::Unknown ||
                                  current == ProducerPublication::Ready;
        const ProducerPublication desired = !validCurrent || terminalFailure
            ? ProducerPublication::Terminal
            : ready ? ProducerPublication::Ready : current;
        if (desired == current) return current;
        if (publication->compare_exchange_weak(
                observed, static_cast<uint32_t>(desired),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return desired;
        }
        // KEY: retry against the newly observed value. If another thread won with Terminal, the
        // next iteration returns it without ever allowing this Ready edge to overwrite failure.
    }
}

ProducerStartupDisposition ClassifyProducerAtInstall(
    ProducerPublication publication) {
    switch (publication) {
        case ProducerPublication::Unknown:
            return ProducerStartupDisposition::Pending;
        case ProducerPublication::Ready:
            return ProducerStartupDisposition::Available;
        case ProducerPublication::Terminal:
        default:
            return ProducerStartupDisposition::ProducerUnavailable;
    }
}

bool ArmTeardown(TeardownMachine* machine, bool exitOwned, bool composerOwned) {
    if (!machine || (!exitOwned && !composerOwned)) return false;
    if (machine->phase != TeardownPhase::Inactive) {
        return machine->phase != TeardownPhase::Complete &&
               machine->exitOwned == exitOwned &&
               machine->composerOwned == composerOwned;
    }
    machine->exitOwned = exitOwned;
    machine->composerOwned = composerOwned;
    machine->phase = exitOwned
        ? TeardownPhase::ExitRetirement
        : TeardownPhase::ComposerUnregister;
    return true;
}

TeardownResult AdvanceTeardown(
    TeardownMachine* machine, bool ownershipClear, const TeardownIo& io) {
    if (!machine) return TeardownResult::InvalidArgument;
    if (machine->phase == TeardownPhase::Inactive) return TeardownResult::Inactive;
    if (machine->phase == TeardownPhase::Complete) return TeardownResult::Complete;
    if (!ownershipClear) return TeardownResult::OwnershipPending;

    if (machine->exitOwned) {
        machine->phase = TeardownPhase::ExitRetirement;
        if (!io.retireExit || !io.retireExit(io.context)) {
            return TeardownResult::ExitRetryRequired;
        }
        machine->exitOwned = false;
    }
    if (machine->composerOwned) {
        machine->phase = TeardownPhase::ComposerUnregister;
        if (!io.unregisterComposer || !io.unregisterComposer(io.context)) {
            return TeardownResult::ComposerRetryRequired;
        }
        machine->composerOwned = false;
    }
    machine->phase = TeardownPhase::Complete;
    return TeardownResult::Complete;
}

ExitDetourResult InstallExitDetour(
    const ExitDetourIo& io, MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo, MinHookBatch::NeutralizationFence fence,
    uintptr_t target, void* detour, ExitDetourOwner* owner) {
    if (!owner || !coordinator || !ValidExitDetourIo(io) || !target || !detour) {
        return ExitDetourResult::InvalidArgument;
    }
    if (owner->created || owner->applyAttempted || owner->mayHaveRun || owner->active ||
        owner->retainedInert || owner->coordinatorPoisoned) {
        return ExitDetourResult::AlreadyOwned;
    }

    owner->target = target;
    void* original = nullptr;
    if (!io.create(io.context, target, detour, &original) || !original) {
        ClearExitOwner(owner);
        return ExitDetourResult::CreateFailed;
    }
    owner->created = true;
    owner->original = original;
    // KEY: MinHook Apply may make the detour reachable immediately. Publishing the trampoline
    // before the queue boundary prevents a callback from observing an uninitialized original.
    if (!io.publishOriginal(io.context, original)) {
        RetainCreatedExitInert(owner);
        return ExitDetourResult::PublicationFailed;
    }
    const MinHookBatch::BatchReport report = MinHookBatch::EnableBatch(
        coordinator, batchIo, MinHookBatch::Owner::SeymourBattle, &target, 1u, fence);
    owner->applyAttempted = report.applyAttempted;
    owner->mayHaveRun = report.mayHaveRun;
    if (report.result == MinHookBatch::BatchResult::Applied) {
        owner->active = true;
        return ExitDetourResult::Installed;
    }
    if (report.result == MinHookBatch::BatchResult::EnableFailedNeutralized) {
        owner->retainedInert = true;
        return ExitDetourResult::EnableFailedRetained;
    }
    if (report.result == MinHookBatch::BatchResult::Busy ||
        report.result == MinHookBatch::BatchResult::NotInitialized ||
        report.result == MinHookBatch::BatchResult::InvalidArgument) {
        // WHY: the shared process MinHook lifetime forbids target removal. A created-but-disabled
        // target is harmless and remains available for restart diagnostics without invalidating
        // any process-global trampoline storage.
        RetainCreatedExitInert(owner);
        if (report.result == MinHookBatch::BatchResult::Busy) {
            return ExitDetourResult::CoordinatorBusy;
        }
        return report.result == MinHookBatch::BatchResult::NotInitialized
            ? ExitDetourResult::CoordinatorNotReady
            : ExitDetourResult::InvalidArgument;
    }
    if (!report.applyAttempted && report.queuedEnableCount == 0u) {
        RetainCreatedExitInert(owner);
        return ExitDetourResult::CoordinatorPoisoned;
    }
    owner->coordinatorPoisoned = true;
    owner->active = !report.exactDisabled;
    owner->retainedInert = report.exactDisabled;
    return ExitDetourResult::CoordinatorPoisoned;
}

ExitDetourResult RetireExitDetour(
    const ExitDetourIo& io, MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo, MinHookBatch::NeutralizationFence fence,
    ExitDetourOwner* owner) {
    if (!owner || !coordinator || !ValidExitDetourIo(io)) {
        return ExitDetourResult::InvalidArgument;
    }
    if (!owner->created) {
        ClearExitOwner(owner);
        return ExitDetourResult::Removed;
    }
    if (!owner->applyAttempted && !owner->mayHaveRun && !owner->coordinatorPoisoned) {
        RetainCreatedExitInert(owner);
        return ExitDetourResult::RetainedInert;
    }
    if (owner->coordinatorPoisoned) return ExitDetourResult::CoordinatorPoisoned;

    const uintptr_t target = owner->target;
    const MinHookBatch::BatchReport report = MinHookBatch::NeutralizeBatch(
        coordinator, batchIo, MinHookBatch::Owner::SeymourBattle, &target, 1u, fence);
    owner->applyAttempted = owner->applyAttempted || report.applyAttempted;
    owner->mayHaveRun = owner->mayHaveRun || report.mayHaveRun;
    if (report.result == MinHookBatch::BatchResult::Neutralized) {
        owner->active = false;
        owner->retainedInert = true;
        // WHY: never remove a MinHook target after an Apply attempt. An entrant can still be
        // paused in the target prologue before its first C++ callback count and needs the
        // trampoline alive.
        return ExitDetourResult::RetainedInert;
    }
    if (report.result == MinHookBatch::BatchResult::Busy) {
        return ExitDetourResult::CoordinatorBusy;
    }
    if (report.result == MinHookBatch::BatchResult::Poisoned) {
        owner->coordinatorPoisoned = true;
        owner->active = !report.exactDisabled;
        owner->retainedInert = report.exactDisabled;
        return ExitDetourResult::CoordinatorPoisoned;
    }
    return ExitDetourResult::TeardownRetryRequired;
}

} // namespace FfxHooks::SeymourBattle
