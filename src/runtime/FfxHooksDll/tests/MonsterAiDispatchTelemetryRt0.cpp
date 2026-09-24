#include "../hooks/MonsterAiDispatchTelemetry.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace FfxHooks {

int32_t F7AiSwap_TestObserveDispatchAdapter(
    MonsterAiObserver::LifecycleState*,
    MonsterAiDispatchTelemetry::DispatchTelemetryState*,
    uint32_t threadId, uint32_t callerReturnPreferredVa,
    int32_t actorIndex, int32_t commandStack32, uint32_t targetMask,
    int32_t force, int32_t n64,
    const MonsterAiDispatchTelemetry::DispatchEventSink&,
    const MonsterAiDispatchTelemetry::DispatchOriginalCall&);

} // namespace FfxHooks

namespace {

using FfxHooks::MonsterAiDispatchTelemetry::DispatchEvent;
using FfxHooks::MonsterAiDispatchTelemetry::DispatchEventSink;
using FfxHooks::MonsterAiDispatchTelemetry::DispatchOriginalCall;
using FfxHooks::MonsterAiDispatchTelemetry::DispatchTelemetryState;
using FfxHooks::MonsterAiObserver::CallbackLease;
using FfxHooks::MonsterAiObserver::LifecycleState;
using FfxHooks::MonsterAiObserver::Snapshot;
using FfxHooks::MonsterAiShadow::DecisionReason;

int g_checks = 0;
int g_failures = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

struct OriginalProbe {
    int calls = 0;
    int32_t actorIndex = 0;
    int32_t commandStack32 = 0;
    uint32_t targetMask = 0;
    int32_t force = 0;
    int32_t n64 = 0;
    int32_t returnValue = 0;
    LifecycleState* stopDuringCall = nullptr;
};

int32_t InvokeOriginal(void* context, int32_t actorIndex, int32_t commandStack32,
                       uint32_t targetMask, int32_t force, int32_t n64) {
    auto& probe = *static_cast<OriginalProbe*>(context);
    ++probe.calls;
    probe.actorIndex = actorIndex;
    probe.commandStack32 = commandStack32;
    probe.targetMask = targetMask;
    probe.force = force;
    probe.n64 = n64;
    if (probe.stopDuringCall) FfxHooks::MonsterAiObserver::RequestStop(probe.stopDuringCall);
    return probe.returnValue;
}

DispatchOriginalCall Original(OriginalProbe& probe) {
    return {&probe, &InvokeOriginal};
}

struct EventLog {
    std::mutex mutex;
    std::vector<DispatchEvent> events;
};

void EmitEvent(void* context, const DispatchEvent& event) {
    auto& log = *static_cast<EventLog*>(context);
    const std::lock_guard<std::mutex> lock(log.mutex);
    log.events.push_back(event);
}

DispatchEventSink Sink(EventLog& log) {
    return {&log, &EmitEvent};
}

Snapshot CandidateSnapshot(uint32_t generation) {
    Snapshot snapshot{};
    snapshot.generation = generation;
    snapshot.listValid = true;
    auto& slot = snapshot.slots[0];
    slot.slot = 0u;
    slot.monsterId = FfxHooks::MonsterAiShadow::kCandidateMonsterRawId;
    slot.valid = true;
    slot.aiLength = FfxHooks::MonsterAiShadow::kCandidateAiLength;
    slot.aiHash = FfxHooks::MonsterAiShadow::kCandidateAiFnv1a64;
    slot.workerLength = FfxHooks::MonsterAiShadow::kCandidateWorkerLength;
    slot.workerHash = FfxHooks::MonsterAiShadow::kCandidateWorkerFnv1a64;
    return snapshot;
}

void Prepare(LifecycleState* lifecycle, DispatchTelemetryState* telemetry,
             uint32_t generation) {
    FfxHooks::MonsterAiObserver::InitializeLifecycle(lifecycle);
    FfxHooks::MonsterAiObserver::OpenAdmission(lifecycle);
    lifecycle->activeGeneration.store(generation, std::memory_order_release);
    FfxHooks::MonsterAiDispatchTelemetry::Initialize(telemetry);
    FfxHooks::MonsterAiDispatchTelemetry::PublishSnapshot(
        telemetry, CandidateSnapshot(generation));
}

int32_t Observe(LifecycleState* lifecycle, DispatchTelemetryState* telemetry,
                uint32_t threadId, uint32_t callerReturnPreferredVa,
                int32_t actorIndex, int32_t commandStack32, uint32_t targetMask,
                int32_t force, int32_t n64, EventLog& log, OriginalProbe& original) {
    return FfxHooks::F7AiSwap_TestObserveDispatchAdapter(
        lifecycle, telemetry, threadId, callerReturnPreferredVa,
        actorIndex, commandStack32, targetMask, force, n64,
        Sink(log), Original(original));
}

void TestPhysicalAbiIsTransparentAndQueueReadbackIsCorrelated() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 17u);
    EventLog log;
    OriginalProbe original{};
    original.returnValue = -73;

    const int32_t result = Observe(
        &lifecycle, &telemetry, 91u,
        FfxHooks::MonsterAiShadow::kNormalPerformReturnPreferredVa,
        20, static_cast<int32_t>(0xBEEF4100u), 0xA5A55AA5u, -1, 73,
        log, original);

    Expect(result == -73 && original.calls == 1,
           "dispatch observer must return the exact queue readback after one original call");
    Expect(original.actorIndex == 20 &&
               static_cast<uint32_t>(original.commandStack32) == 0xBEEF4100u &&
               original.targetMask == 0xA5A55AA5u && original.force == -1 &&
               original.n64 == 73,
           "the physical five-DWORD dispatcher ABI must reach original bit-for-bit unchanged");
    Expect(log.events.size() == 1u,
           "one admitted dispatcher call must publish exactly one bounded event");
    if (log.events.size() == 1u) {
        const DispatchEvent& event = log.events[0];
        Expect(event.generation == 17u && event.threadId == 91u &&
                   event.serial == 1u && event.actorSlot == 0,
               "event correlation must include generation, thread, actor slot, and serial");
        Expect(event.command == 0x4100u && event.targetMask == 0xA5A55AA5u &&
                   event.force == -1 && event.queueReadback == -73,
               "event telemetry must retain bounded command/target/force and queue readback");
        Expect(event.reason == DecisionReason::ForceArgumentSet &&
                   !event.proposalAvailable,
               "nonzero force must fail closed without changing the forwarded command");
    }
}

void TestRegisteredIdentityFeedsTheExistingShadowDecision() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 23u);
    EventLog log;
    OriginalProbe original{};
    original.returnValue = 41;

    const int32_t result = Observe(
        &lifecycle, &telemetry, 101u,
        FfxHooks::MonsterAiShadow::kNormalPerformReturnPreferredVa,
        20, 0x4100, 0x00000004u, 0, 0, log, original);

    Expect(result == 41 && original.calls == 1 && log.events.size() == 1u,
           "recognized dispatch must remain a transparent one-call observer");
    if (log.events.size() == 1u) {
        const DispatchEvent& event = log.events[0];
        Expect(event.reason == DecisionReason::CandidateProposalRecorded &&
                   event.proposalAvailable && event.proposedCommand == 0x4127u &&
                   event.monsterRawId == 0x0156u,
               "the registered value-only identity must feed the existing shadow proposal core");
    }
}

void TestRepeatedEventsArePreservedWithUniqueSerials() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 29u);
    EventLog log;
    OriginalProbe original{};
    original.returnValue = 7;

    for (int index = 0; index < 3; ++index) {
        (void)Observe(
            &lifecycle, &telemetry, 111u,
            FfxHooks::MonsterAiShadow::kNormalPerformReturnPreferredVa,
            20, 0x4100, 1u, 0, 0, log, original);
    }

    Expect(original.calls == 3 && log.events.size() == 3u,
           "identical dispatches must never be deduplicated or skip original");
    if (log.events.size() == 3u) {
        Expect(log.events[0].serial == 1u && log.events[1].serial == 2u &&
                   log.events[2].serial == 3u,
               "repeated events must retain distinct monotonically increasing serials");
    }
}

void TestGenerationMismatchAndRetirementFailClosed() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 31u);
    lifecycle.activeGeneration.store(32u, std::memory_order_release);
    EventLog log;
    OriginalProbe original{};

    (void)Observe(
        &lifecycle, &telemetry, 121u,
        FfxHooks::MonsterAiShadow::kNormalPerformReturnPreferredVa,
        20, 0x4100, 2u, 0, 0, log, original);
    Expect(original.calls == 1 && log.events.size() == 1u &&
               log.events[0].generation == 32u &&
               log.events[0].reason == DecisionReason::MonsterNotCandidate &&
               !log.events[0].proposalAvailable,
           "a reused actor slot with stale identity must fail closed for the new generation");

    FfxHooks::MonsterAiDispatchTelemetry::PublishSnapshot(
        &telemetry, CandidateSnapshot(32u));
    FfxHooks::MonsterAiDispatchTelemetry::RetireGeneration(&telemetry, 32u);
    (void)Observe(
        &lifecycle, &telemetry, 122u,
        FfxHooks::MonsterAiShadow::kNormalPerformReturnPreferredVa,
        20, 0x4100, 2u, 0, 0, log, original);
    Expect(original.calls == 2 && log.events.size() == 2u &&
               log.events[1].reason == DecisionReason::MonsterNotCandidate,
           "cleanup retirement must remove proposal identity without blocking vanilla dispatch");
}

void TestCloseAndDelayedProloguePassThrough() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 37u);
    EventLog log;
    OriginalProbe original{};
    original.returnValue = 99;

    FfxHooks::MonsterAiObserver::RequestStop(&lifecycle);
    // This models a CPU that followed the target jump before disable and only now reaches the
    // shim's first C++ statement. The retained trampoline must remain callable, but telemetry is
    // process-sticky closed.
    const int32_t result = Observe(
        &lifecycle, &telemetry, 131u,
        FfxHooks::MonsterAiShadow::kNormalPerformReturnPreferredVa,
        20, 0x4100, 4u, 0, 0, log, original);
    Expect(result == 99 && original.calls == 1 && log.events.empty(),
           "a delayed post-close entrant must pass through original once with no telemetry");
}

void TestStopRacingOriginalCompletesTheAdmittedEvent() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 41u);
    EventLog log;
    OriginalProbe original{};
    original.returnValue = 123;
    original.stopDuringCall = &lifecycle;

    const int32_t result = Observe(
        &lifecycle, &telemetry, 141u,
        FfxHooks::MonsterAiShadow::kForceDispatchReturnPreferredVa,
        20, 0x4100, 8u, 1, 0, log, original);
    Expect(result == 123 && original.calls == 1 && log.events.size() == 1u &&
               !FfxHooks::MonsterAiObserver::ObservationAdmitted(lifecycle),
           "an admitted callback must finish one correlated event when stop races original");
    if (log.events.size() == 1u) {
        Expect(log.events[0].reason == DecisionReason::ForceDispatchCaller,
               "force caller classification must survive a stop racing queue readback");
    }
}

void TestConcurrentRepeatedEventsRetainOneSerialEach() {
    LifecycleState lifecycle{};
    DispatchTelemetryState telemetry{};
    Prepare(&lifecycle, &telemetry, 43u);
    EventLog log;
    constexpr size_t kThreadCount = 8u;
    std::vector<std::thread> threads;
    std::array<OriginalProbe, kThreadCount> originals{};
    threads.reserve(kThreadCount);
    for (size_t index = 0u; index < kThreadCount; ++index) {
        threads.emplace_back([&, index]() {
            originals[index].returnValue = static_cast<int32_t>(index);
            (void)Observe(
                &lifecycle, &telemetry, static_cast<uint32_t>(200u + index),
                FfxHooks::MonsterAiShadow::kDeathOverrideReturnPreferredVa,
                20, 0x4100, static_cast<uint32_t>(1u << index), 1, 0,
                log, originals[index]);
        });
    }
    for (std::thread& thread : threads) thread.join();

    std::vector<uint32_t> serials;
    for (const DispatchEvent& event : log.events) serials.push_back(event.serial);
    std::sort(serials.begin(), serials.end());
    bool everyOriginalOnce = true;
    for (const OriginalProbe& original : originals) {
        everyOriginalOnce = everyOriginalOnce && original.calls == 1;
    }
    Expect(everyOriginalOnce && serials.size() == kThreadCount,
           "every concurrent repeated dispatcher callback must call original and emit once");
    for (size_t index = 0u; index < serials.size(); ++index) {
        Expect(serials[index] == index + 1u,
               "concurrent dispatcher telemetry must reserve one unique nonzero serial");
    }
}

} // namespace

int main() {
    TestPhysicalAbiIsTransparentAndQueueReadbackIsCorrelated();
    TestRegisteredIdentityFeedsTheExistingShadowDecision();
    TestRepeatedEventsArePreservedWithUniqueSerials();
    TestGenerationMismatchAndRetirementFailClosed();
    TestCloseAndDelayedProloguePassThrough();
    TestStopRacingOriginalCompletesTheAdmittedEvent();
    TestConcurrentRepeatedEventsRetainOneSerialEach();
    std::fprintf(stderr, "Monster AI dispatch telemetry RT0/RT1: %d/%d checks passed\n",
                 g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
