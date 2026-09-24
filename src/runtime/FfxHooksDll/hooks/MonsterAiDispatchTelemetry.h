#pragma once

#include "MonsterAiDispatchShadow.h"
#include "MonsterAiObserverCore.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace FfxHooks::MonsterAiDispatchTelemetry {

// Every published field is atomic because registration and command dispatch may run on different
// game threads. The sequence word provides a bounded coherent snapshot without storing or exposing
// any actor/script pointer. Split hashes keep the value-only cache well-defined on x86 as well.
struct PublishedSlot {
    std::atomic<uint32_t> sequence{0u};
    std::atomic<uint32_t> generation{0u};
    std::atomic<uint32_t> rawId{0u};
    std::atomic<uint32_t> aiLength{0u};
    std::atomic<uint32_t> aiHashLow{0u};
    std::atomic<uint32_t> aiHashHigh{0u};
    std::atomic<uint32_t> workerLength{0u};
    std::atomic<uint32_t> workerHashLow{0u};
    std::atomic<uint32_t> workerHashHigh{0u};
};

struct DispatchTelemetryState {
    std::atomic<uint32_t> serial{0u};
    std::array<PublishedSlot, MonsterAiObserver::kActorSlotCount> slots{};
};

// This is the complete production log contract. It contains only bounded scalar values; runtime
// return addresses, actor pointers, file pointers, and script bytes never cross this boundary.
struct DispatchEvent {
    uint32_t generation = 0u;
    uint32_t threadId = 0u;
    uint32_t serial = 0u;
    int32_t actorSlot = -1;
    uint16_t monsterRawId = 0u;
    uint16_t command = 0u;
    uint32_t targetMask = 0u;
    int32_t force = 0;
    int32_t queueReadback = 0;
    MonsterAiShadow::DecisionReason reason = MonsterAiShadow::DecisionReason::ShadowDisabled;
    bool proposalAvailable = false;
    uint16_t proposedCommand = 0u;
};

struct DispatchEventSink {
    void* context = nullptr;
    void (*emit)(void*, const DispatchEvent&) = nullptr;
};

struct DispatchOriginalCall {
    void* context = nullptr;
    int32_t (*invoke)(void*, int32_t actorIndex, int32_t commandStack32,
                      uint32_t targetMask, int32_t force, int32_t n64) = nullptr;
};

void Initialize(DispatchTelemetryState*) noexcept;
void PublishSnapshot(DispatchTelemetryState*, const MonsterAiObserver::Snapshot&) noexcept;
void RetireGeneration(DispatchTelemetryState*, uint32_t generation) noexcept;

// The physical x86 shim owns CallbackLease and supplies the normalized preferred-image caller.
// This adapter calls vanilla exactly once with the original five DWORD arguments, then attaches
// its unmodified return value as queue readback. It has no mutation-capable return channel.
int32_t ObserveDispatch(
    MonsterAiObserver::CallbackLease&, DispatchTelemetryState*, uint32_t threadId,
    uint32_t callerReturnPreferredVa, int32_t actorIndex, int32_t commandStack32,
    uint32_t targetMask, int32_t force, int32_t n64, const DispatchEventSink&,
    const DispatchOriginalCall&) noexcept;

} // namespace FfxHooks::MonsterAiDispatchTelemetry
