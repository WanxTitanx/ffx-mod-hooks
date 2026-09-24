#include "../hooks/SharedBattleRuntime.h"
#include "../hooks/SeymourBattleCore.h"
#include "../hooks/MinHookBatchCoordinator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_checks = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using namespace FfxHooks::SharedBattleRuntime;
using namespace FfxHooks::SeymourBattle;

struct CompositionSpy {
    std::vector<std::string> events;
    int originalCalls = 0;
    int composerCalls = 0;
    bool callOriginalTwice = false;
    bool omitOriginal = false;
};

int CompositionOriginal(void* context) {
    CompositionSpy& spy = *static_cast<CompositionSpy*>(context);
    ++spy.originalCalls;
    spy.events.emplace_back("original");
    return 73;
}

void ReservedBefore(void* context, uintptr_t) {
    static_cast<CompositionSpy*>(context)->events.emplace_back("reserved-before");
}

void ReservedAfter(void* context, uintptr_t, int result) {
    CompositionSpy& spy = *static_cast<CompositionSpy*>(context);
    Expect(result == 73, "reserved post seam must observe the exact original result");
    spy.events.emplace_back("reserved-after");
}

void SeymourComposer(void* context, uintptr_t returnRva, const OriginalIo& original) {
    CompositionSpy& spy = *static_cast<CompositionSpy*>(context);
    ++spy.composerCalls;
    Expect(returnRva == kBattleStateInitSceneReturnRva,
           "composer must receive only the exact battle-state caller return RVA");
    spy.events.emplace_back("seymour-before");
    if (!spy.omitOriginal) {
        Expect(original.call(original.context) == 73,
               "composer must observe the exact original return value");
        if (spy.callOriginalTwice) {
            Expect(original.call(original.context) == 73,
                   "the guarded original call must return the cached result on a duplicate attempt");
        }
    }
    spy.events.emplace_back("seymour-after");
}

void TestSharedRuntimeRequestAndExactCallerClassification() {
    Expect(kSeymourLiveProducerRequiresInfrastructure,
           "the LIVE Seymour producer must reserve inert startup infrastructure so OFF-to-ON edits do not install hooks from Present");
    Expect(!AnyConsumerRequiresRuntime({}), "no consumer must keep shared battle hooks inert");
    Expect(AnyConsumerRequiresRuntime({true, false, false, false}),
           "Difficulty alone must request the shared runtime");
    Expect(AnyConsumerRequiresRuntime({false, true, false, false}),
           "Seymour producer alone must request the shared runtime");
    Expect(AnyConsumerRequiresRuntime({false, false, true, false}),
           "future S.I.N. alone must request the shared runtime");
    Expect(AnyConsumerRequiresRuntime({false, false, false, true}),
           "future CustomMix alone must request the shared runtime");

    Expect(ClassifyInitSceneCaller(kBattleStateInitSceneReturnRva) ==
               InitSceneCaller::BattleState,
           "return RVA 0x38321C must classify as the only battle entry");
    Expect(ClassifyInitSceneCaller(kBootstrapInitSceneReturnRva) ==
               InitSceneCaller::Bootstrap,
           "return RVA 0x381C76 must remain a rejected global bootstrap caller");
    Expect(ClassifyInitSceneCaller(kSphereGridStartupInitSceneReturnRva) ==
               InitSceneCaller::SphereGridStartup,
           "return RVA 0x3821CF must remain a rejected startup/Sphere Grid caller");
    Expect(ClassifyInitSceneCaller(0x00381234u) == InitSceneCaller::Unknown,
           "every unproved InitScene caller must fail closed");
    Expect(IsAdmittedEntryReturnRva(kBattleStateInitSceneReturnRva) &&
               !IsAdmittedEntryReturnRva(kBootstrapInitSceneReturnRva) &&
               !IsAdmittedEntryReturnRva(kSphereGridStartupInitSceneReturnRva),
           "Seymour entry admission must be narrower than the complete InitScene xref set");
    Expect(IsAdmittedExitReturnRva(kBattleExitSyncReturnRva) &&
               !IsAdmittedExitReturnRva(kBattleExitSyncReturnRva + 1u),
           "Seymour exit admission must accept only return RVA 0x390F07");
}

void TestSharedInitSceneCompositionCallsOriginalExactlyOnce() {
    CompositionSpy spy{};
    const OriginalIo original{&spy, &CompositionOriginal};
    const ReservedSeamIo reserved{&spy, &ReservedBefore, &ReservedAfter};
    const ComposerIo composer{&spy, &SeymourComposer};
    const InitSceneRunResult result = RunInitScene(
        kBattleStateInitSceneReturnRva, original, reserved, composer);
    const std::vector<std::string> expected{
        "seymour-before", "reserved-before", "original", "reserved-after", "seymour-after"};
    Expect(result.originalResult == 73 && result.originalCallAttempts == 1u &&
               result.composerInvoked && spy.originalCalls == 1 && spy.composerCalls == 1,
           "battle composition must invoke Seymour and the original exactly once");
    Expect(spy.events == expected,
           "shared ordering must be Seymour apply, reserved pre/original/post, Seymour cleanup");

    spy = CompositionSpy{};
    spy.callOriginalTwice = true;
    const InitSceneRunResult duplicate = RunInitScene(
        kBattleStateInitSceneReturnRva, original, reserved, composer);
    Expect(duplicate.originalCallAttempts == 2u && spy.originalCalls == 1,
           "duplicate composer attempts must never execute the original twice");

    spy = CompositionSpy{};
    spy.omitOriginal = true;
    const InitSceneRunResult omitted = RunInitScene(
        kBattleStateInitSceneReturnRva, original, reserved, composer);
    Expect(omitted.originalCallAttempts == 1u && spy.originalCalls == 1,
           "pipeline fallback must execute the original once when a composer omits it");

    for (const uintptr_t denied : {
             kBootstrapInitSceneReturnRva, kSphereGridStartupInitSceneReturnRva, uintptr_t{0x12u}}) {
        spy = CompositionSpy{};
        const InitSceneRunResult passthrough = RunInitScene(denied, original, reserved, composer);
        Expect(!passthrough.composerInvoked && spy.composerCalls == 0 &&
                   spy.originalCalls == 1 && passthrough.originalCallAttempts == 1u,
               "non-battle callers must bypass every behavioral composer and call vanilla once");
        Expect(spy.events == std::vector<std::string>{"original"},
               "non-battle callers must not enter the reserved battle-only seam");
    }
}

void TestComposerSlotOwnershipIsExactAndSticky() {
    CompositionSpy firstSpy{};
    CompositionSpy foreignSpy{};
    const ComposerIo first{&firstSpy, &SeymourComposer};
    const ComposerIo foreign{&foreignSpy, &SeymourComposer};
    ComposerSlot slot{};
    Expect(slot.Register(&first) == ComposerSlotResult::Registered,
           "empty composer slot must accept its first process-lifetime descriptor");
    Expect(slot.Register(&first) == ComposerSlotResult::AlreadyRegistered,
           "same descriptor registration must be idempotent");
    Expect(slot.Register(&foreign) == ComposerSlotResult::Conflict,
           "foreign composer registration must fail closed");
    Expect(slot.Load() == &first,
           "foreign registration must not replace the current composer");
    Expect(slot.Unregister(&foreign) == ComposerSlotResult::NotOwner && slot.Load() == &first,
           "foreign unregister must not clear another owner's descriptor");
    Expect(slot.Unregister(&first) == ComposerSlotResult::Unregistered && slot.Load() == nullptr,
           "only the exact descriptor may unregister the composer");
}

RosterImage BaselineRoster() {
    RosterImage image{};
    image.party = 0x10u;
    image.state = {0u, 1u, 2u};
    image.ability = {
        3u, 4u, 5u, 6u, 8u, 9u, 10u, 11u, 12u,
        13u, 14u, 15u, 16u, 17u, 18u, 19u, 0xFFu,
    };
    return image;
}

void TestRosterOwnershipProofsRejectDrift() {
    const RosterImage baseline = BaselineRoster();
    Expect(ValidatePreflight(baseline, kAcceptedBattleDiscriminator) == PreflightResult::Ready,
           "exact clean 0x10 roster must pass preflight");
    Expect(ValidatePreflight(baseline, 6u) == PreflightResult::WrongDiscriminator,
           "non-battle discriminator must reject before mutation");

    RosterImage applied = baseline;
    applied.party = 0x11u;
    applied.ability.back() = kSeymourSlot;
    Expect(IsExactInitialApply(baseline, applied) &&
               IsConservativeOwnedImage(baseline, applied),
           "official apply must be exactly one FF-to-7 insertion and party 0x10-to-0x11");

    RosterImage switched = applied;
    std::swap(switched.state.front(), switched.ability.front());
    Expect(IsConservativeOwnedImage(baseline, switched),
           "legitimate active/reserve reorder must retain combined-20 ownership");
    RosterImage duplicate = switched;
    duplicate.ability[1] = kSeymourSlot;
    Expect(!IsConservativeOwnedImage(baseline, duplicate),
           "duplicate Seymour membership must fail closed");
    RosterImage drift = switched;
    drift.ability[1] = 0x99u;
    Expect(!IsConservativeOwnedImage(baseline, drift),
           "third-party membership drift must fail closed");
    Expect(IsConservativeRestoredImage(baseline, baseline),
           "exact clean baseline must prove restoration");
}

struct RosterRuntime {
    RosterImage roster = BaselineRoster();
    RosterImage exitSynced{};
    Command current{1u, true, false};
    int assignOnCalls = 0;
    int assignOffCalls = 0;
    int entryOriginalCalls = 0;
    int exitOriginalCalls = 0;
    int commandChecks = 0;
    int readCalls = 0;
    int rejectCommandAfter = -1;
    int failOnReadCall = -1;
    bool readFails = false;
    bool removeFails = false;
};

bool RuntimeRead(void* context, RosterImage* image) {
    RosterRuntime& runtime = *static_cast<RosterRuntime*>(context);
    ++runtime.readCalls;
    if (runtime.readFails || runtime.readCalls == runtime.failOnReadCall || !image) return false;
    *image = runtime.roster;
    return true;
}

bool RuntimeAssign(void* context, uint8_t slot, int enabled) {
    RosterRuntime& runtime = *static_cast<RosterRuntime*>(context);
    if (slot != kSeymourSlot) return false;
    if (enabled != 0) {
        ++runtime.assignOnCalls;
        runtime.roster.party = 0x11u;
        for (uint8_t& value : runtime.roster.ability) {
            if (value == 0xFFu) {
                value = kSeymourSlot;
                return true;
            }
        }
        return false;
    }
    ++runtime.assignOffCalls;
    if (runtime.removeFails) return false;
    runtime.roster.party = 0x10u;
    for (uint8_t& value : runtime.roster.state) {
        if (value == kSeymourSlot) {
            value = 0xFFu;
            return true;
        }
    }
    for (uint8_t& value : runtime.roster.ability) {
        if (value == kSeymourSlot) {
            value = 0xFFu;
            return true;
        }
    }
    return true;
}

bool RuntimeCommandCurrent(void* context, const Command* expected) {
    RosterRuntime& runtime = *static_cast<RosterRuntime*>(context);
    ++runtime.commandChecks;
    if (runtime.rejectCommandAfter >= 0 && runtime.commandChecks > runtime.rejectCommandAfter) {
        return false;
    }
    return expected && expected->generation == runtime.current.generation &&
           expected->requested == runtime.current.requested &&
           expected->stopping == runtime.current.stopping;
}

int RuntimeEntryOriginal(void* context) {
    RosterRuntime& runtime = *static_cast<RosterRuntime*>(context);
    ++runtime.entryOriginalCalls;
    return 42;
}

void RuntimeExitOriginal(void* context) {
    RosterRuntime& runtime = *static_cast<RosterRuntime*>(context);
    ++runtime.exitOriginalCalls;
    runtime.roster = runtime.exitSynced;
}

ServiceIo RosterIo(RosterRuntime& runtime) {
    return {&runtime, kStateListSize, kAbilityListSize,
            &RuntimeRead, &RuntimeAssign, &RuntimeCommandCurrent};
}

void TestEntryExitServiceUsesOfficialAssignmentAndOriginalExactlyOnce() {
    RosterRuntime defaultOff{};
    defaultOff.current = {1u, false, false};
    const RosterImage defaultOffBaseline = defaultOff.roster;
    Ownership defaultOffOwnership{};
    Telemetry defaultOffTelemetry{};
    const EntryServiceResult inertEntry = ServiceEntry(
        defaultOff.current, kAcceptedBattleDiscriminator, RosterIo(defaultOff),
        {&defaultOff, &RuntimeEntryOriginal}, &defaultOffOwnership, &defaultOffTelemetry);
    Expect(inertEntry.outcome == ServiceOutcome::NoChange &&
               defaultOff.entryOriginalCalls == 1 && defaultOff.assignOnCalls == 0 &&
               defaultOff.assignOffCalls == 0 &&
               IsConservativeRestoredImage(defaultOffBaseline, defaultOff.roster) &&
               defaultOffOwnership.state == State::Off,
           "default-OFF producer infrastructure must call vanilla once without any formation write");

    RosterRuntime runtime{};
    Ownership ownership{};
    Telemetry telemetry{};
    const EntryServiceResult entry = ServiceEntry(
        runtime.current, kAcceptedBattleDiscriminator, RosterIo(runtime),
        {&runtime, &RuntimeEntryOriginal}, &ownership, &telemetry);
    Expect(entry.outcome == ServiceOutcome::Applied && entry.originalResult == 42 &&
               runtime.entryOriginalCalls == 1 && runtime.assignOnCalls == 1 &&
               runtime.assignOffCalls == 1 &&
               ownership.state == State::AppliedBattleRoster &&
               ownership.battleRosterOwned && runtime.roster.party == 0x10u &&
               IsConservativeRestoredImage(ownership.baseline, runtime.roster),
           "entry must apply officially, call InitScene once, then clean persistent membership");

    // A game-owned gate can skip the exit sync call entirely. The next admitted InitScene is a
    // new battle and must retire the stale local-ownership marker before applying the same ON
    // generation again; otherwise Seymour silently disappears from every later battle.
    const EntryServiceResult nextEntryWithoutExit = ServiceEntry(
        runtime.current, kAcceptedBattleDiscriminator, RosterIo(runtime),
        {&runtime, &RuntimeEntryOriginal}, &ownership, &telemetry);
    Expect(nextEntryWithoutExit.outcome == ServiceOutcome::Applied &&
               runtime.entryOriginalCalls == 2 && runtime.assignOnCalls == 2 &&
               runtime.assignOffCalls == 2 && ownership.battleRosterOwned &&
               IsConservativeRestoredImage(ownership.baseline, runtime.roster),
           "a next battle after skipped exit sync must safely reapply the unchanged ON generation");

    runtime.exitSynced = ownership.baseline;
    runtime.exitSynced.party = 0x11u;
    runtime.exitSynced.ability.back() = kSeymourSlot;
    std::swap(runtime.exitSynced.state.front(), runtime.exitSynced.ability.front());
    const ServiceOutcome exit = ServiceExit(
        runtime.current, RosterIo(runtime), {&runtime, &RuntimeExitOriginal},
        &ownership, &telemetry);
    Expect(exit == ServiceOutcome::Restored && runtime.exitOriginalCalls == 1 &&
               runtime.assignOffCalls == 3 && ownership.state == State::PendingBattle &&
               !ownership.battleRosterOwned && !ownership.hasBaseline &&
               runtime.roster.party == 0x10u,
           "exit must sync vanilla once, preserve Switch order, then remove Seymour officially");

    RosterRuntime denied{};
    Ownership deniedOwnership{};
    const EntryServiceResult deniedEntry = ServiceEntry(
        denied.current, 6u, RosterIo(denied), {&denied, &RuntimeEntryOriginal},
        &deniedOwnership, nullptr);
    Expect(deniedEntry.outcome == ServiceOutcome::DeferredBattle &&
               denied.entryOriginalCalls == 1 && denied.assignOnCalls == 0,
           "wrong discriminator must pass vanilla once without touching formation state");
}

void TestServiceFailsClosedOnCommandDriftAndOfficialRemoveFailure() {
    RosterRuntime superseded{};
    superseded.rejectCommandAfter = 1;
    Ownership supersededOwnership{};
    const EntryServiceResult supersededEntry = ServiceEntry(
        superseded.current, kAcceptedBattleDiscriminator, RosterIo(superseded),
        {&superseded, &RuntimeEntryOriginal}, &supersededOwnership, nullptr);
    Expect(supersededEntry.outcome == ServiceOutcome::CommandSupersededRolledBack &&
               superseded.entryOriginalCalls == 1 && superseded.assignOnCalls == 1 &&
               superseded.assignOffCalls == 1 && !supersededOwnership.battleRosterOwned,
           "a superseded ON command must roll back officially before vanilla continues");

    RosterRuntime failedRemove{};
    failedRemove.removeFails = true;
    Ownership failedOwnership{};
    const EntryServiceResult failedEntry = ServiceEntry(
        failedRemove.current, kAcceptedBattleDiscriminator, RosterIo(failedRemove),
        {&failedRemove, &RuntimeEntryOriginal}, &failedOwnership, nullptr);
    Expect(failedEntry.outcome == ServiceOutcome::RestorePending &&
               failedRemove.entryOriginalCalls == 1 && failedOwnership.hasBaseline &&
               failedOwnership.persistentMayContainSeymour &&
               failedOwnership.state == State::RestorePending,
           "official remove failure must retain baseline and publish RestorePending");

    RosterRuntime missingExitSync{};
    Ownership exitOwnership{};
    (void)ServiceEntry(
        missingExitSync.current, kAcceptedBattleDiscriminator, RosterIo(missingExitSync),
        {&missingExitSync, &RuntimeEntryOriginal}, &exitOwnership, nullptr);
    missingExitSync.exitSynced = exitOwnership.baseline;
    const ServiceOutcome missingSync = ServiceExit(
        missingExitSync.current, RosterIo(missingExitSync),
        {&missingExitSync, &RuntimeExitOriginal}, &exitOwnership, nullptr);
    Expect(missingSync == ServiceOutcome::RestorePending &&
               exitOwnership.battleRosterOwned && exitOwnership.hasBaseline,
           "a skipped/non-owned exit sync must never discard battle roster ownership");

    RosterRuntime skippedExitThenOff{};
    Ownership skippedExitOwnership{};
    (void)ServiceEntry(
        skippedExitThenOff.current, kAcceptedBattleDiscriminator,
        RosterIo(skippedExitThenOff), {&skippedExitThenOff, &RuntimeEntryOriginal},
        &skippedExitOwnership, nullptr);
    skippedExitThenOff.current = {2u, false, false};
    const EntryServiceResult nextBattleOff = ServiceEntry(
        skippedExitThenOff.current, kAcceptedBattleDiscriminator,
        RosterIo(skippedExitThenOff), {&skippedExitThenOff, &RuntimeEntryOriginal},
        &skippedExitOwnership, nullptr);
    Expect(nextBattleOff.outcome == ServiceOutcome::Restored &&
               skippedExitThenOff.entryOriginalCalls == 2 &&
               !skippedExitOwnership.battleRosterOwned &&
               skippedExitOwnership.state == State::Off &&
               skippedExitThenOff.roster.party == 0x10u,
           "next battle must publish restored OFF after a skipped exit sync without reintroduction");
}

void TestPostAssignReadbackFailureRetainsConservativeOwnershipUntilCleanup() {
    RosterRuntime exitRecovery{};
    exitRecovery.failOnReadCall = 2;
    Ownership exitOwnership{};
    Telemetry exitTelemetry{};
    const EntryServiceResult uncertainEntry = ServiceEntry(
        exitRecovery.current, kAcceptedBattleDiscriminator, RosterIo(exitRecovery),
        {&exitRecovery, &RuntimeEntryOriginal}, &exitOwnership, &exitTelemetry);
    Expect(uncertainEntry.outcome == ServiceOutcome::RestorePending &&
               exitOwnership.hasBaseline && exitOwnership.persistentMayContainSeymour &&
               exitOwnership.battleRosterOwned && exitRecovery.entryOriginalCalls == 1,
           "a failed post-Assign readback must conservatively retain both persistent and battle-local ownership after vanilla InitScene");

    exitRecovery.exitSynced = exitOwnership.baseline;
    exitRecovery.exitSynced.party = 0x11u;
    exitRecovery.exitSynced.ability.back() = kSeymourSlot;
    const ServiceOutcome recoveredAtExit = ServiceExit(
        exitRecovery.current, RosterIo(exitRecovery), {&exitRecovery, &RuntimeExitOriginal},
        &exitOwnership, &exitTelemetry);
    Expect(recoveredAtExit == ServiceOutcome::Restored &&
               !exitOwnership.hasBaseline && !exitOwnership.battleRosterOwned &&
               !exitOwnership.persistentMayContainSeymour &&
               IsConservativeRestoredImage(BaselineRoster(), exitRecovery.roster),
           "a later exact exit sync must retry official cleanup after a transient post-Assign readback failure");

    RosterRuntime nextEntryRecovery{};
    nextEntryRecovery.failOnReadCall = 2;
    Ownership nextEntryOwnership{};
    (void)ServiceEntry(
        nextEntryRecovery.current, kAcceptedBattleDiscriminator, RosterIo(nextEntryRecovery),
        {&nextEntryRecovery, &RuntimeEntryOriginal}, &nextEntryOwnership, nullptr);
    nextEntryRecovery.current = {2u, false, false};
    const EntryServiceResult recoveredAtNextEntry = ServiceEntry(
        nextEntryRecovery.current, kAcceptedBattleDiscriminator, RosterIo(nextEntryRecovery),
        {&nextEntryRecovery, &RuntimeEntryOriginal}, &nextEntryOwnership, nullptr);
    Expect(recoveredAtNextEntry.outcome == ServiceOutcome::Restored &&
               !nextEntryOwnership.hasBaseline && !nextEntryOwnership.battleRosterOwned &&
               !nextEntryOwnership.persistentMayContainSeymour &&
               nextEntryRecovery.roster.party == 0x10u &&
               IsConservativeRestoredImage(BaselineRoster(), nextEntryRecovery.roster),
           "a skipped exit followed by OFF at next battle entry must clean uncertain membership without reintroduction");
}

struct ExitDetourSpy {
    uintptr_t target = 0x00786080u;
    bool created = false;
    bool enabled = false;
    bool createFails = false;
    bool removeFails = false;
    bool queueEnableFails = false;
    bool queueDisableFails = false;
    int applyFailuresRemaining = 0;
    bool disableFails = false;
    bool publishFails = false;
    void* publishedOriginal = nullptr;
    bool applySawPublication = false;
    int createCalls = 0;
    int publishCalls = 0;
    int removeCalls = 0;
    int queueEnableCalls = 0;
    int queueDisableCalls = 0;
    int applyCalls = 0;
    int disableCalls = 0;
};

bool ExitInitialize(void*) { return true; }

bool ExitCreate(void* context, uintptr_t target, void* detour, void** originalOut) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.createCalls;
    if (spy.createFails || target != spy.target || !detour || !originalOut || spy.created) {
        return false;
    }
    spy.created = true;
    *originalOut = reinterpret_cast<void*>(0x00786080u);
    return true;
}

bool ExitRemove(void* context, uintptr_t target) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.removeCalls;
    if (spy.removeFails || target != spy.target || !spy.created) return false;
    spy.created = false;
    return true;
}

bool ExitPublishOriginal(void* context, void* original) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.publishCalls;
    if (spy.publishFails || !original) return false;
    spy.publishedOriginal = original;
    return true;
}

bool ExitQueueEnable(void* context, uintptr_t target) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.queueEnableCalls;
    if (spy.queueEnableFails || target != spy.target || !spy.created) return false;
    spy.enabled = true;
    return true;
}

bool ExitQueueDisable(void* context, uintptr_t target) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.queueDisableCalls;
    if (spy.queueDisableFails || target != spy.target || !spy.created) return false;
    spy.enabled = false;
    return true;
}

bool ExitApply(void* context) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.applyCalls;
    spy.applySawPublication = spy.publishedOriginal != nullptr;
    if (spy.applyFailuresRemaining > 0) {
        --spy.applyFailuresRemaining;
        return false;
    }
    return true;
}

bool ExitDisable(void* context, uintptr_t target) {
    ExitDetourSpy& spy = *static_cast<ExitDetourSpy*>(context);
    ++spy.disableCalls;
    if (spy.disableFails || target != spy.target) return false;
    spy.enabled = false;
    return true;
}

ExitDetourIo ExitCreateIo(ExitDetourSpy& spy) {
    return {&spy, &ExitCreate, &ExitPublishOriginal};
}

FfxHooks::MinHookBatch::BatchIo ExitBatchIo(ExitDetourSpy& spy) {
    return {&spy, &ExitQueueEnable, &ExitQueueDisable, &ExitApply, &ExitDisable};
}

void PrimeCoordinator(FfxHooks::MinHookBatch::Coordinator& coordinator) {
    Expect(FfxHooks::MinHookBatch::EnsureInitialized(
               &coordinator, {nullptr, &ExitInitialize}) ==
               FfxHooks::MinHookBatch::InitializationResult::Ready,
           "test coordinator must initialize before a Seymour batch");
}

void TestUniqueExitDetourRetainsProcessLifetimeOwnership() {
    ExitDetourSpy spy{};
    FfxHooks::MinHookBatch::Coordinator coordinator;
    PrimeCoordinator(coordinator);
    ExitDetourOwner owner{};
    const ExitDetourResult installed = InstallExitDetour(
        ExitCreateIo(spy), &coordinator, ExitBatchIo(spy), {}, spy.target,
        reinterpret_cast<void*>(0x00500000u), &owner);
    Expect(installed == ExitDetourResult::Installed && owner.active && owner.created &&
               owner.applyAttempted && owner.mayHaveRun && owner.original != nullptr &&
               spy.createCalls == 1 && spy.publishCalls == 1 && spy.applyCalls == 1 &&
               spy.applySawPublication && spy.removeCalls == 0,
           "the exit trampoline must be release-published before the one shared Apply");

    const ExitDetourResult retired = RetireExitDetour(
        ExitCreateIo(spy), &coordinator, ExitBatchIo(spy), {}, &owner);
    Expect(retired == ExitDetourResult::RetainedInert && !owner.active &&
               owner.retainedInert && owner.created && owner.original != nullptr &&
               spy.removeCalls == 0 && !spy.enabled,
           "an applied exit target and trampoline must remain process-lifetime after disable");
}

void TestUniqueExitDetourRollbackAndPoisonFailures() {
    ExitDetourSpy uninitializedSpy{};
    FfxHooks::MinHookBatch::Coordinator uninitialized;
    ExitDetourOwner uninitializedOwner{};
    const ExitDetourResult notReady = InstallExitDetour(
        ExitCreateIo(uninitializedSpy), &uninitialized, ExitBatchIo(uninitializedSpy), {},
        uninitializedSpy.target, reinterpret_cast<void*>(0x00500000u), &uninitializedOwner);
    Expect(notReady == ExitDetourResult::CoordinatorNotReady &&
               uninitializedOwner.created && uninitializedOwner.retainedInert &&
               uninitializedSpy.removeCalls == 0,
           "a created exit target must remain process-lifetime even before an unavailable Apply");

    ExitDetourSpy publicationSpy{};
    publicationSpy.publishFails = true;
    FfxHooks::MinHookBatch::Coordinator publicationCoordinator;
    PrimeCoordinator(publicationCoordinator);
    ExitDetourOwner publicationOwner{};
    const ExitDetourResult publicationFailed = InstallExitDetour(
        ExitCreateIo(publicationSpy), &publicationCoordinator, ExitBatchIo(publicationSpy), {},
        publicationSpy.target, reinterpret_cast<void*>(0x00500000u), &publicationOwner);
    Expect(publicationFailed == ExitDetourResult::PublicationFailed &&
               publicationOwner.created && publicationOwner.retainedInert &&
               publicationSpy.applyCalls == 0 && publicationSpy.removeCalls == 0,
           "failed trampoline publication must prevent Apply and retain the disabled target");

    ExitDetourSpy queueSpy{};
    queueSpy.queueEnableFails = true;
    FfxHooks::MinHookBatch::Coordinator queueCoordinator;
    PrimeCoordinator(queueCoordinator);
    ExitDetourOwner queueOwner{};
    const ExitDetourResult queueFailure = InstallExitDetour(
        ExitCreateIo(queueSpy), &queueCoordinator, ExitBatchIo(queueSpy), {}, queueSpy.target,
        reinterpret_cast<void*>(0x00500000u), &queueOwner);
    Expect(queueFailure == ExitDetourResult::EnableFailedRetained &&
               queueOwner.applyAttempted && queueOwner.mayHaveRun &&
               queueOwner.retainedInert && queueOwner.created && queueSpy.removeCalls == 0,
           "queue failure neutralization still crosses Apply and must retain the trampoline");

    ExitDetourSpy poisonSpy{};
    poisonSpy.applyFailuresRemaining = 1;
    FfxHooks::MinHookBatch::Coordinator poisonCoordinator;
    PrimeCoordinator(poisonCoordinator);
    ExitDetourOwner poisonOwner{};
    const ExitDetourResult poisoned = InstallExitDetour(
        ExitCreateIo(poisonSpy), &poisonCoordinator, ExitBatchIo(poisonSpy), {},
        poisonSpy.target, reinterpret_cast<void*>(0x00500000u), &poisonOwner);
    Expect(poisoned == ExitDetourResult::CoordinatorPoisoned &&
               poisonOwner.coordinatorPoisoned && poisonOwner.created &&
               poisonOwner.applyAttempted && poisonSpy.removeCalls == 0,
           "failed Apply must poison the process coordinator and forbid hook removal");
}

struct TeardownSpy {
    int retireCalls = 0;
    int unregisterCalls = 0;
    int retireFailuresRemaining = 0;
    int unregisterFailuresRemaining = 0;
};

bool TeardownRetireExit(void* context) {
    TeardownSpy& spy = *static_cast<TeardownSpy*>(context);
    ++spy.retireCalls;
    if (spy.retireFailuresRemaining > 0) {
        --spy.retireFailuresRemaining;
        return false;
    }
    return true;
}

bool TeardownUnregisterComposer(void* context) {
    TeardownSpy& spy = *static_cast<TeardownSpy*>(context);
    ++spy.unregisterCalls;
    if (spy.unregisterFailuresRemaining > 0) {
        --spy.unregisterFailuresRemaining;
        return false;
    }
    return true;
}

TeardownIo TeardownOperations(TeardownSpy& spy) {
    return {&spy, &TeardownRetireExit, &TeardownUnregisterComposer};
}

void TestTeardownRetriesExitBeforeUnregisteringComposer() {
    TeardownSpy spy{};
    spy.retireFailuresRemaining = 1;
    TeardownMachine machine{};
    Expect(ArmTeardown(&machine, true, true),
           "installed Seymour must arm exact exit and composer ownership");
    Expect(AdvanceTeardown(&machine, true, TeardownOperations(spy)) ==
               TeardownResult::ExitRetryRequired &&
               machine.phase == TeardownPhase::ExitRetirement &&
               machine.exitOwned && machine.composerOwned &&
               spy.retireCalls == 1 && spy.unregisterCalls == 0,
           "failed exit retirement must preserve both owners and never unregister the composer");
    Expect(AdvanceTeardown(&machine, true, TeardownOperations(spy)) ==
               TeardownResult::Complete &&
               machine.phase == TeardownPhase::Complete &&
               !machine.exitOwned && !machine.composerOwned &&
               spy.retireCalls == 2 && spy.unregisterCalls == 1,
           "a later exit-retirement retry must continue through exact composer unregister");
    Expect(AdvanceTeardown(&machine, true, TeardownOperations(spy)) ==
               TeardownResult::Complete &&
               spy.retireCalls == 2 && spy.unregisterCalls == 1,
           "completed teardown must be idempotent and issue no duplicate lifecycle operation");
}

void TestTeardownRetriesComposerWithoutRepeatingSuccessfulExitRetirement() {
    TeardownSpy spy{};
    spy.unregisterFailuresRemaining = 1;
    TeardownMachine machine{};
    Expect(ArmTeardown(&machine, true, true),
           "installed ownership must arm before composer retry coverage");
    Expect(AdvanceTeardown(&machine, true, TeardownOperations(spy)) ==
               TeardownResult::ComposerRetryRequired &&
               machine.phase == TeardownPhase::ComposerUnregister &&
               !machine.exitOwned && machine.composerOwned &&
               spy.retireCalls == 1 && spy.unregisterCalls == 1,
           "failed composer unregister must retain only composer ownership");
    Expect(AdvanceTeardown(&machine, true, TeardownOperations(spy)) ==
               TeardownResult::Complete &&
               spy.retireCalls == 1 && spy.unregisterCalls == 2,
           "composer retry must not repeat an already-proved exit retirement");
}

void TestStopAndConflictRollbackNeverClearUnprovedOwnership() {
    TeardownSpy installedSpy{};
    TeardownMachine installed{};
    Expect(ArmTeardown(&installed, true, true),
           "installed teardown must arm before stop coverage");
    AtomicCommandMailbox mailbox{};
    Expect(PublishRequested(&mailbox, true), "test command must begin requested ON");
    RequestStop(&mailbox);
    const Command stopped = ReadCommand(&mailbox);
    Expect(stopped.stopping && !stopped.requested &&
               AdvanceTeardown(&installed, false, TeardownOperations(installedSpy)) ==
                   TeardownResult::OwnershipPending &&
               installed.exitOwned && installed.composerOwned &&
               installedSpy.retireCalls == 0 && installedSpy.unregisterCalls == 0,
           "DllMain-style stop while roster-owned must be non-mutating and cannot authorize teardown");

    TeardownSpy conflictSpy{};
    conflictSpy.retireFailuresRemaining = 1;
    TeardownMachine conflictRollback{};
    Expect(ArmTeardown(&conflictRollback, true, false),
           "ComposerConflict rollback must retain the already-enabled exit owner");
    Expect(AdvanceTeardown(&conflictRollback, true, TeardownOperations(conflictSpy)) ==
               TeardownResult::ExitRetryRequired && conflictRollback.exitOwned &&
               conflictSpy.retireCalls == 1,
           "ComposerConflict rollback failure must remain retryable instead of publishing terminal conflict");
    Expect(AdvanceTeardown(&conflictRollback, true, TeardownOperations(conflictSpy)) ==
               TeardownResult::Complete && !conflictRollback.exitOwned &&
               conflictSpy.retireCalls == 2 && conflictSpy.unregisterCalls == 0,
           "ComposerConflict rollback must become terminal only after exact exit retirement succeeds");
}

void TestProducerTerminalIsAbsorbingAcrossStartup() {
    std::atomic<uint32_t> publication{
        static_cast<uint32_t>(ProducerPublication::Unknown)};
    Expect(ClassifyProducerAtInstall(ProducerPublication::Unknown) ==
               ProducerStartupDisposition::Pending,
           "an unknown Present producer must remain pending at hook startup");
    Expect(PublishProducerState(&publication, false, true) ==
               ProducerPublication::Terminal &&
               ClassifyProducerAtInstall(ProducerPublication::Terminal) ==
                   ProducerStartupDisposition::ProducerUnavailable,
           "a terminal Present result published before Start must classify as unavailable");
    Expect(PublishProducerState(&publication, true, false) ==
               ProducerPublication::Terminal &&
               publication.load(std::memory_order_acquire) ==
                   static_cast<uint32_t>(ProducerPublication::Terminal),
           "Terminal producer state must absorb every later Ready publication");

    publication.store(static_cast<uint32_t>(ProducerPublication::Unknown),
                      std::memory_order_release);
    Expect(PublishProducerState(&publication, true, false) ==
               ProducerPublication::Ready &&
               ClassifyProducerAtInstall(ProducerPublication::Ready) ==
                   ProducerStartupDisposition::Available,
           "a validated Present producer must remain available at hook startup");
}

void TestAdmissionCountsBeforeBehaviorAndStopIsAbsorbing() {
    AtomicAdmission admission{};
    OpenAdmission(&admission);
    const AdmissionTicket first = EnterCallback(&admission);
    const AdmissionTicket concurrent = EnterCallback(&admission);
    Expect(first.counted && first.behaviorAdmitted &&
               concurrent.counted && !concurrent.behaviorAdmitted &&
               ActiveCallbacks(&admission) == 2u,
           "every callback must be counted before exclusive behavior admission");
    CloseAdmission(&admission);
    LeaveCallback(&admission, concurrent);
    LeaveCallback(&admission, first);
    Expect(ActiveCallbacks(&admission) == 0u,
           "balanced callback tickets must drain exactly to zero");
    const AdmissionTicket closed = EnterCallback(&admission);
    Expect(closed.counted && !closed.behaviorAdmitted,
           "closed admission must retain trampoline-safe counting but reject behavior");
    LeaveCallback(&admission, closed);

    AtomicCommandMailbox mailbox{};
    Expect(PublishRequested(&mailbox, true), "Present must publish an ON generation");
    RequestStop(&mailbox);
    const Command stopped = ReadCommand(&mailbox);
    Expect(stopped.stopping && !stopped.requested && !PublishRequested(&mailbox, true),
           "stop must be absorbing and suppress stale requested state");
}

} // namespace

int main() {
    TestSharedRuntimeRequestAndExactCallerClassification();
    TestSharedInitSceneCompositionCallsOriginalExactlyOnce();
    TestComposerSlotOwnershipIsExactAndSticky();
    TestRosterOwnershipProofsRejectDrift();
    TestEntryExitServiceUsesOfficialAssignmentAndOriginalExactlyOnce();
    TestServiceFailsClosedOnCommandDriftAndOfficialRemoveFailure();
    TestPostAssignReadbackFailureRetainsConservativeOwnershipUntilCleanup();
    TestUniqueExitDetourRetainsProcessLifetimeOwnership();
    TestUniqueExitDetourRollbackAndPoisonFailures();
    TestTeardownRetriesExitBeforeUnregisteringComposer();
    TestTeardownRetriesComposerWithoutRepeatingSuccessfulExitRetirement();
    TestStopAndConflictRollbackNeverClearUnprovedOwnership();
    TestProducerTerminalIsAbsorbingAcrossStartup();
    TestAdmissionCountsBeforeBehaviorAndStopIsAbsorbing();
    std::cout << "SEYMOUR BATTLE RT1: " << g_checks << '/' << g_checks << " PASS\n";
    return 0;
}
