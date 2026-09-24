#include "../hooks/CustomMixRuntime.h"
#include "../hooks/F7UiCore.h"
#include "../hooks/SharedBattleRuntime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

using namespace FfxHooks::CustomMixUltra;
namespace Runtime = FfxHooks::CustomMixUltra::Runtime;
namespace Shared = FfxHooks::SharedBattleRuntime;

int g_passed = 0;
int g_failed = 0;

constexpr std::size_t kFixtureSize = 0x4428u;
constexpr std::size_t kFixtureChunk2Offset = 0x3F6Cu;
constexpr std::size_t kFixtureChunk3Offset = 0x3F88u;
constexpr std::size_t kFixtureSlotOffset = 0x3F78u;

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

void PutU16(std::uint8_t* bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void PutU32(std::uint8_t* bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::uint16_t ReadU16(const std::uint8_t* bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1u] << 8u);
}

void FillCarrier(std::uint8_t* bytes) {
    std::memset(bytes, 0xCC, kFixtureSize);
    const std::array<std::uint32_t, 9u> header = {{
        8u, 0x30u, 0x3790u, 0x3F6Cu, 0x3F88u,
        0x42E8u, 0u, 0x4388u, 0x4428u,
    }};
    for (std::size_t index = 0u; index < header.size(); ++index) {
        PutU32(bytes, index * sizeof(std::uint32_t), header[index]);
    }
    const std::array<std::uint8_t, 12u> chunk2Prefix = {{
        0x00u, 0x00u, 0x07u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};
    std::memcpy(bytes + kFixtureChunk2Offset, chunk2Prefix.data(), chunk2Prefix.size());
    PutU16(bytes, kFixtureSlotOffset, 0x1081u);
    for (std::size_t slot = 1u; slot < 8u; ++slot) {
        PutU16(bytes, kFixtureSlotOffset + slot * sizeof(std::uint16_t), 0x1092u);
    }
    const std::array<std::uint8_t, 16u> chunk3Prefix = {{
        0x00u, 0x01u, 0x01u, 0x04u, 0x07u, 0x07u, 0x08u, 0x00u,
        0x01u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    }};
    std::memcpy(bytes + kFixtureChunk3Offset, chunk3Prefix.data(), chunk3Prefix.size());
    const std::array<std::uint32_t, 8u> chunk3Offsets = {{
        0x70u, 0xE0u, 0x150u, 0x1C0u, 0x230u, 0x2B0u, 0x60u, 0x350u,
    }};
    for (std::size_t index = 0u; index < chunk3Offsets.size(); ++index) {
        PutU32(bytes, kFixtureChunk3Offset + 0x10u + index * sizeof(std::uint32_t),
               chunk3Offsets[index]);
    }
}

struct CarrierFixture {
    std::vector<std::uint8_t> bytes;
    CarrierFixture() : bytes(kFixtureSize) { FillCarrier(bytes.data()); }
};

CarrierView MakeCarrier(CarrierFixture& fixture) {
    CarrierView carrier{};
    carrier.bytes = fixture.bytes.data();
    carrier.size = fixture.bytes.size();
    carrier.encounterToken = 0x02050000u;
    carrier.encounterName = "dome02_00";
    carrier.encounterNameLength = 9u;
    carrier.access = CarrierAccess::ReadWrite;
    return carrier;
}

PendingRequest MakeRequest(std::uint64_t generation = 7u) {
    PendingRequest request{};
    request.enabled = true;
    request.armed = true;
    request.generation = generation;
    request.deadlineTick = 1000u;
    request.selection.activationCount = 1u;
    request.selection.activations[0] = MonsterChoice::Valefor;
    return request;
}

Observation MakeObservation(std::uint64_t generation = 7u) {
    Observation observation{};
    observation.generation = generation;
    observation.nowTick = 500u;
    return observation;
}

struct SharedSpy {
    CarrierFixture* carrier = nullptr;
    std::vector<int> order;
    int vanillaCalls = 0;
    int composerCalls = 0;
    int guardedAttempts = 0;
    int vanillaResult = 0;
    bool nestedAccepted = true;
    bool composerCallsOriginal = true;
    bool composerCallsOriginalTwice = false;
    bool throwFromNested = false;
    bool corruptCandidateAfterVanilla = false;
    bool candidateVisibleBefore = false;
    bool candidateVisibleVanilla = false;
    bool candidateVisibleAfter = false;
};

bool CandidateVisible(const SharedSpy& spy) {
    return ReadU16(spy.carrier->bytes.data(), kFixtureSlotOffset) == 0x114Eu &&
           ReadU16(spy.carrier->bytes.data(), kFixtureSlotOffset + 2u) == 0xFFFFu;
}

int CallVanilla(void* context) {
    SharedSpy& spy = *static_cast<SharedSpy*>(context);
    ++spy.vanillaCalls;
    spy.order.push_back(2);
    spy.candidateVisibleVanilla = CandidateVisible(spy);
    return spy.vanillaResult;
}

void SeymourComposer(void* context, std::uintptr_t, const Shared::OriginalIo& original) {
    SharedSpy& spy = *static_cast<SharedSpy*>(context);
    ++spy.composerCalls;
    spy.order.push_back(1);
    spy.candidateVisibleBefore = CandidateVisible(spy);
    if (spy.composerCallsOriginal) {
        (void)original.call(original.context);
        if (spy.composerCallsOriginalTwice) (void)original.call(original.context);
    }
    spy.guardedAttempts = spy.composerCallsOriginalTwice ? 2 : (spy.composerCallsOriginal ? 1 : 0);
    spy.order.push_back(3);
    spy.candidateVisibleAfter = CandidateVisible(spy);
    if (spy.corruptCandidateAfterVanilla) {
        spy.carrier->bytes[kFixtureSlotOffset] ^= 0x5Au;
    }
}

bool RunSharedBattle(void* context, int* resultOut) {
    SharedSpy& spy = *static_cast<SharedSpy*>(context);
    if (spy.throwFromNested) throw std::runtime_error("nested shared runtime failure");
    const Shared::OriginalIo original{&spy, &CallVanilla};
    const Shared::ReservedSeamIo reserved{};
    const Shared::ComposerIo composer{&spy, &SeymourComposer};
    const Shared::InitSceneRunResult result = Shared::RunInitScene(
        Shared::kBattleStateInitSceneReturnRva, original, reserved, composer);
    spy.guardedAttempts = static_cast<int>(result.originalCallAttempts);
    if (resultOut) *resultOut = result.originalResult;
    return spy.nestedAccepted;
}

bool MaterializeCarrier(void* context, CarrierView* carrierOut) noexcept {
    if (!context || !carrierOut) return false;
    *carrierOut = MakeCarrier(*static_cast<CarrierFixture*>(context));
    return true;
}

bool RejectCarrierLookup(void*, CarrierView*) noexcept { return false; }

Runtime::BattleCompositionOutcome RunComposition(
    SharedSpy& spy,
    PendingRequest* request,
    Runtime::MaterializeCarrierFn materialize = &MaterializeCarrier) {
    const Runtime::BattleCompositionIo io{
        {spy.carrier, materialize},
        {&spy, &RunSharedBattle},
    };
    return Runtime::RunBattleComposition(request, MakeObservation(), io);
}

void TestOuterTransactionContainsSeymourAndVanillaExactlyOnce() {
    CarrierFixture carrier;
    SharedSpy spy{};
    spy.carrier = &carrier;
    spy.vanillaResult = 0;
    spy.composerCallsOriginalTwice = true;
    PendingRequest request = MakeRequest();

    const Runtime::BattleCompositionOutcome outcome = RunComposition(spy, &request);

    Expect(outcome.transaction.result == TransactionResult::Restored,
           "the admitted outer transaction must restore after the nested shared runtime");
    Expect(outcome.originalResultAvailable && outcome.originalResult == 0,
           "game int result zero must be preserved independently from callback success");
    Expect(spy.composerCalls == 1 && spy.vanillaCalls == 1 && spy.guardedAttempts == 2,
           "Seymour may attempt original twice while guarded vanilla executes once");
    Expect(spy.order == std::vector<int>({1, 2, 3}),
           "candidate visibility order must be Seymour-before, vanilla, Seymour-after");
    Expect(spy.candidateVisibleBefore && spy.candidateVisibleVanilla && spy.candidateVisibleAfter,
           "the temporary candidate must contain the complete Seymour composer and vanilla call");
    Expect(ReadU16(carrier.bytes.data(), kFixtureSlotOffset) == 0x1081u &&
               ReadU16(carrier.bytes.data(), kFixtureSlotOffset + 2u) == 0x1092u,
           "the vanilla formation must be restored only after nested composition returns");
}

void TestOmittedOriginalFallsBackOnceInsidePatch() {
    CarrierFixture carrier;
    SharedSpy spy{};
    spy.carrier = &carrier;
    spy.vanillaResult = -1234567;
    spy.composerCallsOriginal = false;
    PendingRequest request = MakeRequest();

    const Runtime::BattleCompositionOutcome outcome = RunComposition(spy, &request);
    Expect(outcome.transaction.result == TransactionResult::Restored && spy.vanillaCalls == 1,
           "an omitted composer original must use the guarded fallback exactly once");
    Expect(outcome.originalResultAvailable && outcome.originalResult == -1234567,
           "the fallback vanilla int result must survive the bool transaction adapter");
    Expect(spy.candidateVisibleVanilla,
           "the guarded fallback original must still execute inside the candidate patch");
}

void TestPassthroughRejectFalseThrowAndConflictPaths() {
    {
        CarrierFixture carrier;
        SharedSpy spy{};
        spy.carrier = &carrier;
        spy.vanillaResult = 77;
        PendingRequest request = MakeRequest();
        const Runtime::BattleCompositionIo io{
            {&carrier, &RejectCarrierLookup}, {&spy, &RunSharedBattle}};
        const Runtime::BattleCompositionOutcome outcome =
            Runtime::RunBattleComposition(&request, MakeObservation(), io);
        Expect(outcome.transaction.result == TransactionResult::InvalidCarrierPointer &&
                   spy.vanillaCalls == 1 && outcome.originalResult == 77,
               "a probe rejection must pass through nested vanilla once and preserve its int result");
        Expect(!spy.candidateVisibleVanilla,
               "a rejected carrier must never expose candidate bytes to nested vanilla");
    }
    {
        CarrierFixture carrier;
        SharedSpy spy{};
        spy.carrier = &carrier;
        spy.nestedAccepted = false;
        PendingRequest request = MakeRequest();
        const Runtime::BattleCompositionOutcome outcome = RunComposition(spy, &request);
        Expect(outcome.transaction.result == TransactionResult::OriginalRejected &&
                   outcome.transaction.restored && spy.vanillaCalls == 1,
               "a false nested adapter result must restore and report OriginalRejected");
    }
    {
        CarrierFixture carrier;
        SharedSpy spy{};
        spy.carrier = &carrier;
        spy.throwFromNested = true;
        PendingRequest request = MakeRequest();
        const Runtime::BattleCompositionOutcome outcome = RunComposition(spy, &request);
        Expect(outcome.transaction.result == TransactionResult::OriginalThrew &&
                   outcome.transaction.restored && !outcome.originalResultAvailable,
               "a throwing nested adapter must restore and report no fabricated int result");
    }
    {
        CarrierFixture carrier;
        SharedSpy spy{};
        spy.carrier = &carrier;
        spy.corruptCandidateAfterVanilla = true;
        PendingRequest request = MakeRequest();
        const Runtime::BattleCompositionOutcome outcome = RunComposition(spy, &request);
        Expect(outcome.transaction.result == TransactionResult::RestoreConflict &&
                   !outcome.transaction.restored,
               "foreign candidate drift must be preserved as a restore conflict");
    }
    {
        CarrierFixture carrier;
        SharedSpy spy{};
        spy.carrier = &carrier;
        PendingRequest request = MakeRequest(8u);
        const Runtime::BattleCompositionOutcome outcome = RunComposition(spy, &request);
        Expect(outcome.transaction.result == TransactionResult::GenerationMismatch &&
                   spy.vanillaCalls == 1 && !spy.candidateVisibleVanilla,
               "a generation rejection must remain an untouched exact-once passthrough");
    }
}

void TestSelectionEditingAndPreviewRemainClosedAndStable() {
    SelectionInput selection{};
    for (int count = 1; count <= 8; ++count) {
        const Runtime::SelectionEditResult added =
            Runtime::TryAddChoice(selection, MonsterChoice::Valefor);
        Expect(added.accepted && added.selection.activationCount == count &&
                   added.preview.result == SelectionResult::Ready &&
                   added.preview.expanded.monsterCount == count,
               "one-slot symbolic choices must fill the bounded preview from one through eight");
        selection = added.selection;
    }
    Expect(Runtime::ClassifySelection(selection) == Runtime::StatusCode::Full,
           "an exact eight-slot preview must publish FULL");
    const SelectionInput fullBefore = selection;
    const Runtime::SelectionEditResult rejected =
        Runtime::TryAddChoice(selection, MonsterChoice::Ifrit);
    Expect(!rejected.accepted &&
               std::memcmp(&rejected.selection, &fullBefore, sizeof(fullBefore)) == 0,
           "a rejected add must preserve the prior valid symbolic selection byte-for-byte");

    selection = Runtime::ClearSelection().selection;
    selection = Runtime::TryAddChoice(selection, MonsterChoice::Magus).selection;
    selection = Runtime::TryAddChoice(selection, MonsterChoice::Valefor).selection;
    selection = Runtime::TryAddChoice(selection, MonsterChoice::Magus).selection;
    const SelectionOutcome preview = BuildSelection(selection);
    const std::array<std::uint16_t, 8u> expected = {{
        0x1155u, 0x1156u, 0x1157u, 0x1155u,
        0x1156u, 0x1157u, 0x114Eu, 0xFFFFu,
    }};
    Expect(preview.result == SelectionResult::Ready && preview.expanded.monsterIds == expected,
           "repeated Magus must expand only through BuildSelection in stable first-activation order");
    const Runtime::SelectionEditResult removed = Runtime::RemoveLastChoice(selection);
    Expect(removed.accepted && removed.selection.activationCount == 2u &&
               removed.preview.expanded.monsterCount == 4u,
           "Remove Last must rebuild a valid bounded preview without retaining expanded IDs");
    const Runtime::SelectionEditResult cleared = Runtime::ClearSelection();
    Expect(cleared.selection.activationCount == 0u &&
               cleared.status == Runtime::StatusCode::Empty,
           "Clear must return the symbolic draft to EMPTY");
}

void TestRequestOneShotDeadlineCancellationAndStickyConflict() {
    Runtime::RequestState state;
    SelectionInput selection{};
    selection.activationCount = 1u;
    selection.activations[0] = MonsterChoice::Bahamut;

    Expect(Runtime::kRequestTtlMs == 30000u,
           "the request deadline must allow the reviewed thirty-second transition window");

    state.Start(false, false);
    Expect(!state.IsReady() && !state.Arm(selection, 100u) &&
               state.Status().code == Runtime::StatusCode::Unavailable,
           "an unsupported profile must remain inert and unavailable");
    state.Start(true, true);
    Expect(!state.IsReady() && !state.Arm(selection, 100u),
           "validation-only must never open request admission");
    state.Start(true, false);
    Expect(state.IsReady() && state.Arm(selection, 100u),
           "an exact ready runtime must arm only after an explicit launch call");
    const std::uint64_t generation1 = state.Status().generation;
    Expect(generation1 != 0u && state.Status().code == Runtime::StatusCode::Queued,
           "the first queued request must carry a nonzero generation");
    const Runtime::PendingClaim claim1 = state.Claim(200u);
    const Runtime::PendingClaim replay = state.Claim(201u);
    Expect(claim1.claimed && claim1.request.generation == generation1 &&
               claim1.observation.generation == generation1 && !replay.claimed,
           "claim must copy and clear the armed request one-shot before validation");

    Expect(state.Arm(selection, 300u), "a consumed request may be followed by a new generation");
    const std::uint64_t generation2 = state.Status().generation;
    Expect(generation2 > generation1,
           "request generations must be strictly monotonic and nonzero");
    state.Cancel(Runtime::CancelReason::Back);
    Expect(!state.Claim(301u).claimed,
           "Back must cancel a queued request before a battle can claim it");

    Expect(state.Arm(selection, 400u), "a fresh request must arm before timeout coverage");
    state.Tick(400u + Runtime::kRequestTtlMs);
    Expect(state.Status().code == Runtime::StatusCode::Expired &&
               !state.Claim(400u + Runtime::kRequestTtlMs).claimed,
           "the monotonic deadline tick must cancel an unconsumed request at timeout");

    Expect(state.Arm(selection, 1000u), "a fresh request must arm before restore-conflict coverage");
    const std::uint64_t conflictGeneration = state.Status().generation;
    TransactionOutcome conflict{};
    conflict.result = TransactionResult::RestoreConflict;
    conflict.patchApplied = true;
    state.PublishTransaction(conflictGeneration, conflict);
    Expect(state.Status().code == Runtime::StatusCode::RestoreConflict &&
               state.RestoreConflictLatched() && !state.Arm(selection, 1100u),
           "a restore conflict must block every new arm for the remaining process session");
    state.ResetAfterDrain();
    Expect(state.RestoreConflictLatched() && !state.Arm(selection, 1200u),
           "teardown reset must not erase the session-sticky restore-conflict latch");
}

void TestStatusVocabularyAndCancellationLifecycle() {
    const std::array<const char*, 9u> expected = {{
        "EMPTY", "FULL", "READY", "QUEUED", "CONSUMED", "FAILED",
        "RESTORE CONFLICT", "EXPIRED", "UNAVAILABLE",
    }};
    for (std::size_t index = 0u; index < expected.size(); ++index) {
        Expect(std::strcmp(Runtime::StatusName(
                   static_cast<Runtime::StatusCode>(index)), expected[index]) == 0,
               "every Ultra status must use its exact bounded English UI label");
    }

    SelectionInput selection{};
    selection.activationCount = 1u;
    selection.activations[0] = MonsterChoice::Shiva;
    for (const Runtime::CancelReason reason : {
             Runtime::CancelReason::Back, Runtime::CancelReason::Cancel,
             Runtime::CancelReason::Close, Runtime::CancelReason::FocusLoss,
             Runtime::CancelReason::NewGeneration}) {
        Runtime::RequestState state;
        state.Start(true, false);
        Expect(state.Arm(selection, 10u), "a cancellation fixture must arm explicitly");
        state.Cancel(reason);
        Expect(state.Status().code == Runtime::StatusCode::Empty &&
                   !state.Claim(11u).claimed,
               "Back, cancel, close, focus loss, and replacement must clear the one-shot request");
    }

    Runtime::RequestState state;
    state.Start(true, false);
    Expect(state.Arm(selection, 20u), "a stop fixture must arm explicitly");
    state.Stop();
    Expect(!state.IsReady() && state.Status().code == Runtime::StatusCode::Unavailable &&
               !state.Claim(21u).claimed,
           "stop must close readiness, clear the request, and publish UNAVAILABLE");
    state.Start(true, false);
    Expect(state.Arm(selection, 30u), "a queue-failure fixture must arm explicitly");
    state.Cancel(Runtime::CancelReason::QueueFailure);
    Expect(state.Status().code == Runtime::StatusCode::Failed,
           "queue failure must clear the request and publish FAILED");
}

struct LaunchSpy {
    std::vector<int> order;
    Runtime::CarrierQueueResult queue{};
    bool armResult = true;
    int cancels = 0;
};

Runtime::CarrierQueueResult QueueCarrier(void* context) noexcept {
    LaunchSpy& spy = *static_cast<LaunchSpy*>(context);
    spy.order.push_back(1);
    return spy.queue;
}

bool ArmAfterQueue(void* context, const SelectionInput&, std::uint64_t) noexcept {
    LaunchSpy& spy = *static_cast<LaunchSpy*>(context);
    spy.order.push_back(2);
    return spy.armResult;
}

void CancelLaunch(void* context, Runtime::CancelReason) noexcept {
    ++static_cast<LaunchSpy*>(context)->cancels;
}

void TestQueueMustSucceedExactlyBeforeArm() {
    SelectionInput selection{};
    selection.activationCount = 1u;
    selection.activations[0] = MonsterChoice::Anima;
    Runtime::LaunchIo io{};

    LaunchSpy success{};
    success.queue = {true, -1, true};
    io = {&success, &QueueCarrier, &ArmAfterQueue, &CancelLaunch};
    const Runtime::LaunchOutcome launched = Runtime::QueueThenArm(selection, 55u, io);
    Expect(launched.code == Runtime::LaunchCode::QueuedAndArmed &&
               success.order == std::vector<int>({1, 2}) && success.cancels == 0,
           "the exact carrier queue must complete before CustomMix arms");

    for (const Runtime::CarrierQueueResult rejected : {
             Runtime::CarrierQueueResult{false, -1, true},
             Runtime::CarrierQueueResult{true, 0, true},
             Runtime::CarrierQueueResult{true, -1, false}}) {
        LaunchSpy failure{};
        failure.queue = rejected;
        io = {&failure, &QueueCarrier, &ArmAfterQueue, &CancelLaunch};
        const Runtime::LaunchOutcome outcome = Runtime::QueueThenArm(selection, 56u, io);
        Expect(outcome.code == Runtime::LaunchCode::QueueFailed &&
                   failure.order == std::vector<int>({1}) && failure.cancels == 1,
               "ok, ret=-1, and queueArmed must all be true before any arm attempt");
    }

    LaunchSpy armFailure{};
    armFailure.queue = {true, -1, true};
    armFailure.armResult = false;
    io = {&armFailure, &QueueCarrier, &ArmAfterQueue, &CancelLaunch};
    const Runtime::LaunchOutcome failedArm = Runtime::QueueThenArm(selection, 57u, io);
    Expect(failedArm.code == Runtime::LaunchCode::ArmFailed && failedArm.carrierQueued &&
               armFailure.order == std::vector<int>({1, 2}) && armFailure.cancels == 1,
           "an arm failure must leave the already queued carrier vanilla and publish failure");
}

struct EditorLaunchStateSpy {
    Runtime::RequestState state{};
    Runtime::CarrierQueueResult queue{};
    bool allowArm = true;
    int armCalls = 0;
    int cancels = 0;
};

Runtime::CarrierQueueResult QueueEditorCarrier(void* context) noexcept {
    return static_cast<EditorLaunchStateSpy*>(context)->queue;
}

bool ArmEditorRequest(
    void* context,
    const SelectionInput& selection,
    std::uint64_t nowTick) noexcept {
    EditorLaunchStateSpy& spy = *static_cast<EditorLaunchStateSpy*>(context);
    ++spy.armCalls;
    return spy.allowArm && spy.state.Arm(selection, nowTick);
}

void CancelEditorRequest(void* context, Runtime::CancelReason reason) noexcept {
    EditorLaunchStateSpy& spy = *static_cast<EditorLaunchStateSpy*>(context);
    ++spy.cancels;
    spy.state.Cancel(reason);
}

void TestEditorLaunchDispositionOwnsSelectionAndRequestLifecycle() {
    SelectionInput selection{};
    selection.activationCount = 2u;
    selection.activations[0] = MonsterChoice::Anima;
    selection.activations[1] = MonsterChoice::Valefor;

    {
        EditorLaunchStateSpy spy{};
        spy.state.Start(true, false);
        spy.queue = {false, -1, true};
        const Runtime::LaunchIo io{
            &spy, &QueueEditorCarrier, &ArmEditorRequest, &CancelEditorRequest};
        const Runtime::EditorLaunchOutcome outcome =
            Runtime::LaunchEditorSelection(selection, 100u, io);
        Expect(outcome.disposition ==
                   Runtime::EditorLaunchDisposition::ReopenPreservingSelection &&
                   std::memcmp(&outcome.selection, &selection, sizeof(selection)) == 0 &&
                   outcome.launch.code == Runtime::LaunchCode::QueueFailed &&
                   spy.armCalls == 0 && spy.cancels == 1 &&
                   spy.state.Status().code == Runtime::StatusCode::Failed,
               "queue failure must preserve the exact symbolic draft and reopen the editor");
    }

    {
        EditorLaunchStateSpy spy{};
        spy.state.Start(true, false);
        spy.queue = {true, -1, true};
        spy.allowArm = false;
        const Runtime::LaunchIo io{
            &spy, &QueueEditorCarrier, &ArmEditorRequest, &CancelEditorRequest};
        const Runtime::EditorLaunchOutcome outcome =
            Runtime::LaunchEditorSelection(selection, 200u, io);
        Expect(outcome.disposition ==
                   Runtime::EditorLaunchDisposition::CloseWithVanillaCarrier &&
                   outcome.selection.activationCount == 0u &&
                   outcome.launch.carrierQueued && !outcome.launch.requestArmed &&
                   spy.armCalls == 1 && spy.cancels == 1 &&
                   spy.state.Status().code == Runtime::StatusCode::Failed,
               "arm failure must close without reopening while the queued carrier stays vanilla");
    }

    {
        EditorLaunchStateSpy spy{};
        spy.state.Start(true, false);
        spy.queue = {true, -1, true};
        const Runtime::LaunchIo io{
            &spy, &QueueEditorCarrier, &ArmEditorRequest, &CancelEditorRequest};
        const Runtime::EditorLaunchOutcome outcome =
            Runtime::LaunchEditorSelection(selection, 300u, io);
        const Runtime::PendingClaim claimed = spy.state.Claim(301u);
        Expect(outcome.disposition ==
                   Runtime::EditorLaunchDisposition::CloseWithArmedRequest &&
                   outcome.selection.activationCount == 0u &&
                   outcome.launch.code == Runtime::LaunchCode::QueuedAndArmed &&
                   spy.armCalls == 1 && spy.cancels == 0 && claimed.claimed,
               "successful Launch must close without cancelling the newly armed one-shot request");
    }
}

void TestFixedProductionProbeUsesMappedCommittedWritableCarrier() {
    const std::size_t imageBytes =
        static_cast<std::size_t>(WindowsAdapter::kCarrierNameRva) +
        WindowsAdapter::kCarrierNameReadWidth;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, imageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto* carrier = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kFixtureSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Expect(image != nullptr && carrier != nullptr,
           "the isolated x86 probe fixture must allocate image globals and carrier regions");
    if (!image || !carrier) {
        if (image) VirtualFree(image, 0, MEM_RELEASE);
        if (carrier) VirtualFree(carrier, 0, MEM_RELEASE);
        return;
    }

    FillCarrier(carrier);
    PutU16(image, WindowsAdapter::kCarrierSizeRva, 0x4428u);
    PutU32(image, WindowsAdapter::kCarrierPointerRva,
           static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(carrier)));
    std::memcpy(image + WindowsAdapter::kCarrierNameRva, "dome02_00", 10u);

    const Runtime::Testing::ProbeEvidence ready = Runtime::Testing::ProbeCarrier(
        reinterpret_cast<std::uintptr_t>(image));
    Expect(ready.virtualQueryCalls == 1u &&
               ready.outcome.code == WindowsAdapter::AdapterCode::Ready &&
               ready.outcome.carrier.bytes == carrier &&
               ready.outcome.carrier.size == 0x4428u,
           "the production probe must use one VirtualQuery and map the full exact writable span");

    DWORD oldProtection = 0;
    const BOOL protectedOk = VirtualProtect(
        carrier, kFixtureSize, PAGE_READONLY, &oldProtection);
    Expect(protectedOk != FALSE, "the isolated carrier must become read-only for rejection coverage");
    if (protectedOk) {
        const Runtime::Testing::ProbeEvidence readOnly = Runtime::Testing::ProbeCarrier(
            reinterpret_cast<std::uintptr_t>(image));
        Expect(readOnly.virtualQueryCalls == 1u &&
                   readOnly.outcome.code == WindowsAdapter::AdapterCode::RegionNotWritable,
               "the fixed production probe must reject a read-only full carrier region");
        DWORD ignored = 0;
        VirtualProtect(carrier, kFixtureSize, oldProtection, &ignored);
    }

    VirtualFree(carrier, 0, MEM_RELEASE);
    VirtualFree(image, 0, MEM_RELEASE);
}

void TestUltraListUsesSharedMouseScrollAndHubBackSemantics() {
    using namespace FfxHooks::F7Ui;
    int selection = 0;
    int firstVisible = 0;
    ScrollList(5, 12, 8, selection, firstVisible);
    Expect(selection == 5 && firstVisible == 0,
           "the twelve-row Ultra list must scroll within the shared eight-row page");
    ScrollList(5, 12, 8, selection, firstVisible);
    Expect(selection == 10 && firstVisible == 3,
           "shared scrolling must keep Ultra actions and Back reachable");

    const ListGeometry geometry{100.0f, 50.0f, 300.0f, 30.0f, 25.0f, 8};
    const Hit hit = HitTestRows(150.0f, 50.0f + 7.0f * 30.0f + 10.0f,
                                geometry, 9, 17);
    Expect(hit.kind == HitKind::Row && hit.index == 16,
           "mouse hit testing must reach the Ultra Back row after scrolling");
    const PointerDecision click{true, true, true};
    const ListPointerResolution resolved = ResolveListPointerInput(4, hit, click);
    Expect(resolved.selection == 16 && resolved.confirm && resolved.ownsDirectionalFrame,
           "an admitted Ultra mouse click must own selection, confirm, and directional precedence");

    ModalState modal{};
    modal.open = true;
    modal.cursorOwned = true;
    modal.nativeGateOwned = true;
    modal.forceGateOwned = true;
    const CloseEffects back = CloseModal(
        modal, CloseSource::BackRow, CloseDestination::Hub);
    Expect(back.changed && back.closeMenu && back.returnToHub,
           "Ultra Back must return through shared hub cleanup rather than close to a black screen");
}

void TestPositionDraftRequestAdmission() {
    namespace P=FfxHooks::ArenaPositions;
    SelectionInput selection{};
    selection.activationCount=3;
    selection.positions=P::Generate(3);
    Expect(P::Move(&selection.positions,1,4,0), "manual draft accepts a bounded per-slot move");
    auto added=Runtime::TryAddChoice(selection,MonsterChoice::Ifrit);
    Expect(added.accepted && added.selection.positions.automatic && added.selection.positions.count==4 &&
               P::Validate(added.selection.positions,4)==P::Issue::None,
           "changing the expanded selection rebuilds positions instead of reusing edits for another slot");
    auto removed=Runtime::RemoveLastChoice(added.selection);
    Expect(removed.accepted && removed.selection.positions.automatic && removed.selection.positions.count==3,
           "removing a boss regenerates the correct position count");
    Runtime::RequestState state;state.Start(true,false);
    Expect(state.Arm(selection,100), "a valid manual layout can arm with its symbolic selection");
    selection.positions.points[1].x=150.0f;
    const auto claim=state.Claim(101);
    Expect(claim.claimed && claim.request.selection.positions.points[1].x==4.0f,
           "the queued layout is immutable after the caller edits its draft");
    LaunchSpy spy{};spy.queue={true,-1,true};spy.armResult=true;
    const Runtime::LaunchIo io{&spy,&QueueCarrier,&ArmAfterQueue,&CancelLaunch};
    selection.positions.count=8;
    const auto outcome=Runtime::QueueThenArm(selection,102,io);
    Expect(outcome.code==Runtime::LaunchCode::InvalidSelection && spy.order.empty(),
           "a stale position count is rejected before the carrier queue side effect");
    Expect(!state.Arm(selection,102) && state.Status().transactionResult==TransactionResult::InvalidPositions,
           "the request consumer independently rejects malformed position payloads");
}

}  // namespace

int main() {
    TestPositionDraftRequestAdmission();
    TestOuterTransactionContainsSeymourAndVanillaExactlyOnce();
    TestOmittedOriginalFallsBackOnceInsidePatch();
    TestPassthroughRejectFalseThrowAndConflictPaths();
    TestSelectionEditingAndPreviewRemainClosedAndStable();
    TestRequestOneShotDeadlineCancellationAndStickyConflict();
    TestStatusVocabularyAndCancellationLifecycle();
    TestQueueMustSucceedExactlyBeforeArm();
    TestEditorLaunchDispositionOwnsSelectionAndRequestLifecycle();
    TestFixedProductionProbeUsesMappedCommittedWritableCarrier();
    TestUltraListUsesSharedMouseScrollAndHubBackSemantics();

    std::printf("CustomMix production runtime RT0/RT1: %d/%d passed\n",
                g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
