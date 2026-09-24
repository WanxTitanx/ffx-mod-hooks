#include "../hooks/MonsterAiObserverCore.h"
#include "../hooks/F7AiSwap.h"
#include "../hooks/FieldScoutAdmissionCore.h"
#include "../hooks/MinHookBatchCoordinator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace FfxHooks {

// These test-only seams must delegate to the same portable install/remove adapters used by
// production F7AiSwap_Install/F7AiSwap_Remove. Declaring them here makes the first TDD run fail
// until the adapter deliberately exposes the contract under FFXHOOKS_TESTING.
MonsterAiObserver::InstallResult F7AiSwap_TestInstallAdapter(
    const MonsterAiObserver::DetourIo&, MinHookBatch::Coordinator*,
    const MinHookBatch::BatchIo&, const MonsterAiObserver::DrainIo&,
    MonsterAiObserver::LifecycleState*, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t,
    MonsterAiObserver::DetourOwner*);
MonsterAiObserver::TeardownResult F7AiSwap_TestRemoveAdapter(
    const MonsterAiObserver::DetourIo&, MinHookBatch::Coordinator*,
    const MinHookBatch::BatchIo&, const MonsterAiObserver::DrainIo&,
    MonsterAiObserver::LifecycleState*, MonsterAiObserver::DetourOwner*);
F7AiObserverStatus F7AiSwap_TestResolveStatus(F7AiObserverStatus, bool requested);
F7AiObserverStatus F7AiSwap_TestResolveSetupFailureStatus(
    F7AiObserverStatus, bool requested);
F7AiObserverStatus F7AiSwap_TestResolveInstallFailureStatus(
    MonsterAiObserver::InstallResult, const MonsterAiObserver::DetourOwner&);
bool F7AiSwap_TestBuildLoadedSignature(
    bool registration, uintptr_t moduleBase, uint8_t* expectedOut, size_t expectedLength);
bool F7AiSwap_TestValidateLoadedSignature(
    bool registration, uintptr_t moduleBase, const uint8_t* observed, size_t observedLength);
bool F7AiSwap_TestBuildDispatchLoadedSignature(
    uintptr_t moduleBase, uint8_t* expectedOut, size_t expectedLength);
bool F7AiSwap_TestValidateDispatchLoadedSignature(
    uintptr_t moduleBase, const uint8_t* observed, size_t observedLength);
bool F7AiSwap_TestValidateDispatchCallerReturns(
    const uint32_t* callerReturnRvas, size_t count);
uint32_t F7AiSwap_TestNormalizeCallerReturn(uintptr_t moduleBase, uintptr_t liveReturn);

} // namespace FfxHooks

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

void TestRelocatedLoadedSignatures() {
    constexpr uintptr_t preferredBase = 0x00400000u;
    constexpr uintptr_t relocatedBase = 0x002F0000u;
    constexpr std::array<uint8_t, 24> registrationPreferred = {
        0x55u, 0x8Bu, 0xECu, 0x83u, 0xECu, 0x0Cu, 0xA1u, 0xD8u,
        0x13u, 0xC6u, 0x00u, 0x33u, 0xC5u, 0x89u, 0x45u, 0xFCu,
        0x53u, 0x56u, 0x57u, 0xE8u, 0xF8u, 0x89u, 0x01u, 0x00u,
    };
    constexpr std::array<uint8_t, 24> registrationRelocated = {
        0x55u, 0x8Bu, 0xECu, 0x83u, 0xECu, 0x0Cu, 0xA1u, 0xD8u,
        0x13u, 0xB5u, 0x00u, 0x33u, 0xC5u, 0x89u, 0x45u, 0xFCu,
        0x53u, 0x56u, 0x57u, 0xE8u, 0xF8u, 0x89u, 0x01u, 0x00u,
    };
    constexpr std::array<uint8_t, 18> cleanupPreferred = {
        0x80u, 0x3Du, 0xE0u, 0xA8u, 0x12u, 0x01u, 0x00u, 0x0Fu,
        0x84u, 0x91u, 0x00u, 0x00u, 0x00u, 0xE8u, 0x6Eu, 0xBBu,
        0xFEu, 0xFFu,
    };
    constexpr std::array<uint8_t, 18> cleanupRelocated = {
        0x80u, 0x3Du, 0xE0u, 0xA8u, 0x01u, 0x01u, 0x00u, 0x0Fu,
        0x84u, 0x91u, 0x00u, 0x00u, 0x00u, 0xE8u, 0x6Eu, 0xBBu,
        0xFEu, 0xFFu,
    };

    std::array<uint8_t, registrationPreferred.size()> registration{};
    Expect(FfxHooks::F7AiSwap_TestBuildLoadedSignature(
               true, preferredBase, registration.data(), registration.size()),
           "registration signature must build at the preferred image base");
    Expect(registration == registrationPreferred,
           "preferred-base registration signature must retain its evidenced abs32 operand");
    Expect(FfxHooks::F7AiSwap_TestBuildLoadedSignature(
               true, relocatedBase, registration.data(), registration.size()),
           "registration signature must build at loaded base 0x002F0000");
    Expect(registration == registrationRelocated,
           "registration abs32 must resolve to loaded base plus RVA 0x008613D8");
    Expect(std::equal(registration.begin() + 19, registration.end(),
                      registrationPreferred.begin() + 19),
           "registration rel32 call bytes must remain unchanged under ASLR");
    Expect(FfxHooks::F7AiSwap_TestValidateLoadedSignature(
               true, relocatedBase, registration.data(), registration.size()),
           "exact relocated registration prefix must validate");
    for (size_t index = 0u; index < registration.size(); ++index) {
        auto mutated = registration;
        mutated[index] ^= 0x01u;
        Expect(!FfxHooks::F7AiSwap_TestValidateLoadedSignature(
                   true, relocatedBase, mutated.data(), mutated.size()),
               "every registration prefix byte must be exact");
    }
    Expect(!FfxHooks::F7AiSwap_TestValidateLoadedSignature(
               true, relocatedBase, registrationPreferred.data(), registrationPreferred.size()),
           "registration must reject the preferred-base abs32 at a relocated base");
    const uintptr_t registrationOverflowBase =
        static_cast<uintptr_t>((std::numeric_limits<uint32_t>::max)()) - 0x008613D8u + 1u;
    Expect(!FfxHooks::F7AiSwap_TestBuildLoadedSignature(
               true, registrationOverflowBase, registration.data(), registration.size()),
           "registration must reject an abs32 target overflow");

    std::array<uint8_t, cleanupPreferred.size()> cleanup{};
    Expect(FfxHooks::F7AiSwap_TestBuildLoadedSignature(
               false, preferredBase, cleanup.data(), cleanup.size()),
           "cleanup signature must build at the preferred image base");
    Expect(cleanup == cleanupPreferred,
           "preferred-base cleanup signature must retain its evidenced abs32 operand");
    Expect(FfxHooks::F7AiSwap_TestBuildLoadedSignature(
               false, relocatedBase, cleanup.data(), cleanup.size()),
           "cleanup signature must build at loaded base 0x002F0000");
    Expect(cleanup == cleanupRelocated,
           "cleanup abs32 must resolve to loaded base plus RVA 0x00D2A8E0");
    Expect(std::equal(cleanup.begin() + 13, cleanup.end(), cleanupPreferred.begin() + 13),
           "cleanup rel32 call bytes must remain unchanged under ASLR");
    Expect(FfxHooks::F7AiSwap_TestValidateLoadedSignature(
               false, relocatedBase, cleanup.data(), cleanup.size()),
           "exact relocated cleanup prefix must validate");
    for (size_t index = 0u; index < cleanup.size(); ++index) {
        auto mutated = cleanup;
        mutated[index] ^= 0x01u;
        Expect(!FfxHooks::F7AiSwap_TestValidateLoadedSignature(
                   false, relocatedBase, mutated.data(), mutated.size()),
               "every cleanup prefix byte must be exact");
    }
    Expect(!FfxHooks::F7AiSwap_TestValidateLoadedSignature(
               false, relocatedBase, cleanupPreferred.data(), cleanupPreferred.size()),
           "cleanup must reject the preferred-base abs32 at a relocated base");
    const uintptr_t cleanupOverflowBase =
        static_cast<uintptr_t>((std::numeric_limits<uint32_t>::max)()) - 0x00D2A8E0u + 1u;
    Expect(!FfxHooks::F7AiSwap_TestBuildLoadedSignature(
               false, cleanupOverflowBase, cleanup.data(), cleanup.size()),
           "cleanup must reject an abs32 target overflow");

    constexpr std::array<uint8_t, 66> dispatchPreferred = {
        0x55u, 0x8Bu, 0xECu, 0x83u, 0xECu, 0x10u, 0x83u, 0x3Du,
        0x58u, 0x6Au, 0x13u, 0x01u, 0xFFu, 0x0Fu, 0x84u, 0x20u,
        0x01u, 0x00u, 0x00u, 0x0Fu, 0xB6u, 0x05u, 0x68u, 0x6Au,
        0x13u, 0x01u, 0x8Bu, 0x55u, 0x08u, 0x3Bu, 0xD0u, 0x0Fu,
        0x85u, 0x0Eu, 0x01u, 0x00u, 0x00u, 0xA0u, 0x6Bu, 0x6Au,
        0x13u, 0x01u, 0x3Cu, 0x04u, 0x0Fu, 0x83u, 0x01u, 0x01u,
        0x00u, 0x00u, 0x0Fu, 0xB6u, 0xC8u, 0x66u, 0x8Bu, 0x45u,
        0x0Cu, 0xC1u, 0xE1u, 0x04u, 0x81u, 0xC1u, 0x70u, 0x6Au,
        0x13u, 0x01u,
    };
    auto dispatchRelocated = dispatchPreferred;
    for (const auto& patch : std::array<std::pair<size_t, uint32_t>, 4>{{
             {0x08u, 0x01026A58u},
             {0x16u, 0x01026A68u},
             {0x26u, 0x01026A6Bu},
             {0x3Eu, 0x01026A70u},
         }}) {
        dispatchRelocated[patch.first + 0u] = static_cast<uint8_t>(patch.second & 0xFFu);
        dispatchRelocated[patch.first + 1u] =
            static_cast<uint8_t>((patch.second >> 8u) & 0xFFu);
        dispatchRelocated[patch.first + 2u] =
            static_cast<uint8_t>((patch.second >> 16u) & 0xFFu);
        dispatchRelocated[patch.first + 3u] =
            static_cast<uint8_t>((patch.second >> 24u) & 0xFFu);
    }
    std::array<uint8_t, dispatchPreferred.size()> dispatch{};
    Expect(FfxHooks::F7AiSwap_TestBuildDispatchLoadedSignature(
               preferredBase, dispatch.data(), dispatch.size()) &&
               dispatch == dispatchPreferred,
           "dispatcher signature must reproduce all 66 preferred-base bytes exactly");
    Expect(FfxHooks::F7AiSwap_TestBuildDispatchLoadedSignature(
               relocatedBase, dispatch.data(), dispatch.size()) &&
               dispatch == dispatchRelocated,
           "dispatcher signature must relocate exactly four evidenced HIGHLOW operands");
    Expect(FfxHooks::F7AiSwap_TestValidateDispatchLoadedSignature(
               relocatedBase, dispatch.data(), dispatch.size()),
           "exact ASLR-adjusted 66-byte dispatcher signature must validate");
    for (size_t index = 0u; index < dispatch.size(); ++index) {
        auto mutated = dispatch;
        mutated[index] ^= 0x01u;
        Expect(!FfxHooks::F7AiSwap_TestValidateDispatchLoadedSignature(
                   relocatedBase, mutated.data(), mutated.size()),
               "every dispatcher prefix byte, including relocated operands, must be exact");
    }
    const uintptr_t dispatchOverflowBase =
        static_cast<uintptr_t>((std::numeric_limits<uint32_t>::max)()) -
        0x00D36A70u + 1u;
    Expect(!FfxHooks::F7AiSwap_TestBuildDispatchLoadedSignature(
               dispatchOverflowBase, dispatch.data(), dispatch.size()),
           "dispatcher signature builder must reject any HIGHLOW target overflow");

    constexpr std::array<uint32_t, 3> exactReturns = {
        0x003A454Eu, 0x003A4A60u, 0x003A4B8Cu};
    constexpr std::array<uint32_t, 3> reorderedReturns = {
        0x003A4B8Cu, 0x003A454Eu, 0x003A4A60u};
    constexpr std::array<uint32_t, 3> duplicateReturns = {
        0x003A454Eu, 0x003A454Eu, 0x003A4B8Cu};
    constexpr std::array<uint32_t, 4> extraReturns = {
        0x003A454Eu, 0x003A4A60u, 0x003A4B8Cu, 0x003A4B8Du};
    Expect(FfxHooks::F7AiSwap_TestValidateDispatchCallerReturns(
               exactReturns.data(), exactReturns.size()) &&
               FfxHooks::F7AiSwap_TestValidateDispatchCallerReturns(
                   reorderedReturns.data(), reorderedReturns.size()),
           "dispatcher validation must admit exactly the three evidenced return RVAs in any scan order");
    Expect(!FfxHooks::F7AiSwap_TestValidateDispatchCallerReturns(
               duplicateReturns.data(), duplicateReturns.size()) &&
               !FfxHooks::F7AiSwap_TestValidateDispatchCallerReturns(
                   extraReturns.data(), extraReturns.size()) &&
               !FfxHooks::F7AiSwap_TestValidateDispatchCallerReturns(nullptr, 0u),
           "missing, duplicate, extra, or absent dispatcher callers must fail closed");
    Expect(FfxHooks::F7AiSwap_TestNormalizeCallerReturn(
               relocatedBase, relocatedBase + 0x003A454Eu) == 0x007A454Eu &&
               FfxHooks::F7AiSwap_TestNormalizeCallerReturn(
                   relocatedBase, relocatedBase + 0x003A4A60u) == 0x007A4A60u &&
               FfxHooks::F7AiSwap_TestNormalizeCallerReturn(
                   relocatedBase, relocatedBase + 0x003A4B8Cu) == 0x007A4B8Cu,
           "ASLR normalization must recover all three preferred-image caller returns");
    Expect(FfxHooks::F7AiSwap_TestNormalizeCallerReturn(relocatedBase, relocatedBase - 1u) == 0u &&
               FfxHooks::F7AiSwap_TestNormalizeCallerReturn(
                   relocatedBase, relocatedBase + 0x0237D000u) == 0u,
           "caller normalization must reject addresses outside the supported loaded image");
}

struct FakeBatchIo {
    std::vector<uintptr_t> created;
    std::map<uintptr_t, bool> pending;
    std::vector<uintptr_t> enabled;
    std::vector<uintptr_t> mayHaveRun;
    int queueEnableCalls = 0;
    int queueDisableCalls = 0;
    int applyCalls = 0;
    int disableCalls = 0;
    int failQueueEnableAt = 0;
    int failQueueDisableAt = 0;
    std::vector<int> failApplyCalls;
    int failDisableAt = 0;
    bool unexpectedTarget = false;
    FfxHooks::MinHookBatch::Coordinator* reentrantCoordinator = nullptr;
    FfxHooks::MinHookBatch::Owner reentrantOwner =
        FfxHooks::MinHookBatch::Owner::FieldScout;
    bool invokeReentrantEnable = false;
    bool reentrantInvoked = false;
    FfxHooks::MinHookBatch::BatchResult reentrantResult =
        FfxHooks::MinHookBatch::BatchResult::InvalidArgument;

    bool IsCreated(uintptr_t target) const {
        return std::find(created.begin(), created.end(), target) != created.end();
    }

    static bool QueueEnable(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeBatchIo*>(context);
        ++fake.queueEnableCalls;
        if (!fake.IsCreated(target)) {
            fake.unexpectedTarget = true;
            return false;
        }
        if (fake.invokeReentrantEnable && !fake.reentrantInvoked &&
            fake.reentrantCoordinator) {
            fake.reentrantInvoked = true;
            const uintptr_t nestedTarget = target;
            const auto nested = FfxHooks::MinHookBatch::EnableBatch(
                fake.reentrantCoordinator,
                {&fake, &FakeBatchIo::QueueEnable, &FakeBatchIo::QueueDisable,
                 &FakeBatchIo::Apply, &FakeBatchIo::Disable},
                fake.reentrantOwner,
                &nestedTarget, 1u);
            fake.reentrantResult = nested.result;
        }
        if (fake.failQueueEnableAt == fake.queueEnableCalls) return false;
        fake.pending[target] = true;
        return true;
    }

    static bool QueueDisable(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeBatchIo*>(context);
        ++fake.queueDisableCalls;
        if (!fake.IsCreated(target)) {
            fake.unexpectedTarget = true;
            return false;
        }
        if (fake.failQueueDisableAt == fake.queueDisableCalls) return false;
        fake.pending[target] = false;
        return true;
    }

    static bool Apply(void* context) {
        auto& fake = *static_cast<FakeBatchIo*>(context);
        ++fake.applyCalls;
        for (uintptr_t target : fake.created) {
            if (std::find(fake.mayHaveRun.begin(), fake.mayHaveRun.end(), target) ==
                fake.mayHaveRun.end()) {
                fake.mayHaveRun.push_back(target);
            }
        }
        const bool fail = std::find(
            fake.failApplyCalls.begin(), fake.failApplyCalls.end(), fake.applyCalls) !=
            fake.failApplyCalls.end();
        size_t applied = 0u;
        for (const auto& queued : fake.pending) {
            if (queued.second) {
                if (std::find(fake.enabled.begin(), fake.enabled.end(), queued.first) ==
                    fake.enabled.end()) {
                    fake.enabled.push_back(queued.first);
                }
            } else {
                fake.enabled.erase(
                    std::remove(fake.enabled.begin(), fake.enabled.end(), queued.first),
                    fake.enabled.end());
            }
            ++applied;
            if (fail && applied == 1u) break;
        }
        if (!fail) fake.pending.clear();
        return !fail;
    }

    static bool Disable(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeBatchIo*>(context);
        ++fake.disableCalls;
        if (!fake.IsCreated(target)) {
            fake.unexpectedTarget = true;
            return false;
        }
        if (fake.failDisableAt == fake.disableCalls) return false;
        fake.enabled.erase(std::remove(fake.enabled.begin(), fake.enabled.end(), target),
                           fake.enabled.end());
        return true;
    }
};

FfxHooks::MinHookBatch::BatchIo BatchApi(FakeBatchIo& fake) {
    return {&fake, &FakeBatchIo::QueueEnable, &FakeBatchIo::QueueDisable,
            &FakeBatchIo::Apply, &FakeBatchIo::Disable};
}

struct FakeInitialization {
    int calls = 0;
    bool fail = false;
    FfxHooks::MinHookBatch::Coordinator* reentrantCoordinator = nullptr;
    FfxHooks::MinHookBatch::InitializationResult reentrantResult =
        FfxHooks::MinHookBatch::InitializationResult::InvalidArgument;

    static bool Initialize(void* context) {
        auto& fake = *static_cast<FakeInitialization*>(context);
        ++fake.calls;
        if (fake.reentrantCoordinator) {
            fake.reentrantResult = FfxHooks::MinHookBatch::EnsureInitialized(
                fake.reentrantCoordinator, {&fake, &FakeInitialization::Initialize});
            fake.reentrantCoordinator = nullptr;
        }
        return !fake.fail;
    }
};

FfxHooks::MinHookBatch::InitializationIo InitializationApi(FakeInitialization& fake) {
    return {&fake, &FakeInitialization::Initialize};
}

void PrimeCoordinator(FfxHooks::MinHookBatch::Coordinator& coordinator) {
    FakeInitialization initialization;
    Expect(FfxHooks::MinHookBatch::EnsureInitialized(
               &coordinator, InitializationApi(initialization)) ==
               FfxHooks::MinHookBatch::InitializationResult::Ready &&
               initialization.calls == 1,
           "test coordinator must initialize exactly once before queue ownership");
}

void TestProcessGlobalMinHookInitialization() {
    using namespace FfxHooks::MinHookBatch;
    constexpr uintptr_t target = 0x00784120u;

    Coordinator coordinator;
    FakeBatchIo batch;
    batch.created = {target};
    Expect(EnableBatch(&coordinator, BatchApi(batch), Owner::MonsterAiObserver,
                       &target, 1u).result == BatchResult::NotInitialized &&
               batch.queueEnableCalls == 0,
           "queue ownership must fail closed before process-global MinHook initialization");

    FakeInitialization initialization;
    initialization.reentrantCoordinator = &coordinator;
    const InitializationResult ready = EnsureInitialized(
        &coordinator, InitializationApi(initialization));
    const InitializationResult alreadyReady = EnsureInitialized(
        &coordinator, InitializationApi(initialization));
    Expect(ready == InitializationResult::Ready &&
               alreadyReady == InitializationResult::Ready &&
               initialization.calls == 1 &&
               initialization.reentrantResult == InitializationResult::Busy &&
               GetSnapshot(coordinator).initialized,
           "process-global initialization must be single-flight, idempotent, and reentrancy-safe");

    Coordinator failedCoordinator;
    FakeInitialization failure;
    failure.fail = true;
    const InitializationResult failed = EnsureInitialized(
        &failedCoordinator, InitializationApi(failure));
    const InitializationResult poisoned = EnsureInitialized(
        &failedCoordinator, InitializationApi(failure));
    Expect(failed == InitializationResult::FailedPoisoned &&
               poisoned == InitializationResult::Poisoned && failure.calls == 1 &&
               GetSnapshot(failedCoordinator).state == State::Poisoned &&
               !GetSnapshot(failedCoordinator).initialized,
           "an initialization failure must poison MinHook ownership until process restart");
}

void TestProcessGlobalMinHookCoordinator() {
    using namespace FfxHooks::MinHookBatch;
    constexpr std::array<uintptr_t, 2> targets = {0x00784120u, 0x00781660u};

    Coordinator successCoordinator;
    PrimeCoordinator(successCoordinator);
    FakeBatchIo success;
    success.created.assign(targets.begin(), targets.end());
    const BatchReport enabled = EnableBatch(
        &successCoordinator, BatchApi(success), Owner::MonsterAiObserver,
        targets.data(), targets.size());
    Expect(enabled.result == BatchResult::Applied && enabled.applyAttempted &&
               enabled.mayHaveRun && enabled.queuedEnableCount == targets.size() &&
               success.enabled.size() == targets.size() &&
               GetSnapshot(successCoordinator).state == State::Idle,
           "one owner must apply its complete MinHook batch and release the coordinator");
    const BatchReport neutralized = NeutralizeBatch(
        &successCoordinator, BatchApi(success), Owner::MonsterAiObserver,
        targets.data(), targets.size());
    Expect(neutralized.result == BatchResult::Neutralized &&
               neutralized.queuedDisableCount == targets.size() &&
               neutralized.exactDisabled && success.enabled.empty() &&
               success.applyCalls == 2 && success.disableCalls == 2,
           "normal teardown must queue-disable, apply, then exactly disable every owned target");

    Coordinator queueFailureCoordinator;
    PrimeCoordinator(queueFailureCoordinator);
    FakeBatchIo queueFailure;
    queueFailure.created.assign(targets.begin(), targets.end());
    queueFailure.failQueueEnableAt = 2;
    const BatchReport queueFailed = EnableBatch(
        &queueFailureCoordinator, BatchApi(queueFailure), Owner::MonsterAiObserver,
        targets.data(), targets.size());
    Expect(queueFailed.result == BatchResult::EnableFailedNeutralized &&
               queueFailed.primaryFailure == FailureStage::QueueEnable &&
               queueFailed.applyAttempted && queueFailed.mayHaveRun &&
               queueFailed.neutralized && queueFailure.queueDisableCalls == 2 &&
               queueFailure.applyCalls == 1 && queueFailure.enabled.empty(),
           "a partial queue-enable failure must be neutralized within the same owner batch");

    Coordinator applyFailureCoordinator;
    PrimeCoordinator(applyFailureCoordinator);
    FakeBatchIo applyFailure;
    applyFailure.created.assign(targets.begin(), targets.end());
    applyFailure.failApplyCalls = {1};
    const BatchReport applyFailed = EnableBatch(
        &applyFailureCoordinator, BatchApi(applyFailure), Owner::MonsterAiObserver,
        targets.data(), targets.size());
    Expect(applyFailed.result == BatchResult::Poisoned &&
               applyFailed.primaryFailure == FailureStage::ApplyEnable &&
               applyFailed.neutralized && applyFailure.applyCalls == 2 &&
               applyFailure.queueDisableCalls == 2 && applyFailure.disableCalls == 2 &&
               applyFailure.enabled.empty() &&
               GetSnapshot(applyFailureCoordinator).state == State::Poisoned,
           "a failed enable apply must neutralize targets but poison future batches until restart");

    Coordinator poisonedCoordinator;
    PrimeCoordinator(poisonedCoordinator);
    FakeBatchIo poisoned;
    poisoned.created.assign(targets.begin(), targets.end());
    poisoned.failApplyCalls = {1, 2};
    const BatchReport poisonedReport = EnableBatch(
        &poisonedCoordinator, BatchApi(poisoned), Owner::MonsterAiObserver,
        targets.data(), targets.size());
    const int poisonedApplyCalls = poisoned.applyCalls;
    const BatchReport blocked = EnableBatch(
        &poisonedCoordinator, BatchApi(poisoned), Owner::FieldScout,
        targets.data(), targets.size());
    Expect(poisonedReport.result == BatchResult::Poisoned &&
               poisonedReport.neutralizationFailure == FailureStage::ApplyDisable &&
               GetSnapshot(poisonedCoordinator).state == State::Poisoned &&
               blocked.result == BatchResult::Poisoned &&
               poisoned.applyCalls == poisonedApplyCalls,
           "a failed neutralization must poison the process-global coordinator until restart");

    Coordinator disableQueueCoordinator;
    PrimeCoordinator(disableQueueCoordinator);
    FakeBatchIo disableQueueFailure;
    disableQueueFailure.created.assign(targets.begin(), targets.end());
    Expect(EnableBatch(&disableQueueCoordinator, BatchApi(disableQueueFailure),
                       Owner::FieldScout, targets.data(), targets.size()).result ==
               BatchResult::Applied,
           "disable-queue failure fixture must begin from an applied FieldScout batch");
    disableQueueFailure.failQueueDisableAt = 2;
    const BatchReport disableQueuePoisoned = NeutralizeBatch(
        &disableQueueCoordinator, BatchApi(disableQueueFailure), Owner::FieldScout,
        targets.data(), targets.size());
    Expect(disableQueuePoisoned.result == BatchResult::Poisoned &&
               disableQueuePoisoned.neutralizationFailure == FailureStage::QueueDisable &&
               disableQueueFailure.disableCalls == 2 &&
               GetSnapshot(disableQueueCoordinator).state == State::Poisoned,
           "a queue-disable failure must still exact-disable all targets and poison future applies");

    Coordinator exactDisableCoordinator;
    PrimeCoordinator(exactDisableCoordinator);
    FakeBatchIo exactDisableFailure;
    exactDisableFailure.created.assign(targets.begin(), targets.end());
    Expect(EnableBatch(&exactDisableCoordinator, BatchApi(exactDisableFailure),
                       Owner::FieldScout, targets.data(), targets.size()).result ==
               BatchResult::Applied,
           "exact-disable failure fixture must begin from an applied batch");
    exactDisableFailure.failDisableAt = 1;
    const BatchReport exactDisablePoisoned = NeutralizeBatch(
        &exactDisableCoordinator, BatchApi(exactDisableFailure), Owner::FieldScout,
        targets.data(), targets.size());
    Expect(exactDisablePoisoned.result == BatchResult::Poisoned &&
               exactDisablePoisoned.neutralizationFailure == FailureStage::ExactDisable &&
               exactDisableFailure.disableCalls == 2,
           "an exact-disable failure must preserve poison while attempting every target");

    Coordinator collisionCoordinator;
    PrimeCoordinator(collisionCoordinator);
    FakeBatchIo collision;
    collision.created.assign(targets.begin(), targets.end());
    collision.reentrantCoordinator = &collisionCoordinator;
    collision.reentrantOwner = Owner::Difficulty;
    collision.invokeReentrantEnable = true;
    const BatchReport collisionOuter = EnableBatch(
        &collisionCoordinator, BatchApi(collision), Owner::MonsterAiObserver,
        targets.data(), targets.size());
    Expect(collisionOuter.result == BatchResult::Applied && collision.reentrantInvoked &&
               collision.reentrantResult == BatchResult::Busy &&
               !collision.unexpectedTarget,
           "Difficulty must fail busy instead of aliasing or applying Monster AI's owned batch");

    Coordinator invalidCoordinator;
    PrimeCoordinator(invalidCoordinator);
    FakeBatchIo invalid;
    invalid.created.assign(targets.begin(), targets.end());
    const std::array<uintptr_t, 2> duplicate = {targets[0], targets[0]};
    Expect(EnableBatch(&invalidCoordinator, BatchApi(invalid), Owner::MonsterAiObserver,
                       duplicate.data(), duplicate.size()).result ==
               BatchResult::InvalidArgument && invalid.queueEnableCalls == 0 &&
               GetSnapshot(invalidCoordinator).state == State::Idle,
           "duplicate or ambiguous batch targets must fail before touching MinHook");
    Expect(EnableBatch(&invalidCoordinator, BatchApi(invalid),
                       static_cast<Owner>(0xFFu), targets.data(), targets.size()).result ==
               BatchResult::InvalidArgument && invalid.queueEnableCalls == 0,
           "an unknown owner value must not acquire or mutate the process-global queue");

    Expect(Owner::Difficulty != Owner::MonsterAiObserver &&
               Owner::Difficulty != Owner::FieldScout &&
               Owner::SeymourBattle != Owner::MonsterAiObserver &&
               Owner::SeymourBattle != Owner::Difficulty,
           "Difficulty and Seymour must have distinct queue ownership instead of aliasing Monster AI");

    Coordinator knownOwnersCoordinator;
    PrimeCoordinator(knownOwnersCoordinator);
    FakeBatchIo knownOwners;
    knownOwners.created.assign(targets.begin(), targets.end());
    const BatchReport difficultyApplied = EnableBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::Difficulty,
        targets.data(), targets.size());
    const BatchReport difficultyNeutralized = NeutralizeBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::Difficulty,
        targets.data(), targets.size());
    const BatchReport seymourApplied = EnableBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::SeymourBattle,
        targets.data(), targets.size());
    const BatchReport seymourNeutralized = NeutralizeBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::SeymourBattle,
        targets.data(), targets.size());
    Expect(difficultyApplied.result == BatchResult::Applied &&
               difficultyNeutralized.result == BatchResult::Neutralized &&
               seymourApplied.result == BatchResult::Applied &&
               seymourNeutralized.result == BatchResult::Neutralized,
           "the generic coordinator must admit distinct reviewed Difficulty and Seymour owners");

    // CustomMix installs a late position accessor alongside the already-active shared batch.
    // A missing owner registration must fail here rather than survive leaf getter tests.
    constexpr uintptr_t positionTarget = 0x007AC000u;
    knownOwners.created.push_back(positionTarget);
    Expect(EnableBatch(&knownOwnersCoordinator, BatchApi(knownOwners), Owner::Difficulty,
                      targets.data(), targets.size()).result == BatchResult::Applied,
           "shared battle hooks remain active before the optional position hook installs");
    const auto positionsApplied = EnableBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::ArenaPositions,
        &positionTarget, 1u);
    Expect(positionsApplied.result == BatchResult::Applied &&
               positionsApplied.queuedEnableCount == 1u && knownOwners.enabled.size() == 3u,
           "ArenaPositions must activate its native accessor through the production coordinator");
    const auto positionsRetired = NeutralizeBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::ArenaPositions,
        &positionTarget, 1u);
    Expect(positionsRetired.result == BatchResult::Neutralized &&
               positionsRetired.exactDisabled && knownOwners.enabled.size() == targets.size() &&
               std::find(knownOwners.enabled.begin(), knownOwners.enabled.end(), positionTarget) ==
                   knownOwners.enabled.end(),
           "position retirement must disable only its target and preserve shared battle hooks");
    knownOwners.failQueueEnableAt = knownOwners.queueEnableCalls + 1;
    const auto positionsFailed = EnableBatch(
        &knownOwnersCoordinator, BatchApi(knownOwners), Owner::ArenaPositions,
        &positionTarget, 1u);
    Expect(positionsFailed.result == BatchResult::EnableFailedNeutralized &&
               knownOwners.enabled.size() == targets.size() &&
               GetSnapshot(knownOwnersCoordinator).state == State::Idle,
           "a failed optional position enable must leave the Difficulty batch intact");
    Expect(NeutralizeBatch(&knownOwnersCoordinator, BatchApi(knownOwners), Owner::Difficulty,
                         targets.data(), targets.size()).result == BatchResult::Neutralized &&
               knownOwners.enabled.empty(),
           "shared battle hooks can still retire after a position hook enable failure");
}

void TestRetainedMonsterObserverOwnerSurvivesFieldScoutLifecycle() {
    using namespace FfxHooks::MinHookBatch;
    constexpr uintptr_t supportedImageBase = 0x00400000u;
    constexpr std::array<uintptr_t, 3> monsterObserverTargets = {
        supportedImageBase + 0x00384120u,
        supportedImageBase + 0x00381660u,
        supportedImageBase + 0x003AC9E0u};
    constexpr std::array<uintptr_t, 2> fieldScoutTargets = {
        0x006A1230u, 0x006A4560u};

    Coordinator coordinator;
    PrimeCoordinator(coordinator);
    FakeBatchIo minHook;
    minHook.created.assign(monsterObserverTargets.begin(), monsterObserverTargets.end());
    minHook.created.insert(
        minHook.created.end(), fieldScoutTargets.begin(), fieldScoutTargets.end());

    const BatchReport observerEnabled = EnableBatch(
        &coordinator, BatchApi(minHook), Owner::MonsterAiObserver,
        monsterObserverTargets.data(), monsterObserverTargets.size());
    const BatchReport observerRetained = NeutralizeBatch(
        &coordinator, BatchApi(minHook), Owner::MonsterAiObserver,
        monsterObserverTargets.data(), monsterObserverTargets.size());
    const std::vector<uintptr_t> createdAfterObserver = minHook.created;
    const BatchReport fieldScoutEnabled = EnableBatch(
        &coordinator, BatchApi(minHook), Owner::FieldScout,
        fieldScoutTargets.data(), fieldScoutTargets.size());
    const BatchReport fieldScoutRetained = NeutralizeBatch(
        &coordinator, BatchApi(minHook), Owner::FieldScout,
        fieldScoutTargets.data(), fieldScoutTargets.size());

    Expect(observerEnabled.result == BatchResult::Applied &&
               observerRetained.result == BatchResult::Neutralized &&
               fieldScoutEnabled.result == BatchResult::Applied &&
               fieldScoutRetained.result == BatchResult::Neutralized &&
               minHook.created == createdAfterObserver && minHook.enabled.empty() &&
               GetSnapshot(coordinator).state == State::Idle,
           "FieldScout teardown must preserve the retained exact Monster AI observer targets");
}

void PutU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes.at(offset) = static_cast<uint8_t>(value & 0xFFu);
    bytes.at(offset + 1u) = static_cast<uint8_t>((value >> 8u) & 0xFFu);
}

void PutU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes.at(offset) = static_cast<uint8_t>(value & 0xFFu);
    bytes.at(offset + 1u) = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    bytes.at(offset + 2u) = static_cast<uint8_t>((value >> 16u) & 0xFFu);
    bytes.at(offset + 3u) = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

struct FakeMemory {
    uintptr_t base = 0x10000000u;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x90000u, 0u);
    std::vector<std::pair<uintptr_t, uintptr_t>> denied;
    size_t readCalls = 0;
    size_t writeCalls = 0;

    static bool Read(void* context, uintptr_t address, uint8_t* out, size_t length) {
        auto& memory = *static_cast<FakeMemory*>(context);
        ++memory.readCalls;
        if (!out || address < memory.base || length > memory.bytes.size()) return false;
        const uintptr_t relative = address - memory.base;
        if (relative > memory.bytes.size() || length > memory.bytes.size() - relative) return false;
        const uintptr_t end = address + length;
        if (end < address) return false;
        for (const auto& range : memory.denied) {
            if (address < range.second && end > range.first) return false;
        }
        std::memcpy(out, memory.bytes.data() + static_cast<size_t>(relative), length);
        return true;
    }

    size_t Offset(uintptr_t address) const {
        return static_cast<size_t>(address - base);
    }
};

constexpr uintptr_t kActorList = 0x10010000u;
constexpr size_t kActorStride = FfxHooks::MonsterAiObserver::kActorStride;

void InitializeEmptyActors(FakeMemory& memory) {
    for (size_t slot = 0; slot < FfxHooks::MonsterAiObserver::kActorSlotCount; ++slot) {
        PutU16(memory.bytes, memory.Offset(kActorList + slot * kActorStride +
            FfxHooks::MonsterAiObserver::kMonsterIdOffset), 0xFFFFu);
    }
}

void MakeValidSlot(FakeMemory& memory, size_t slot, uint16_t monsterId,
                   uint8_t workerCount = 2u, uint32_t fileSkew = 0u) {
    const uintptr_t actor = kActorList + slot * kActorStride;
    const uintptr_t file = memory.base + 0x30000u + slot * 0x8000u + fileSkew;
    constexpr uint32_t aiOffset = 0x30u;
    constexpr uint32_t workerOffset = 0x90u;
    constexpr uint32_t statOffset = 0xD0u;
    constexpr uint32_t fileSize = 0x180u;

    PutU16(memory.bytes, memory.Offset(actor + FfxHooks::MonsterAiObserver::kMonsterIdOffset), monsterId);
    PutU32(memory.bytes, memory.Offset(actor + FfxHooks::MonsterAiObserver::kWholeFileOffset),
           static_cast<uint32_t>(file));
    PutU32(memory.bytes, memory.Offset(actor + FfxHooks::MonsterAiObserver::kAiFileOffset),
           static_cast<uint32_t>(file + aiOffset));
    PutU32(memory.bytes, memory.Offset(actor + FfxHooks::MonsterAiObserver::kWorkerFileOffset),
           static_cast<uint32_t>(file + workerOffset));
    memory.bytes.at(memory.Offset(actor + FfxHooks::MonsterAiObserver::kWorkerCountOffset)) = workerCount;

    PutU32(memory.bytes, memory.Offset(file + FfxHooks::MonsterAiObserver::kHeaderAiOffset), aiOffset);
    PutU32(memory.bytes, memory.Offset(file + FfxHooks::MonsterAiObserver::kHeaderWorkerOffset), workerOffset);
    PutU32(memory.bytes, memory.Offset(file + FfxHooks::MonsterAiObserver::kHeaderStatOffset), statOffset);
    PutU32(memory.bytes, memory.Offset(file + FfxHooks::MonsterAiObserver::kHeaderFileSizeOffset), fileSize);
    for (uint32_t i = aiOffset; i < statOffset; ++i) {
        memory.bytes.at(memory.Offset(file + i)) = static_cast<uint8_t>((i * 17u + monsterId) & 0xFFu);
    }
}

FfxHooks::MonsterAiObserver::ReadOnlyMemory Reader(FakeMemory& memory) {
    return {&memory, &FakeMemory::Read};
}

struct SinkLog {
    std::vector<FfxHooks::MonsterAiObserver::SnapshotPhase> phases;
    std::vector<FfxHooks::MonsterAiObserver::Snapshot> snapshots;
    std::vector<std::pair<uint32_t, uint32_t>> teardowns;

    static void SnapshotEvent(void* context,
                              FfxHooks::MonsterAiObserver::SnapshotPhase phase,
                              const FfxHooks::MonsterAiObserver::Snapshot& snapshot) {
        auto& log = *static_cast<SinkLog*>(context);
        log.phases.push_back(phase);
        log.snapshots.push_back(snapshot);
    }

    static void TeardownEvent(void* context, uint32_t generation, uint32_t threadId) {
        static_cast<SinkLog*>(context)->teardowns.emplace_back(generation, threadId);
    }
};

FfxHooks::MonsterAiObserver::EventSink Sink(SinkLog& log) {
    return {&log, &SinkLog::SnapshotEvent, &SinkLog::TeardownEvent};
}

struct OriginalCallState {
    int calls = 0;
    FakeMemory* memory = nullptr;
    bool mutateAi = false;
};

void OriginalCall(void* context) {
    auto& state = *static_cast<OriginalCallState*>(context);
    ++state.calls;
    if (state.mutateAi && state.memory) {
        const uintptr_t file = state.memory->base + 0x30000u;
        state.memory->bytes.at(state.memory->Offset(file + 0x40u)) ^= 0x5Au;
    }
}

struct ReentrantContext {
    FfxHooks::MonsterAiObserver::LifecycleState* lifecycle = nullptr;
    FfxHooks::MonsterAiObserver::ReadOnlyMemory memory{};
    FfxHooks::MonsterAiObserver::EventSink sink{};
    int outerCalls = 0;
    int innerCalls = 0;
};

void InnerOriginal(void* context) {
    ++static_cast<ReentrantContext*>(context)->innerCalls;
}

struct StopDuringOriginalContext {
    FfxHooks::MonsterAiObserver::LifecycleState* lifecycle = nullptr;
    int calls = 0;
};

void StopDuringOriginal(void* context) {
    auto& state = *static_cast<StopDuringOriginalContext*>(context);
    ++state.calls;
    FfxHooks::MonsterAiObserver::RequestStop(state.lifecycle);
}

void ReentrantOriginal(void* context) {
    auto& state = *static_cast<ReentrantContext*>(context);
    ++state.outerCalls;
    FfxHooks::MonsterAiObserver::CallbackLease callback(state.lifecycle);
    FfxHooks::MonsterAiObserver::ObserveRegistration(
        callback, state.memory, kActorList, 77u, state.sink,
        {&state, &InnerOriginal});
}

void TestSnapshotBoundsAndPrivacy() {
    using namespace FfxHooks::MonsterAiObserver;
    FakeMemory memory;
    InitializeEmptyActors(memory);

    Snapshot snapshot{};
    Expect(CaptureSnapshot(Reader(memory), 0u, 1u, 9u, &snapshot) == CaptureResult::InvalidActorList,
           "null actor list must fail closed");
    Expect(!snapshot.listValid, "null actor list must publish invalid-list state");
    Expect(memory.writeCalls == 0u, "snapshot API must have no memory-write path");

    MakeValidSlot(memory, 0u, 0x0156u);
    Expect(CaptureSnapshot(Reader(memory), kActorList, 2u, 42u, &snapshot) == CaptureResult::Captured,
           "one valid slot must produce a bounded snapshot");
    Expect(snapshot.listValid && snapshot.generation == 2u && snapshot.threadId == 42u,
           "snapshot must retain only lifecycle metadata");
    const SlotSnapshot& slot = snapshot.slots[0];
    Expect(slot.slot == 0u && slot.monsterId == 0x0156u && slot.valid && slot.workerCount == 2u,
           "valid slot must expose the bounded public fields");
    Expect(slot.aiLength == 0x60u && slot.workerLength == 0x40u &&
               slot.aiHash != 0u && slot.workerHash != 0u,
           "valid script partitions must be represented only by bounded lengths and deterministic hashes");
    Snapshot repeated{};
    Expect(CaptureSnapshot(Reader(memory), kActorList, 2u, 42u, &repeated) == CaptureResult::Captured &&
               repeated.slots[0].aiHash == slot.aiHash &&
               repeated.slots[0].workerHash == slot.workerHash,
           "unchanged memory must hash deterministically");

    char line[256] = {};
    Expect(FormatSlotLine(snapshot, 0u, line, sizeof(line)),
           "valid slot must format into a bounded maintenance log line");
    const std::string formatted(line);
    for (const char* required : {"generation=2", "thread=42", "slot=0", "monster_id=0x0156",
                                  "valid=1", "workers=2", "ai_length=96", "ai_hash=",
                                  "worker_length=64", "worker_hash="}) {
        Expect(formatted.find(required) != std::string::npos,
               "observer line must contain every allowed field");
    }
    for (const char* forbidden : {"pointer", "address", "bytes", "script=", "payload", "0x100"}) {
        Expect(formatted.find(forbidden) == std::string::npos,
               "observer line must not expose pointers or raw script material");
    }
}

void TestInvalidAndDuplicateSlots() {
    using namespace FfxHooks::MonsterAiObserver;
    FakeMemory memory;
    InitializeEmptyActors(memory);
    MakeValidSlot(memory, 0u, 0x0200u);
    MakeValidSlot(memory, 1u, 0x0201u, 0u);
    MakeValidSlot(memory, 2u, 0x0202u);
    MakeValidSlot(memory, 3u, 0x0200u);

    const uintptr_t slot2Actor = kActorList + 2u * kActorStride;
    PutU32(memory.bytes, memory.Offset(slot2Actor + kWorkerFileOffset), 0xFFFFFFF0u);

    Snapshot snapshot{};
    Expect(CaptureSnapshot(Reader(memory), kActorList, 3u, 11u, &snapshot) == CaptureResult::Captured,
           "mixed slots must still produce an eight-slot snapshot");
    Expect(!snapshot.slots[0].valid && !snapshot.slots[3].valid,
           "duplicate occupied monster IDs must both fail closed");
    Expect(!snapshot.slots[1].valid && snapshot.slots[1].workerCount == 0u,
           "zero-worker script state must be invalid without reading script bytes");
    Expect(!snapshot.slots[2].valid && snapshot.slots[2].aiHash == 0u &&
               snapshot.slots[2].workerHash == 0u,
           "overflowing or inconsistent file bounds must clear both hashes");
    Expect(!snapshot.slots[4].valid && snapshot.slots[4].monsterId == 0xFFFFu,
           "empty actor slots must stay explicit and invalid");

    FakeMemory shortMemory;
    InitializeEmptyActors(shortMemory);
    shortMemory.denied.push_back({kActorList + kMonsterIdOffset,
                                  kActorList + kMonsterIdOffset + sizeof(uint16_t)});
    Snapshot shortSnapshot{};
    Expect(CaptureSnapshot(Reader(shortMemory), kActorList, 4u, 12u, &shortSnapshot) ==
               CaptureResult::ActorListUnreadable,
           "an unreadable first actor record must classify the list as short/unreadable");
    Expect(!shortSnapshot.listValid, "short actor list must fail closed as a whole");
}

void TestLifecycleAndOriginalCallContract() {
    using namespace FfxHooks::MonsterAiObserver;
    FakeMemory memory;
    InitializeEmptyActors(memory);
    MakeValidSlot(memory, 0u, 0x0310u);
    LifecycleState lifecycle{};
    InitializeLifecycle(&lifecycle);
    Expect(!ObservationAdmitted(lifecycle),
           "lifecycle initialization must keep observation closed until hook activation succeeds");
    OpenAdmission(&lifecycle);
    SinkLog sinkLog;
    OriginalCallState original{0, &memory, true};

    {
        CallbackLease callback(&lifecycle);
        ObserveRegistration(callback, Reader(memory), kActorList, 51u, Sink(sinkLog),
                            {&original, &OriginalCall});
    }
    Expect(original.calls == 1, "registration adapter must invoke original exactly once");
    Expect(sinkLog.snapshots.size() == 2u && sinkLog.phases.size() == 2u &&
               sinkLog.phases[0] == SnapshotPhase::BeforeRegistration &&
               sinkLog.phases[1] == SnapshotPhase::AfterRegistration,
           "registration must publish one bounded before/after pair");
    Expect(sinkLog.snapshots[0].generation == sinkLog.snapshots[1].generation &&
               sinkLog.snapshots[0].slots[0].aiHash != sinkLog.snapshots[1].slots[0].aiHash,
           "before/after snapshots must pair by generation and observe original changes");

    OriginalCallState cleanup{};
    {
        CallbackLease callback(&lifecycle);
        ObserveCleanup(callback, 52u, Sink(sinkLog), {&cleanup, &OriginalCall});
    }
    Expect(cleanup.calls == 1, "cleanup adapter must invoke original exactly once");
    Expect(sinkLog.teardowns.size() == 1u &&
               sinkLog.teardowns[0].first == sinkLog.snapshots[0].generation &&
               sinkLog.teardowns[0].second == 52u,
           "cleanup must publish the matching registration generation");

    Expect(NextGeneration(0u) == 1u &&
               NextGeneration((std::numeric_limits<uint32_t>::max)()) == 1u,
           "generation rollover must reserve zero and restart at one");
}

void TestReentrancyAndStopAdmission() {
    using namespace FfxHooks::MonsterAiObserver;
    FakeMemory memory;
    InitializeEmptyActors(memory);
    MakeValidSlot(memory, 0u, 0x0410u);
    LifecycleState lifecycle{};
    InitializeLifecycle(&lifecycle);
    OpenAdmission(&lifecycle);
    SinkLog sinkLog;
    ReentrantContext context{&lifecycle, Reader(memory), Sink(sinkLog), 0, 0};

    {
        CallbackLease callback(&lifecycle);
        ObserveRegistration(callback, Reader(memory), kActorList, 76u, Sink(sinkLog),
                            {&context, &ReentrantOriginal});
    }
    Expect(context.outerCalls == 1 && context.innerCalls == 1,
           "reentrant registration must still invoke every original exactly once");
    Expect(sinkLog.snapshots.size() == 2u,
           "reentrant observer call must not publish a nested snapshot pair");
    Expect(ActiveCallbacks(lifecycle) == 0u,
           "callback accounting must drain after the outer invocation");

    RequestStop(&lifecycle);
    OriginalCallState stopped{};
    {
        CallbackLease callback(&lifecycle);
        ObserveRegistration(callback, Reader(memory), kActorList, 78u, Sink(sinkLog),
                            {&stopped, &OriginalCall});
    }
    Expect(stopped.calls == 1 && sinkLog.snapshots.size() == 2u,
           "stop admission must suppress observation without suppressing vanilla execution");
    Expect(!ObservationAdmitted(lifecycle), "stop request must be sticky");

    LifecycleState raceLifecycle{};
    InitializeLifecycle(&raceLifecycle);
    OpenAdmission(&raceLifecycle);
    SinkLog raceLog;
    StopDuringOriginalContext race{&raceLifecycle, 0};
    {
        CallbackLease callback(&raceLifecycle);
        ObserveRegistration(callback, Reader(memory), kActorList, 79u, Sink(raceLog),
                            {&race, &StopDuringOriginal});
    }
    Expect(race.calls == 1 && raceLog.snapshots.size() == 2u,
           "an admitted callback must finish its paired post-snapshot when stop races its original");
    Expect(!ObservationAdmitted(raceLifecycle) && ActiveCallbacks(raceLifecycle) == 0u,
           "a stop racing the original must remain sticky and drain callback ownership");
}

struct FakeDetours {
    FfxHooks::MonsterAiObserver::LifecycleState* lifecycle = nullptr;
    int failCreateAt = 0;
    int failQueueEnableAt = 0;
    int failQueueDisableAt = 0;
    std::vector<int> failApplyCalls;
    bool failDisable = false;
    int failRemoveAt = 0;
    bool applyStartsCallback = false;
    bool disableStartsLateCallback = false;
    bool lateCallbackStarted = false;
    bool disableWhileAdmitted = false;
    bool removeWhileCallbackActive = false;
    bool unexpectedDisableTarget = false;
    bool unexpectedRemoveTarget = false;
    int createCalls = 0;
    int queueEnableCalls = 0;
    int queueDisableCalls = 0;
    int applyCalls = 0;
    int removeCalls = 0;
    std::vector<uintptr_t> created;
    std::map<uintptr_t, bool> pending;
    std::vector<uintptr_t> enabled;
    std::vector<uintptr_t> disabled;
    std::vector<uintptr_t> removed;

    static bool Create(void* context, uintptr_t target, uintptr_t, uintptr_t* originalOut) {
        auto& fake = *static_cast<FakeDetours*>(context);
        ++fake.createCalls;
        if (fake.failCreateAt == fake.createCalls) return false;
        *originalOut = target + 0x100u;
        fake.created.push_back(target);
        return true;
    }
    static bool QueueEnable(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeDetours*>(context);
        ++fake.queueEnableCalls;
        if (fake.failQueueEnableAt == fake.queueEnableCalls) return false;
        fake.pending[target] = true;
        return true;
    }
    static bool QueueDisable(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeDetours*>(context);
        ++fake.queueDisableCalls;
        if (fake.failQueueDisableAt == fake.queueDisableCalls) return false;
        fake.pending[target] = false;
        return true;
    }
    static bool Apply(void* context) {
        auto& fake = *static_cast<FakeDetours*>(context);
        ++fake.applyCalls;
        const bool fail = std::find(fake.failApplyCalls.begin(), fake.failApplyCalls.end(),
                                    fake.applyCalls) != fake.failApplyCalls.end();
        size_t applied = 0u;
        for (const auto& queued : fake.pending) {
            if (queued.second) {
                if (std::find(fake.enabled.begin(), fake.enabled.end(), queued.first) ==
                    fake.enabled.end()) {
                    fake.enabled.push_back(queued.first);
                }
            } else {
                fake.enabled.erase(
                    std::remove(fake.enabled.begin(), fake.enabled.end(), queued.first),
                    fake.enabled.end());
            }
            ++applied;
            if (fail && applied == 1u) break;
        }
        if (fail) {
            // Model the documented conservative case: the queue reports failure after the first
            // target became reachable and a callback entered it.
            if (fake.applyStartsCallback && fake.lifecycle) {
                fake.lifecycle->activeCallbacks.store(1u, std::memory_order_release);
            }
            return false;
        }
        fake.pending.clear();
        return true;
    }
    static bool Disable(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeDetours*>(context);
        if (std::find(fake.created.begin(), fake.created.end(), target) == fake.created.end()) {
            fake.unexpectedDisableTarget = true;
            return false;
        }
        if (fake.lifecycle &&
            FfxHooks::MonsterAiObserver::ObservationAdmitted(*fake.lifecycle)) {
            fake.disableWhileAdmitted = true;
        }
        fake.disabled.push_back(target);
        fake.enabled.erase(std::remove(fake.enabled.begin(), fake.enabled.end(), target),
                           fake.enabled.end());
        if (fake.disableStartsLateCallback && !fake.lateCallbackStarted && fake.lifecycle) {
            fake.lateCallbackStarted = true;
            fake.lifecycle->activeCallbacks.store(1u, std::memory_order_release);
        }
        return !fake.failDisable;
    }
    static bool Remove(void* context, uintptr_t target) {
        auto& fake = *static_cast<FakeDetours*>(context);
        if (std::find(fake.created.begin(), fake.created.end(), target) == fake.created.end()) {
            fake.unexpectedRemoveTarget = true;
            return false;
        }
        if (fake.lifecycle &&
            FfxHooks::MonsterAiObserver::ActiveCallbacks(*fake.lifecycle) != 0u) {
            fake.removeWhileCallbackActive = true;
        }
        ++fake.removeCalls;
        fake.removed.push_back(target);
        if (fake.failRemoveAt == fake.removeCalls) return false;
        fake.created.erase(std::remove(fake.created.begin(), fake.created.end(), target),
                           fake.created.end());
        return true;
    }
};

FfxHooks::MonsterAiObserver::DetourIo DetourApi(FakeDetours& fake) {
    return {&fake, &FakeDetours::Create, &FakeDetours::Remove};
}

FfxHooks::MinHookBatch::BatchIo DetourBatchApi(FakeDetours& fake) {
    return {&fake, &FakeDetours::QueueEnable, &FakeDetours::QueueDisable,
            &FakeDetours::Apply, &FakeDetours::Disable};
}

struct FakeDrain {
    FfxHooks::MonsterAiObserver::LifecycleState* lifecycle = nullptr;
    FakeDetours* detours = nullptr;
    bool clearBeforeDisable = true;
    bool clearAfterDisable = true;
    int pauses = 0;
    int pausesBeforeDisable = 0;
    int pausesAfterDisable = 0;

    static void Pause(void* context, uint32_t) {
        auto& drain = *static_cast<FakeDrain*>(context);
        ++drain.pauses;
        const bool beforeDisable = drain.detours && drain.detours->disabled.empty();
        if (beforeDisable) {
            ++drain.pausesBeforeDisable;
        } else {
            ++drain.pausesAfterDisable;
        }
        if (drain.lifecycle && ((beforeDisable && drain.clearBeforeDisable) ||
                                (!beforeDisable && drain.clearAfterDisable))) {
            drain.lifecycle->activeCallbacks.store(0u, std::memory_order_release);
        }
    }
};

FfxHooks::MonsterAiObserver::DrainIo DrainApi(FakeDrain& drain) {
    return {&drain, &FakeDrain::Pause, 4u};
}

struct AdapterFixture {
    FfxHooks::MonsterAiObserver::LifecycleState lifecycle{};
    FfxHooks::MinHookBatch::Coordinator coordinator{};
    FakeDetours detours{};
    FakeDrain drain{};

    explicit AdapterFixture(bool initialize = true) {
        FfxHooks::MonsterAiObserver::InitializeLifecycle(&lifecycle);
        if (initialize) PrimeCoordinator(coordinator);
        detours.lifecycle = &lifecycle;
        drain.lifecycle = &lifecycle;
        drain.detours = &detours;
    }
};

FfxHooks::MonsterAiObserver::InstallResult InstallViaProductionAdapter(
    AdapterFixture& fixture, uintptr_t registration, uintptr_t registrationShim,
    uintptr_t cleanup, uintptr_t cleanupShim,
    FfxHooks::MonsterAiObserver::DetourOwner* owner) {
    constexpr uintptr_t dispatcher = 0x007AC9E0u;
    constexpr uintptr_t dispatcherShim = 0x20003000u;
    return FfxHooks::F7AiSwap_TestInstallAdapter(
        DetourApi(fixture.detours), &fixture.coordinator, DetourBatchApi(fixture.detours),
        DrainApi(fixture.drain), &fixture.lifecycle,
        registration, registrationShim, cleanup, cleanupShim,
        dispatcher, dispatcherShim, owner);
}

FfxHooks::MonsterAiObserver::TeardownResult RemoveViaProductionAdapter(
    AdapterFixture& fixture, FfxHooks::MonsterAiObserver::DetourOwner* owner) {
    return FfxHooks::F7AiSwap_TestRemoveAdapter(
        DetourApi(fixture.detours), &fixture.coordinator, DetourBatchApi(fixture.detours),
        DrainApi(fixture.drain), &fixture.lifecycle, owner);
}

void TestTransactionalDetourOwnership() {
    using namespace FfxHooks::MonsterAiObserver;
    constexpr uintptr_t registration = 0x00784120u;
    constexpr uintptr_t cleanup = 0x00781660u;
    constexpr uintptr_t registrationShim = 0x20001000u;
    constexpr uintptr_t cleanupShim = 0x20002000u;

    AdapterFixture uninitialized(false);
    DetourOwner uninitializedOwner{};
    Expect(InstallViaProductionAdapter(uninitialized, registration, registrationShim,
                                       cleanup, cleanupShim, &uninitializedOwner) ==
               InstallResult::CoordinatorNotInitialized &&
               !uninitializedOwner.registrationCreated &&
               !uninitializedOwner.cleanupCreated && !uninitializedOwner.dispatchCreated &&
               uninitialized.detours.removed ==
                   std::vector<uintptr_t>({0x007AC9E0u, cleanup, registration}),
           "an uninitialized shared coordinator must roll back all never-published detours");

    for (int failCreate : {1, 2, 3}) {
        AdapterFixture fixture;
        fixture.detours.failCreateAt = failCreate;
        DetourOwner owner{};
        Expect(InstallViaProductionAdapter(fixture, registration, registrationShim,
                                           cleanup, cleanupShim, &owner) ==
                   InstallResult::CreateFailed,
               "any create failure must fail the complete observer transaction");
        Expect(!owner.active && owner.registrationOriginal == 0u &&
                   owner.cleanupOriginal == 0u && owner.dispatchOriginal == 0u,
               "partial create failure must release portable ownership");
        if (failCreate == 2) {
            Expect(fixture.detours.removed == std::vector<uintptr_t>{registration},
                   "second create failure must remove the first created hook");
        }
        if (failCreate == 3) {
            Expect(fixture.detours.removed ==
                       std::vector<uintptr_t>({cleanup, registration}),
                   "third create failure must remove cleanup and registration in reverse order");
        }
    }

    AdapterFixture queueFailure;
    queueFailure.detours.failQueueEnableAt = 2;
    DetourOwner queueOwner{};
    Expect(InstallViaProductionAdapter(queueFailure, registration, registrationShim,
                                       cleanup, cleanupShim, &queueOwner) ==
               InstallResult::QueueFailed && !queueOwner.active &&
               queueOwner.applyAttempted && queueOwner.retainedInert &&
               queueFailure.detours.disabled.size() == 3u &&
               queueFailure.detours.removed.empty(),
           "partial queue failure must neutralize and retain all three created hooks");

    AdapterFixture applyFailure;
    applyFailure.detours.failApplyCalls = {1};
    applyFailure.detours.applyStartsCallback = true;
    applyFailure.detours.disableStartsLateCallback = true;
    DetourOwner applyOwner{};
    Expect(InstallViaProductionAdapter(applyFailure, registration, registrationShim,
                                       cleanup, cleanupShim, &applyOwner) ==
               InstallResult::CoordinatorPoisoned && !applyOwner.active &&
               applyOwner.applyAttempted && applyOwner.retainedInert &&
               applyOwner.coordinatorPoisoned &&
               applyOwner.registrationCreated && applyOwner.cleanupCreated &&
               applyOwner.dispatchCreated && applyFailure.detours.disabled.size() == 3u &&
               applyFailure.detours.removed.empty(),
           "partial-enable apply failure must neutralize and retain while poisoning retry until restart");
    Expect(applyFailure.drain.pausesBeforeDisable == 1 &&
               applyFailure.drain.pausesAfterDisable == 1 &&
               !applyFailure.detours.disableWhileAdmitted &&
               !applyFailure.detours.removeWhileCallbackActive,
           "apply rollback must drain both the pre-disable callback and a disable-race callback");

    AdapterFixture preDrainTimeout;
    preDrainTimeout.detours.failApplyCalls = {1};
    preDrainTimeout.detours.applyStartsCallback = true;
    preDrainTimeout.drain.clearBeforeDisable = false;
    DetourOwner preDrainOwner{};
    Expect(InstallViaProductionAdapter(preDrainTimeout, registration, registrationShim,
                                       cleanup, cleanupShim, &preDrainOwner) ==
               InstallResult::CoordinatorPoisoned && preDrainOwner.active &&
               preDrainOwner.applyAttempted && !preDrainOwner.retainedInert &&
               preDrainOwner.coordinatorPoisoned &&
               preDrainTimeout.detours.disabled.empty() &&
               preDrainTimeout.detours.removed.empty() &&
               !ObservationAdmitted(preDrainTimeout.lifecycle),
           "pre-disable drain timeout must retain ownership without touching either target");

    AdapterFixture postDrainTimeout;
    postDrainTimeout.detours.failApplyCalls = {1};
    postDrainTimeout.detours.applyStartsCallback = true;
    postDrainTimeout.detours.disableStartsLateCallback = true;
    postDrainTimeout.drain.clearAfterDisable = false;
    DetourOwner postDrainOwner{};
    Expect(InstallViaProductionAdapter(postDrainTimeout, registration, registrationShim,
                                       cleanup, cleanupShim, &postDrainOwner) ==
               InstallResult::CoordinatorPoisoned && !postDrainOwner.active &&
               postDrainOwner.applyAttempted && postDrainOwner.retainedInert &&
               postDrainOwner.coordinatorPoisoned &&
               postDrainOwner.registrationCreated && postDrainOwner.cleanupCreated &&
               postDrainOwner.dispatchCreated &&
               postDrainTimeout.detours.disabled.size() == 3u &&
               postDrainTimeout.detours.removed.empty(),
           "post-disable drain timeout must retain disabled hooks without freeing trampolines");

    AdapterFixture rollbackFailure;
    rollbackFailure.detours.failCreateAt = 2;
    rollbackFailure.detours.failRemoveAt = 1;
    DetourOwner rollbackOwner{};
    Expect(InstallViaProductionAdapter(rollbackFailure, registration, registrationShim,
                                       cleanup, cleanupShim, &rollbackOwner) ==
               InstallResult::RollbackFailed && rollbackOwner.registrationCreated &&
               rollbackOwner.registrationOriginal == registration + 0x100u,
           "failed rollback must retain the created hook and trampoline for later teardown");
    rollbackFailure.detours.failRemoveAt = 0;
    Expect(RemoveViaProductionAdapter(rollbackFailure, &rollbackOwner) ==
               TeardownResult::Removed &&
               !rollbackOwner.registrationCreated,
           "retained rollback ownership must permit a later normal-context teardown");
    Expect(rollbackFailure.detours.disabled.empty() &&
               !rollbackFailure.detours.unexpectedDisableTarget &&
               !rollbackFailure.detours.unexpectedRemoveTarget,
           "a never-applied retry must remove only the retained created target without disabling it");

    AdapterFixture prePoisonedRollback;
    FakeBatchIo poisonQueue;
    const std::array<uintptr_t, 2> poisonTargets = {0x30001000u, 0x30002000u};
    poisonQueue.created.assign(poisonTargets.begin(), poisonTargets.end());
    poisonQueue.failApplyCalls = {1, 2};
    Expect(FfxHooks::MinHookBatch::EnableBatch(
               &prePoisonedRollback.coordinator, BatchApi(poisonQueue),
               FfxHooks::MinHookBatch::Owner::FieldScout,
               poisonTargets.data(), poisonTargets.size()).result ==
               FfxHooks::MinHookBatch::BatchResult::Poisoned,
           "pre-poisoned rollback fixture must poison the shared coordinator first");
    prePoisonedRollback.detours.failRemoveAt = 1;
    DetourOwner prePoisonedOwner{};
    Expect(InstallViaProductionAdapter(
               prePoisonedRollback, registration, registrationShim,
               cleanup, cleanupShim, &prePoisonedOwner) ==
               InstallResult::RollbackFailed &&
               !prePoisonedOwner.coordinatorPoisoned &&
               (prePoisonedOwner.registrationCreated || prePoisonedOwner.cleanupCreated ||
                prePoisonedOwner.dispatchCreated),
           "an untouched owner must retain create-only retry authority when rollback fails behind a pre-poisoned coordinator");
    prePoisonedRollback.detours.failRemoveAt = 0;
    Expect(RemoveViaProductionAdapter(prePoisonedRollback, &prePoisonedOwner) ==
               TeardownResult::Removed,
           "a pre-poisoned coordinator must not block retry removal of an owner that never entered its queue");

    AdapterFixture cleanupOnly;
    DetourOwner cleanupOnlyOwner{};
    cleanupOnlyOwner.cleanupCreated = true;
    cleanupOnlyOwner.cleanupTarget = cleanup;
    cleanupOnlyOwner.cleanupOriginal = cleanup + 0x100u;
    cleanupOnly.detours.created.push_back(cleanup);
    Expect(RemoveViaProductionAdapter(cleanupOnly, &cleanupOnlyOwner) ==
               TeardownResult::Removed && cleanupOnly.detours.disabled.empty() &&
               cleanupOnly.detours.removed == std::vector<uintptr_t>{cleanup} &&
               !cleanupOnly.detours.unexpectedDisableTarget,
           "a never-applied cleanup-only owner must remain exactly removable on retry");

    AdapterFixture occupied;
    DetourOwner occupiedOwner{};
    occupiedOwner.registrationCreated = true;
    occupiedOwner.registrationTarget = registration;
    occupiedOwner.registrationOriginal = registration + 0x100u;
    Expect(InstallViaProductionAdapter(occupied, registration, registrationShim,
                                       cleanup, cleanupShim, &occupiedOwner) ==
               InstallResult::AlreadyOwned && occupied.detours.createCalls == 0 &&
               occupiedOwner.registrationCreated &&
               occupiedOwner.registrationOriginal == registration + 0x100u,
           "a repeated install must not erase unresolved detour ownership");

    AdapterFixture success;
    DetourOwner owner{};
    Expect(InstallViaProductionAdapter(success, registration, registrationShim,
                                       cleanup, cleanupShim, &owner) ==
               InstallResult::Installed && owner.active &&
               owner.registrationOriginal == registration + 0x100u &&
               owner.cleanupOriginal == cleanup + 0x100u &&
               owner.dispatchOriginal == 0x007ACAE0u,
           "successful transaction must publish all three trampolines only after one apply");
    Expect(RemoveViaProductionAdapter(success, &owner) == TeardownResult::RetainedInert &&
               !owner.active && owner.applyAttempted && owner.retainedInert &&
               owner.registrationCreated && owner.cleanupCreated && owner.dispatchCreated &&
               owner.registrationOriginal == registration + 0x100u &&
               owner.cleanupOriginal == cleanup + 0x100u &&
               owner.dispatchOriginal == 0x007ACAE0u &&
               success.detours.disabled.size() == 3u && success.detours.removed.empty(),
           "normal-context teardown must disable the set and retain every trampoline for process lifetime");
    Expect(RemoveViaProductionAdapter(success, &owner) == TeardownResult::RetainedInert &&
               owner.retainedInert && success.detours.disabled.size() == 6u &&
               success.detours.removed.empty(),
           "repeated retained teardown must remain idempotently inert without freeing ownership");
}

void TestDelayedMachinePrologueRetention() {
    using namespace FfxHooks::MonsterAiObserver;
    constexpr uintptr_t registration = 0x00784120u;
    constexpr uintptr_t cleanup = 0x00781660u;
    constexpr uintptr_t registrationShim = 0x20001000u;
    constexpr uintptr_t cleanupShim = 0x20002000u;
    constexpr uintptr_t dispatch = 0x007AC9E0u;

    AdapterFixture fixture;
    DetourOwner owner{};
    Expect(InstallViaProductionAdapter(fixture, registration, registrationShim,
                                       cleanup, cleanupShim, &owner) ==
               InstallResult::Installed,
           "delayed-prologue fixture must install the production adapter");

    // Model a CPU that already followed MinHook's target jump but is paused in the detour's
    // machine prologue, before the shim's first C++ statement can acquire CallbackLease.
    std::atomic<bool> enteredMachinePrologue{false};
    std::atomic<bool> resumeCppBody{false};
    std::atomic<bool> observedRetainedTrampoline{false};
    std::thread delayedEntrant([&]() {
        enteredMachinePrologue.store(true, std::memory_order_release);
        while (!resumeCppBody.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        CallbackLease callback(&fixture.lifecycle);
        observedRetainedTrampoline.store(
            owner.dispatchCreated && owner.dispatchOriginal == dispatch + 0x100u,
            std::memory_order_release);
    });

    size_t entrySpins = 0u;
    while (!enteredMachinePrologue.load(std::memory_order_acquire) &&
           entrySpins < 1000000u) {
        std::this_thread::yield();
        ++entrySpins;
    }
    const bool entered = enteredMachinePrologue.load(std::memory_order_acquire);
    Expect(entered && ActiveCallbacks(fixture.lifecycle) == 0u,
           "a machine-prologue entrant is intentionally invisible to the callback counter");
    if (!entered) {
        resumeCppBody.store(true, std::memory_order_release);
        delayedEntrant.join();
        return;
    }
    const TeardownResult result = RemoveViaProductionAdapter(fixture, &owner);
    Expect(result == TeardownResult::RetainedInert &&
               owner.retainedInert && fixture.detours.removed.empty(),
           "zero counted callbacks must not authorize freeing a possibly referenced trampoline");

    resumeCppBody.store(true, std::memory_order_release);
    delayedEntrant.join();
    Expect(observedRetainedTrampoline.load(std::memory_order_acquire) &&
               ActiveCallbacks(fixture.lifecycle) == 0u,
           "a delayed dispatcher entrant must safely resume through its retained process-lifetime trampoline");
}

void TestDispatcherIsTheThirdOwnedObserverTarget() {
    using namespace FfxHooks::MonsterAiObserver;
    constexpr uintptr_t registration = 0x00784120u;
    constexpr uintptr_t cleanup = 0x00781660u;
    constexpr uintptr_t dispatcher = 0x007AC9E0u;
    AdapterFixture fixture;
    DetourOwner owner{};

    Expect(InstallViaProductionAdapter(
               fixture, registration, 0x20001000u, cleanup, 0x20002000u, &owner) ==
               InstallResult::Installed && fixture.detours.createCalls == 3 &&
               owner.dispatchCreated && owner.dispatchTarget == dispatcher &&
               owner.dispatchOriginal == dispatcher + 0x100u,
           "dispatcher must be created as the third target in the existing observer transaction");
    Expect(fixture.detours.enabled ==
               std::vector<uintptr_t>({cleanup, registration, dispatcher}) &&
               fixture.detours.applyCalls == 1,
           "registration, cleanup, and dispatcher must become reachable through one owned apply");
    Expect(RemoveViaProductionAdapter(fixture, &owner) == TeardownResult::RetainedInert &&
               fixture.detours.disabled.size() == 3u && fixture.detours.removed.empty() &&
               owner.dispatchCreated && owner.dispatchOriginal == dispatcher + 0x100u,
           "teardown must disable the third target while retaining every applied trampoline");
}

struct FieldScoutStartDrain {
    FfxHooks::FieldScoutAdmission::State* state = nullptr;
    int pauses = 0;

    static void Pause(void* context, uint32_t) {
        auto& drain = *static_cast<FieldScoutStartDrain*>(context);
        ++drain.pauses;
        if (drain.pauses == 1 && drain.state) {
            FfxHooks::FieldScoutAdmission::ReleaseThreadStart(drain.state);
        }
    }
};

void TestFieldScoutStickyAdmissionAcrossAllShimFamilies() {
    using namespace FfxHooks::FieldScoutAdmission;
    constexpr std::array<ShimFamily, kShimFamilyCount> families = {
        ShimFamily::BuildTextureSlot,
        ShimFamily::GraphicFieldMapLoad,
        ShimFamily::LoadAndActivateDriver,
        ShimFamily::GetInstanceNameByIndex,
        ShimFamily::BattleEncounter,
        ShimFamily::WireInstanceToSceneNodes,
        ShimFamily::CommitInstanceMappings,
        ShimFamily::ChrSetWorldPosition,
        ShimFamily::TakaraLoad,
        ShimFamily::WarpActor,
        ShimFamily::SampleZoneSlot,
    };

    State delayedState{};
    InitializeClosed(&delayedState);
    Expect(Open(&delayedState),
           "a fresh FieldScout install may open admission exactly once");
    std::atomic<uint32_t> ready{0u};
    std::atomic<bool> resume{false};
    std::array<int, kShimFamilyCount> originalCalls{};
    std::array<int, kShimFamilyCount> captureSideEffects{};
    std::vector<std::thread> delayedEntrants;
    delayedEntrants.reserve(families.size());
    for (size_t index = 0u; index < families.size(); ++index) {
        delayedEntrants.emplace_back([&, index]() {
            // Model a target jump already taken while the CPU remains before the shim's first
            // C++ admission check. Teardown must win before these delayed entrants resume.
            ready.fetch_add(1u, std::memory_order_release);
            while (!resume.load(std::memory_order_acquire)) std::this_thread::yield();
            if (TryEnterAfterPrologue(&delayedState, families[index])) {
                ++captureSideEffects[index];
            }
            ++originalCalls[index];
        });
    }
    size_t spins = 0u;
    while (ready.load(std::memory_order_acquire) != families.size() &&
           spins < 1000000u) {
        std::this_thread::yield();
        ++spins;
    }
    const bool allDelayed = ready.load(std::memory_order_acquire) == families.size();
    Expect(allDelayed, "all eleven FieldScout shim families must reach the delayed-prologue barrier");
    RequestClose(&delayedState);
    Expect(!Open(&delayedState) && IsShuttingDown(delayedState),
           "process-sticky FieldScout shutdown must reject every attempted reopen");
    resume.store(true, std::memory_order_release);
    for (std::thread& entrant : delayedEntrants) entrant.join();

    bool everyOriginalOnce = true;
    bool noCaptureAfterClose = true;
    for (size_t index = 0u; index < families.size(); ++index) {
        everyOriginalOnce = everyOriginalOnce && originalCalls[index] == 1;
        noCaptureAfterClose = noCaptureAfterClose && captureSideEffects[index] == 0;
    }
    Expect(everyOriginalOnce && noCaptureAfterClose && IsShuttingDown(delayedState),
           "every delayed FieldScout family must pass through original exactly once with zero capture side effects");
    Expect(!ApplyPathTransition(&delayedState, PathTransition::ResumeField, false) &&
               ShouldSkipCapture(delayedState, false),
           "a field path observed after shutdown must never reopen sticky capture admission");

    State startState{};
    InitializeClosed(&startState);
    Expect(Open(&startState),
           "trace-thread fixture must open one fresh admission lifecycle");
    Expect(TryAcquireThreadStart(&startState),
           "an admitted FieldScout callback may reserve one trace-thread start");
    FieldScoutStartDrain drain{&startState, 0};
    Expect(CloseAndDrainThreadStarts(
               &startState, {&drain, &FieldScoutStartDrain::Pause, 4u}) &&
               drain.pauses == 1 && !TryAcquireThreadStart(&startState),
           "sticky close must drain an in-flight thread start and reject every later restart");
}

void TestRequestedStatusTruth() {
    using FfxHooks::F7AiObserverStatus;
    Expect(FfxHooks::F7AiSwap_TestResolveStatus(F7AiObserverStatus::Off, true) ==
               F7AiObserverStatus::ObservePendingRestart,
           "an ON request without an installed observer must report pending restart");
    Expect(FfxHooks::F7AiSwap_TestResolveStatus(F7AiObserverStatus::Off, false) ==
               F7AiObserverStatus::Off,
           "an OFF request without an observer must remain OFF");
    Expect(FfxHooks::F7AiSwap_TestResolveStatus(F7AiObserverStatus::Unavailable, true) ==
               F7AiObserverStatus::Unavailable,
           "a failed compatibility validation must remain unavailable instead of pending");
    Expect(FfxHooks::F7AiSwap_TestResolveStatus(F7AiObserverStatus::RetainedInert, true) ==
               F7AiObserverStatus::RetainedInert,
           "a retained inert observer must remain explicit even while configuration requests ON");
    Expect(FfxHooks::F7AiSwap_TestResolveSetupFailureStatus(
               F7AiObserverStatus::Off, true) == F7AiObserverStatus::Unavailable,
           "a requested observer must publish unavailable after shared MinHook setup failure");
    Expect(FfxHooks::F7AiSwap_TestResolveSetupFailureStatus(
               F7AiObserverStatus::Off, false) == F7AiObserverStatus::Off,
           "an unrequested observer must remain off after unrelated shared MinHook setup failure");

    FfxHooks::MonsterAiObserver::DetourOwner unresolved{};
    unresolved.active = true;
    unresolved.coordinatorPoisoned = true;
    Expect(FfxHooks::F7AiSwap_TestResolveInstallFailureStatus(
               FfxHooks::MonsterAiObserver::InstallResult::CoordinatorPoisoned,
               unresolved) == F7AiObserverStatus::Stopping,
           "a partially reachable failed install must report stopping instead of unavailable");

    FfxHooks::MonsterAiObserver::DetourOwner retained{};
    retained.retainedInert = true;
    retained.coordinatorPoisoned = true;
    Expect(FfxHooks::F7AiSwap_TestResolveInstallFailureStatus(
               FfxHooks::MonsterAiObserver::InstallResult::CoordinatorPoisoned,
               retained) == F7AiObserverStatus::RetainedInert,
           "an exactly disabled failed install must report retained inert ownership");
}

void TestStaticMutationBoundary() {
    using namespace FfxHooks::MonsterAiObserver;
    Expect(kValidatedSwapPairs.empty() && kValidatedSwapPairCount == 0u,
           "the Monster AI mutation whitelist must remain empty");
    ReadOnlyMemory memory{};
    Expect(memory.read == nullptr,
           "the portable observer surface must expose a reader and no writer callback");
}

} // namespace

int main() {
    TestRelocatedLoadedSignatures();
    TestProcessGlobalMinHookInitialization();
    TestProcessGlobalMinHookCoordinator();
    TestRetainedMonsterObserverOwnerSurvivesFieldScoutLifecycle();
    TestSnapshotBoundsAndPrivacy();
    TestInvalidAndDuplicateSlots();
    TestLifecycleAndOriginalCallContract();
    TestReentrancyAndStopAdmission();
    TestTransactionalDetourOwnership();
    TestDelayedMachinePrologueRetention();
    TestDispatcherIsTheThirdOwnedObserverTarget();
    TestFieldScoutStickyAdmissionAcrossAllShimFamilies();
    TestRequestedStatusTruth();
    TestStaticMutationBoundary();
    std::printf("Monster AI observer RT0/RT1: %d/%d checks passed\n",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
