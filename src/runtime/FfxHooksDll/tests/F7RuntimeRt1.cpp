#include "../hooks/F7InLive.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

using namespace FfxHooks::F7Difficulty;
using namespace FfxHooks;

constexpr size_t kGuardBytes = 32;
constexpr int kIterations = 1200;

int g_checks = 0;
int g_failures = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

struct ActorStorage {
    std::array<uint8_t, kGuardBytes + kActorSpan + kGuardBytes> bytes{};
};

struct ConcurrentRuntime {
    SRWLOCK lock = SRWLOCK_INIT;
    Runtime runtime{};
    ActorStorage actor{};
    uint64_t generation = 0;
    std::atomic<int> operations{0};
    std::atomic<int> publications{0};
    std::atomic<int> faults{0};
    std::atomic<int> hybridSnapshots{0};
    int failCurrentHpWrites = 0;
    int failCurrentMpWrites = 0;
    int failMaxHpWrites = 0;
    int failMaxMpWrites = 0;
    int successfulNoopMaxHpWrites = 0;
    int successfulNoopCurrentHpWrites = 0;
    int failCurrentHpReadbacksAfterWrite = 0;
    int failCurrentMpReadbacksAfterWrite = 0;
    bool currentHpReadbackPending = false;
    bool currentMpReadbackPending = false;
    bool failMaxHpReadsAfterWrite = false;
    bool failMaxMpReadsAfterWrite = false;
    bool maxHpWriteObserved = false;
    bool maxMpWriteObserved = false;
    int memoryWrites = 0;
    void* nativeAutoRemove = nullptr;
    void* nativeAutoApply = nullptr;
    int autoRefreshes = 0;
};

uintptr_t ActorAddress(ConcurrentRuntime& state) {
    return reinterpret_cast<uintptr_t>(state.actor.bytes.data() + kGuardBytes);
}

bool ResolveActorRange(
    ConcurrentRuntime& state, uintptr_t address, size_t width, size_t* offsetOut) {
    const uintptr_t base = ActorAddress(state);
    if (address < base || width > kActorSpan) return false;
    const uintptr_t relative = address - base;
    if (relative > kActorSpan - width) return false;
    *offsetOut = static_cast<size_t>(relative);
    return true;
}

bool ReadMemory(void* context, uintptr_t address, void* output, size_t width) {
    ConcurrentRuntime& state = *static_cast<ConcurrentRuntime*>(context);
    size_t offset = 0;
    if (!output || !ResolveActorRange(state, address, width, &offset)) return false;
    if (offset == kMaxHpOffset && state.failMaxHpReadsAfterWrite &&
        state.maxHpWriteObserved) {
        return false;
    }
    if (offset == kMaxMpOffset && state.failMaxMpReadsAfterWrite &&
        state.maxMpWriteObserved) {
        return false;
    }
    if (offset == kCurrentHpOffset && state.currentHpReadbackPending) {
        state.currentHpReadbackPending = false;
        --state.failCurrentHpReadbacksAfterWrite;
        return false;
    }
    if (offset == kCurrentMpOffset && state.currentMpReadbackPending) {
        state.currentMpReadbackPending = false;
        --state.failCurrentMpReadbacksAfterWrite;
        return false;
    }
    std::memcpy(output, state.actor.bytes.data() + kGuardBytes + offset, width);
    return true;
}

bool WriteMemory(void* context, uintptr_t address, const void* input, size_t width) {
    ConcurrentRuntime& state = *static_cast<ConcurrentRuntime*>(context);
    size_t offset = 0;
    if (!input || !ResolveActorRange(state, address, width, &offset)) return false;
    ++state.memoryWrites;
    if (offset == kMaxHpOffset && state.failMaxHpWrites > 0) {
        --state.failMaxHpWrites;
        return false;
    }
    if (offset == kMaxMpOffset && state.failMaxMpWrites > 0) {
        --state.failMaxMpWrites;
        return false;
    }
    if (offset == kCurrentHpOffset && state.failCurrentHpWrites > 0) {
        --state.failCurrentHpWrites;
        return false;
    }
    if (offset == kCurrentMpOffset && state.failCurrentMpWrites > 0) {
        --state.failCurrentMpWrites;
        return false;
    }
    if (offset == kMaxHpOffset && state.successfulNoopMaxHpWrites > 0) {
        --state.successfulNoopMaxHpWrites;
        return true;
    }
    if (offset == kCurrentHpOffset && state.successfulNoopCurrentHpWrites > 0) {
        --state.successfulNoopCurrentHpWrites;
        return true;
    }
    std::memcpy(state.actor.bytes.data() + kGuardBytes + offset, input, width);
    if (offset == kMaxHpOffset) state.maxHpWriteObserved = true;
    if (offset == kMaxMpOffset) state.maxMpWriteObserved = true;
    if (offset == kCurrentHpOffset && state.failCurrentHpReadbacksAfterWrite > 0) {
        state.currentHpReadbackPending = true;
    }
    if (offset == kCurrentMpOffset && state.failCurrentMpReadbacksAfterWrite > 0) {
        state.currentMpReadbackPending = true;
    }
    return true;
}

template <typename T>
void StoreActorValue(ConcurrentRuntime& state, size_t offset, T value) {
    std::memcpy(state.actor.bytes.data() + kGuardBytes + offset, &value, sizeof(value));
}

template <typename T>
T LoadActorValue(const ConcurrentRuntime& state, size_t offset) {
    T value{};
    std::memcpy(&value, state.actor.bytes.data() + kGuardBytes + offset, sizeof(value));
    return value;
}

void ResetVanillaActor(ConcurrentRuntime& state) {
    std::memset(state.actor.bytes.data() + kGuardBytes, 0, kActorSpan);
    StoreActorValue<uint16_t>(state, kFormationIdOffset, 0x0123u);
    StoreActorValue<uint32_t>(state, kMaxHpOffset, 1000u);
    StoreActorValue<uint32_t>(state, kMaxMpOffset, 100u);
    StoreActorValue<uint32_t>(state, kOverkillOffset, 500u);
    for (size_t i = 0; i < kStatCount; ++i) {
        StoreActorValue<uint8_t>(state, kStatOffsets[i], static_cast<uint8_t>(10 + i));
    }
    StoreActorValue<uint32_t>(state, kCurrentHpOffset, 500u);
    StoreActorValue<uint32_t>(state, kCurrentMpOffset, 50u);
}

DifficultyConfig MakeTaggedConfig(int tag) {
    DifficultyConfig config = MakeNeutralConfig();
    config.global.enabled = true;
    config.global.hpMul = tag;
    config.global.mpMul = tag + 1;
    config.global.strMul = 1000 + tag / 10;
    config.global.defMul = 1001 + tag / 10;
    config.global.magMul = 1002 + tag / 10;
    config.global.mdfMul = 1003 + tag / 10;
    config.global.agiMul = 1004 + tag / 10;
    config.global.accMul = 1005 + tag / 10;
    config.global.evaMul = 1006 + tag / 10;
    config.global.lckMul = 1007 + tag / 10;
    config.global.overkillMul = tag + 2;
    config.byArea = true;
    config.areaCount = kMaxAreaRules;
    for (size_t i = 0; i < kMaxAreaRules; ++i) {
        AreaRule& area = config.areas[i];
        area.enabled = true;
        area.fieldRow = tag * 10 + static_cast<int32_t>(i);
        area.preset = config.global;
        area.preset.hpMul = tag + static_cast<int32_t>(i);
        area.preset.mpMul = tag + static_cast<int32_t>(i) + 1;
    }
    return config;
}

bool IsTaggedConfig(const DifficultyConfig& config, int tag) {
    if (!config.global.enabled || config.global.hpMul != tag ||
        config.global.mpMul != tag + 1 || config.global.overkillMul != tag + 2 ||
        !config.byArea || config.areaCount != kMaxAreaRules) {
        return false;
    }
    for (size_t i = 0; i < kMaxAreaRules; ++i) {
        const AreaRule& area = config.areas[i];
        if (!area.enabled || area.fieldRow != tag * 10 + static_cast<int32_t>(i) ||
            area.preset.hpMul != tag + static_cast<int32_t>(i) ||
            area.preset.mpMul != tag + static_cast<int32_t>(i) + 1) {
            return false;
        }
    }
    return true;
}

bool IsNeutralInvalidSnapshot(const DifficultyConfig& config, bool valid) {
    return !valid && !config.global.enabled && !config.byArea && config.areaCount == 0;
}

bool IsKnownSnapshot(const DifficultyConfig& config, bool valid) {
    if (IsNeutralInvalidSnapshot(config, valid)) return true;
    return valid && (IsTaggedConfig(config, 1500) || IsTaggedConfig(config, 2200) ||
                     IsTaggedConfig(config, 3300));
}

struct DifficultyPublication {
    DifficultyConfig config{};
    bool valid = false;
};

void ApplyDifficultyPublication(
    F7Config*, DifficultyConfig* difficulty, bool* difficultyValid,
    void* context) {
    const DifficultyPublication& publication =
        *static_cast<const DifficultyPublication*>(context);
    *difficulty = publication.config;
    *difficultyValid = publication.valid;
}

void ApplyProductionConfigTag(
    F7Config* config, DifficultyConfig* difficulty, bool* difficultyValid,
    void* context) {
    const int tag = static_cast<int>(reinterpret_cast<intptr_t>(context));
    *difficulty = MakeTaggedConfig(tag);
    *difficultyValid = true;
    config->music.lockTrack = tag;
    config->music.battleTrack = tag + 1;
    config->music.randomizer = (tag & 1) != 0;
    config->music.fadeFrames = tag + 2;
    config->music.playlistCount = 2;
    config->music.playlist[0] = tag + 3;
    config->music.playlist[1] = tag + 4;
    config->force.lastField = tag * 10;
    config->force.lastGroup = tag * 10 + 1;
    config->force.lastFormation = tag * 10 + 2;
    config->force.hasLast = true;
    config->force.repeatCount = (tag % 9) + 1;
}

bool IsProductionConfigTag(const F7ConfigStateSnapshot& snapshot, int tag) {
    return snapshot.difficultyValid && IsTaggedConfig(snapshot.difficulty, tag) &&
           snapshot.config.diffGlobal.hpMul == tag &&
           snapshot.config.diffGlobal.mpMul == tag + 1 &&
           snapshot.config.diffByArea &&
           snapshot.config.areaCount == static_cast<int>(kMaxAreaRules) &&
           snapshot.config.music.lockTrack == tag &&
           snapshot.config.music.battleTrack == tag + 1 &&
           snapshot.config.music.fadeFrames == tag + 2 &&
           snapshot.config.music.playlistCount == 2 &&
           snapshot.config.music.playlist[0] == tag + 3 &&
           snapshot.config.music.playlist[1] == tag + 4 &&
           snapshot.config.force.lastField == tag * 10 &&
           snapshot.config.force.lastGroup == tag * 10 + 1 &&
           snapshot.config.force.lastFormation == tag * 10 + 2 &&
           snapshot.config.force.hasLast;
}

template <typename Operation>
std::thread StartWorker(
    std::atomic<int>& ready, std::atomic<bool>& start, Operation operation);

void TestProductionConfigSnapshotApis() {
    F7Config initial{};
    DifficultyConfig neutral = MakeNeutralConfig();
    F7_ReplaceConfigSnapshot(initial, neutral, true);
    F7_UpdateConfigSnapshot(
        &ApplyProductionConfigTag, reinterpret_cast<void*>(static_cast<intptr_t>(1500)));

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> torn{0};
    std::thread writerA = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations; ++i) {
            const int tag = (i & 1) == 0 ? 1500 : 2200;
            F7_UpdateConfigSnapshot(
                &ApplyProductionConfigTag,
                reinterpret_cast<void*>(static_cast<intptr_t>(tag)));
        }
    });
    std::thread writerB = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations; ++i) {
            F7_UpdateConfigSnapshot(
                &ApplyProductionConfigTag,
                reinterpret_cast<void*>(static_cast<intptr_t>(3300)));
        }
    });
    std::thread readerA = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations * 2; ++i) {
            const F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
            if (!IsProductionConfigTag(snapshot, 1500) &&
                !IsProductionConfigTag(snapshot, 2200) &&
                !IsProductionConfigTag(snapshot, 3300)) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    std::thread readerB = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations * 2; ++i) {
            const F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
            if (!IsProductionConfigTag(snapshot, 1500) &&
                !IsProductionConfigTag(snapshot, 2200) &&
                !IsProductionConfigTag(snapshot, 3300)) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    while (ready.load(std::memory_order_acquire) != 4) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    writerA.join();
    writerB.join();
    readerA.join();
    readerB.join();

    const F7ConfigStateSnapshot finalSnapshot = F7_GetConfigSnapshot();
    Expect(torn.load(std::memory_order_relaxed) == 0,
           "actual production config APIs must never expose a torn UI/runtime snapshot");
    Expect(finalSnapshot.revision >= static_cast<uint64_t>(kIterations * 2 + 2),
           "actual production config publication must advance one revision per atomic edit");
    Expect(IsProductionConfigTag(finalSnapshot, 1500) ||
               IsProductionConfigTag(finalSnapshot, 2200) ||
               IsProductionConfigTag(finalSnapshot, 3300),
           "actual production config API must finish on one complete published generation");
}

void TestSinRamTypedConfigSnapshotApis() {
    F7Config initial{};
    DifficultyConfig neutral = MakeNeutralConfig();
    const SinRam::Config invalidRequested{true, 2};
    F7_ReplaceConfigSnapshot(initial, neutral, true, invalidRequested, false);

    F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
    Expect(!snapshot.sinRamValid && !snapshot.sinRam.enabled &&
               snapshot.sinRam.threatLevel == 0,
           "an invalid parsed S.I.N. member must publish only OFF/T0 without invalidating Difficulty");
    Expect(snapshot.difficultyValid,
           "S.I.N. validity must remain independent from Difficulty validity");

    const uint64_t invalidRevision = snapshot.revision;
    Expect(!F7_SetSinRamConfig(SinRam::Config{true, 3}),
           "the typed S.I.N. edit API must reject out-of-range threat levels");
    snapshot = F7_GetConfigSnapshot();
    Expect(snapshot.revision == invalidRevision && !snapshot.sinRamValid &&
               !snapshot.sinRam.enabled && snapshot.sinRam.threatLevel == 0,
           "a rejected S.I.N. edit must not publish a partial or falsely valid revision");

    Expect(F7_SetSinRamConfig(SinRam::Config{true, 1}),
           "the typed S.I.N. edit API must accept the exact enabled/T1 value");
    snapshot = F7_GetConfigSnapshot();
    Expect(snapshot.sinRamValid && snapshot.sinRam.enabled &&
               snapshot.sinRam.threatLevel == 1 && snapshot.difficultyValid,
           "a valid S.I.N. edit must atomically publish its two fields without touching Difficulty");

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> torn{0};
    std::thread writerA = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations; ++i) {
            F7_SetSinRamConfig(SinRam::Config{true, 1});
        }
    });
    std::thread writerB = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations; ++i) {
            F7_SetSinRamConfig(SinRam::Config{false, 2});
        }
    });
    std::thread reader = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations * 2; ++i) {
            const F7ConfigStateSnapshot observed = F7_GetConfigSnapshot();
            const bool first = observed.sinRamValid && observed.sinRam.enabled &&
                observed.sinRam.threatLevel == 1;
            const bool second = observed.sinRamValid && !observed.sinRam.enabled &&
                observed.sinRam.threatLevel == 2;
            if (!first && !second) torn.fetch_add(1, std::memory_order_relaxed);
        }
    });
    while (ready.load(std::memory_order_acquire) != 3) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    writerA.join();
    writerB.join();
    reader.join();
    Expect(torn.load(std::memory_order_relaxed) == 0,
           "concurrent typed S.I.N. edits must never expose a torn enabled/threat pair");
}

struct BatchLeaseBackend {
    std::array<uintptr_t, kDifficultyDetourCount> targets{
        0x007828B0u, 0x00783ED0u, 0x00784010u,
    };
    std::array<bool, kDifficultyDetourCount> created{};
    std::array<bool, kDifficultyDetourCount> enabled{};
    std::array<int, kDifficultyDetourCount> pending{-1, -1, -1};
    int removeCalls = 0;
};

size_t BatchLeaseIndex(const BatchLeaseBackend& backend, uintptr_t target) {
    for (size_t index = 0; index < backend.targets.size(); ++index) {
        if (backend.targets[index] == target) return index;
    }
    return backend.targets.size();
}

bool BatchLeaseInitialize(void*) { return true; }

bool BatchLeaseCreate(
    void* context, uintptr_t target, void* detour, void** originalOut) {
    BatchLeaseBackend& backend = *static_cast<BatchLeaseBackend*>(context);
    const size_t index = BatchLeaseIndex(backend, target);
    if (index == backend.targets.size() || backend.created[index] ||
        !detour || !originalOut) {
        return false;
    }
    backend.created[index] = true;
    *originalOut = detour;
    return true;
}

bool BatchLeaseRemove(void* context, uintptr_t target) {
    BatchLeaseBackend& backend = *static_cast<BatchLeaseBackend*>(context);
    ++backend.removeCalls;
    const size_t index = BatchLeaseIndex(backend, target);
    if (index == backend.targets.size() || !backend.created[index]) return false;
    backend.created[index] = false;
    return true;
}

bool BatchLeaseQueueEnable(void* context, uintptr_t target) {
    BatchLeaseBackend& backend = *static_cast<BatchLeaseBackend*>(context);
    const size_t index = BatchLeaseIndex(backend, target);
    if (index == backend.targets.size() || !backend.created[index]) return false;
    backend.pending[index] = 1;
    return true;
}

bool BatchLeaseQueueDisable(void* context, uintptr_t target) {
    BatchLeaseBackend& backend = *static_cast<BatchLeaseBackend*>(context);
    const size_t index = BatchLeaseIndex(backend, target);
    if (index == backend.targets.size() || !backend.created[index]) return false;
    backend.pending[index] = 0;
    return true;
}

bool BatchLeaseApply(void* context) {
    BatchLeaseBackend& backend = *static_cast<BatchLeaseBackend*>(context);
    for (size_t index = 0; index < backend.pending.size(); ++index) {
        if (backend.pending[index] >= 0) {
            backend.enabled[index] = backend.pending[index] != 0;
            backend.pending[index] = -1;
        }
    }
    return true;
}

bool BatchLeaseDisable(void* context, uintptr_t target) {
    BatchLeaseBackend& backend = *static_cast<BatchLeaseBackend*>(context);
    const size_t index = BatchLeaseIndex(backend, target);
    if (index == backend.targets.size()) return false;
    backend.enabled[index] = false;
    return true;
}

struct PausedLeaseFence {
    std::atomic<bool>* accepting = nullptr;
    std::atomic<int>* countedCallbacks = nullptr;
};

bool ClosePausedLease(void* context) {
    PausedLeaseFence& fence = *static_cast<PausedLeaseFence*>(context);
    fence.accepting->store(false, std::memory_order_release);
    return fence.countedCallbacks->load(std::memory_order_acquire) == 0;
}

bool DrainPausedLease(void* context) {
    PausedLeaseFence& fence = *static_cast<PausedLeaseFence*>(context);
    return fence.countedCallbacks->load(std::memory_order_acquire) == 0;
}

void TestPausedMachinePrologueForcesProcessLifetimeRestorePolicy() {
    BatchLeaseBackend backend{};
    FfxHooks::MinHookBatch::Coordinator coordinator;
    const auto initialized = FfxHooks::MinHookBatch::EnsureInitialized(
        &coordinator, {nullptr, &BatchLeaseInitialize});
    std::array<void*, kDifficultyDetourCount> originals{};
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount> specs{{
        {backend.targets[0], reinterpret_cast<void*>(0x1010u), &originals[0]},
        {backend.targets[1], reinterpret_cast<void*>(0x2020u), &originals[1]},
        {backend.targets[2], reinterpret_cast<void*>(0x3030u), &originals[2]},
    }};
    const DifficultyDetourIo hookIo{
        &backend, &BatchLeaseCreate, &BatchLeaseRemove};
    const FfxHooks::MinHookBatch::BatchIo batchIo{
        &backend, &BatchLeaseQueueEnable, &BatchLeaseQueueDisable,
        &BatchLeaseApply, &BatchLeaseDisable};
    DifficultyDetourOwner owner{};
    const DifficultyDetourResult installed = InstallDifficultyDetours(
        hookIo, &coordinator, batchIo, {}, specs, &owner);
    Expect(initialized == FfxHooks::MinHookBatch::InitializationResult::Ready &&
               installed.code == DifficultyDetourCode::Installed && owner.active,
           "paused-prologue RT1 must start from the applied production three-target owner");

    std::atomic<bool> pausedBeforeFirstIncrement{false};
    std::atomic<bool> releaseEntrant{false};
    std::atomic<bool> accepting{true};
    std::atomic<int> countedCallbacks{0};
    std::atomic<int> retainedOriginalCalls{0};
    std::thread entrant([&]() {
        pausedBeforeFirstIncrement.store(true, std::memory_order_release);
        while (!releaseEntrant.load(std::memory_order_acquire)) std::this_thread::yield();
        countedCallbacks.fetch_add(1, std::memory_order_acq_rel);
        if (!accepting.load(std::memory_order_acquire) && originals[2] != nullptr) {
            retainedOriginalCalls.fetch_add(1, std::memory_order_relaxed);
        }
        countedCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    });
    while (!pausedBeforeFirstIncrement.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    Expect(countedCallbacks.load(std::memory_order_acquire) == 0,
           "RT1 entrant must be paused before the first C++ callback increment");
    PausedLeaseFence fence{&accepting, &countedCallbacks};
    const DifficultyDetourResult retired = RetireDifficultyDetours(
        hookIo, &coordinator, batchIo,
        {&fence, &ClosePausedLease, &DrainPausedLease}, specs, &owner);
    Expect(retired.code == DifficultyDetourCode::RetainedInert &&
               owner.mayHaveRun && owner.retainedInert && !owner.active &&
               backend.removeCalls == 0 && originals[0] && originals[1] && originals[2],
           "zero counted callbacks may neutralize but must retain every reachable target and original");
    Expect(!accepting.load(std::memory_order_acquire) &&
               !backend.enabled[0] && !backend.enabled[1] && !backend.enabled[2],
           "the normal-context fence must close admission before the exact batch becomes inert");

    releaseEntrant.store(true, std::memory_order_release);
    entrant.join();
    Expect(countedCallbacks.load(std::memory_order_acquire) == 0 &&
               retainedOriginalCalls.load(std::memory_order_relaxed) == 1,
           "the pre-counter entrant must finish through retained originals after admission closes");
}

void RecordResult(ConcurrentRuntime& state, const RuntimeResult& result) {
    state.operations.fetch_add(1, std::memory_order_relaxed);
    if (result.code == ResultCode::Fault || result.faults != 0) {
        state.faults.fetch_add(1, std::memory_order_relaxed);
    }
}

void ValidatePublished(
    ConcurrentRuntime& state, const F7ConfigStateSnapshot& snapshot) {
    if (!IsKnownSnapshot(snapshot.difficulty, snapshot.difficultyValid)) {
        state.hybridSnapshots.fetch_add(1, std::memory_order_relaxed);
    }
}

void PublishEdit(ConcurrentRuntime& state, const DifficultyConfig& config) {
    DifficultyPublication publication{config, true};
    const F7ConfigStateSnapshot snapshot = F7_UpdateConfigSnapshot(
        &ApplyDifficultyPublication, &publication);
    ValidatePublished(state, snapshot);
    state.publications.fetch_add(1, std::memory_order_relaxed);
}

void ApplyNow(ConcurrentRuntime& state) {
    // WHY: mirror production by copying one immutable config generation before
    // taking the runtime lock; later UI edits cannot tear this operation.
    const F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
    ValidatePublished(state, snapshot);
    AcquireSRWLockExclusive(&state.lock);
    const ActorRef actor{ActorAddress(state), 0};
    const RuntimeResult result = state.runtime.Update(
        MemoryIo{&state, &ReadMemory, &WriteMemory}, snapshot.difficulty,
        snapshot.difficultyValid, -1, &actor, 1);
    RecordResult(state, result);
    ReleaseSRWLockExclusive(&state.lock);
}

void InitializeBattle(ConcurrentRuntime& state) {
    const F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
    ValidatePublished(state, snapshot);
    AcquireSRWLockExclusive(&state.lock);
    // The real detour calls vanilla first. Recreate that boundary before a new
    // generation so the portable runtime never compounds a prior battle.
    ResetVanillaActor(state);
    state.runtime.BeginGeneration(++state.generation);
    const ActorRef actor{ActorAddress(state), 0};
    const RuntimeResult result = state.runtime.Update(
        MemoryIo{&state, &ReadMemory, &WriteMemory}, snapshot.difficulty,
        snapshot.difficultyValid, -1, &actor, 1);
    RecordResult(state, result);
    ReleaseSRWLockExclusive(&state.lock);
}

void PublishReload(
    ConcurrentRuntime& state, const DifficultyConfig& config, bool valid) {
    F7Config parsedConfig{};
    F7_ReplaceConfigSnapshot(parsedConfig, config, valid);
    const F7ConfigStateSnapshot snapshot = F7_GetConfigSnapshot();
    ValidatePublished(state, snapshot);
    state.publications.fetch_add(1, std::memory_order_relaxed);
}

void Teardown(ConcurrentRuntime& state) {
    AcquireSRWLockExclusive(&state.lock);
    RecordResult(state, state.runtime.Restore(MemoryIo{&state, &ReadMemory, &WriteMemory}));
    ReleaseSRWLockExclusive(&state.lock);
}

template <typename Operation>
std::thread StartWorker(
    std::atomic<int>& ready, std::atomic<bool>& start, Operation operation) {
    return std::thread([&ready, &start, operation]() mutable {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        operation();
    });
}

void TestConcurrentRuntimePublication() {
    ConcurrentRuntime state{};
    state.actor.bytes.fill(0xA5);
    ResetVanillaActor(state);
    F7Config initialConfig{};
    F7_ReplaceConfigSnapshot(initialConfig, MakeTaggedConfig(1500), true);
    state.runtime.BeginGeneration(++state.generation);

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    const DifficultyConfig editA = MakeTaggedConfig(1500);
    const DifficultyConfig editB = MakeTaggedConfig(2200);
    const char reloadJson[] =
        "{\"diff_enabled\":true,\"diff_hpMul\":3300,\"diff_mpMul\":3301,"
        "\"diff_strMul\":1330,\"diff_defMul\":1331,\"diff_magMul\":1332,"
        "\"diff_mdfMul\":1333,\"diff_agiMul\":1334,\"diff_accMul\":1335,"
        "\"diff_evaMul\":1336,\"diff_lckMul\":1337,\"diff_overkillMul\":3302,"
        "\"diffByArea\":false}";
    const char malformedJson[] = "{\"diff_enabled\":tru";

    std::thread editor = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations; ++i) {
            PublishEdit(state, (i & 1) == 0 ? editA : editB);
            std::this_thread::yield();
        }
    });
    std::thread applyNow = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations; ++i) {
            ApplyNow(state);
            std::this_thread::yield();
        }
    });
    std::thread initializer = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations / 4; ++i) {
            InitializeBattle(state);
            std::this_thread::yield();
        }
    });
    std::thread reloader = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations / 2; ++i) {
            // Parsing models bounded filesystem input and stays outside the
            // runtime lock; only the validated immutable value is published.
            DifficultyConfig parsed = MakeNeutralConfig();
            const char* json = (i % 5) == 0 ? malformedJson : reloadJson;
            const size_t length = std::strlen(json);
            const ConfigResult result = ParseConfig(json, length, &parsed);
            if (result.code == ConfigCode::Ok) {
                // Fill the area array before publication to make torn copies observable.
                parsed = MakeTaggedConfig(3300);
                PublishReload(state, parsed, true);
            } else {
                PublishReload(state, MakeNeutralConfig(), false);
            }
            std::this_thread::yield();
        }
    });
    std::thread teardown = StartWorker(ready, start, [&]() {
        for (int i = 0; i < kIterations / 3; ++i) {
            Teardown(state);
            std::this_thread::yield();
        }
    });

    while (ready.load(std::memory_order_acquire) != 5) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    editor.join();
    applyNow.join();
    initializer.join();
    reloader.join();
    teardown.join();

    const F7ConfigStateSnapshot finalSnapshot = F7_GetConfigSnapshot();
    ValidatePublished(state, finalSnapshot);
    AcquireSRWLockExclusive(&state.lock);
    const RuntimeResult finalRestore = state.runtime.Restore(
        MemoryIo{&state, &ReadMemory, &WriteMemory});
    RecordResult(state, finalRestore);
    const uint32_t maxHp = LoadActorValue<uint32_t>(state, kMaxHpOffset);
    const uint32_t maxMp = LoadActorValue<uint32_t>(state, kMaxMpOffset);
    const uint32_t currentHp = LoadActorValue<uint32_t>(state, kCurrentHpOffset);
    const uint32_t currentMp = LoadActorValue<uint32_t>(state, kCurrentMpOffset);
    ReleaseSRWLockExclusive(&state.lock);

    bool guardsIntact = true;
    for (size_t i = 0; i < kGuardBytes; ++i) {
        guardsIntact &= state.actor.bytes[i] == 0xA5;
        guardsIntact &= state.actor.bytes[kGuardBytes + kActorSpan + i] == 0xA5;
    }
    Expect(state.operations.load(std::memory_order_relaxed) ==
               kIterations + kIterations / 4 + kIterations / 3 + 1,
           "Apply Now, initializer, teardown, and final restore must all complete");
    Expect(state.publications.load(std::memory_order_relaxed) ==
               kIterations + kIterations / 2,
           "every concurrent UI edit and reload publication must complete");
    Expect(state.faults.load(std::memory_order_relaxed) == 0,
           "concurrent runtime operations must not produce read/write faults");
    Expect(state.hybridSnapshots.load(std::memory_order_relaxed) == 0,
           "edit and reload publication must never expose a torn Difficulty snapshot");
    Expect(currentHp <= maxHp && currentMp <= maxMp,
           "concurrent edits and teardown must leave current HP/MP within restored maxima");
    Expect(guardsIntact,
           "concurrent runtime operations must preserve both actor-span canaries");
}

DifficultyConfig MakeDoubleVitalsConfig() {
    DifficultyConfig config = MakeNeutralConfig();
    config.global.enabled = true;
    config.global.hpMul = 2000;
    config.global.mpMul = 2000;
    return config;
}

void TestUpdateRetryPreservesPreTransactionRatio() {
    ConcurrentRuntime state{};
    ResetVanillaActor(state);
    state.runtime.BeginGeneration(1);
    state.failCurrentHpWrites = 1;
    state.failCurrentMpWrites = 1;
    const ActorRef actor{ActorAddress(state), 0};
    const MemoryIo io{&state, &ReadMemory, &WriteMemory};
    const DifficultyConfig config = MakeDoubleVitalsConfig();

    const RuntimeResult failed = state.runtime.Update(io, config, true, -1, &actor, 1);
    Expect(failed.code == ResultCode::Fault,
           "ON must report a deterministic fault when both paired current writes fail");
    Expect(LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
               LoadActorValue<uint32_t>(state, kMaxMpOffset) == 200u &&
               LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u &&
               LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 50u,
           "ON partial failure must expose changed maxima without pretending currents committed");

    const RuntimeResult retried = state.runtime.Update(io, config, true, -1, &actor, 1);
    Expect(retried.code == ResultCode::Applied,
           "ON retry must reconcile a previously partial max/current transaction");
    Expect(LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u &&
               LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 100u,
           "ON retry must reuse the immutable pre-transaction HP/MP ratios");
}

void TestRestoreRetryPreservesPreTransactionRatio() {
    ConcurrentRuntime state{};
    ResetVanillaActor(state);
    state.runtime.BeginGeneration(1);
    const ActorRef actor{ActorAddress(state), 0};
    const MemoryIo io{&state, &ReadMemory, &WriteMemory};
    const DifficultyConfig config = MakeDoubleVitalsConfig();
    const RuntimeResult applied = state.runtime.Update(io, config, true, -1, &actor, 1);
    Expect(applied.code == ResultCode::Applied,
           "OFF partial-failure setup must first apply the doubled maxima");

    StoreActorValue<uint32_t>(state, kCurrentHpOffset, 750u);
    StoreActorValue<uint32_t>(state, kCurrentMpOffset, 25u);
    state.failCurrentHpWrites = 1;
    state.failCurrentMpWrites = 1;
    const RuntimeResult failed = state.runtime.Restore(io);
    Expect(failed.code == ResultCode::Fault,
           "OFF must report a deterministic fault when both paired current writes fail");
    Expect(LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
               LoadActorValue<uint32_t>(state, kMaxMpOffset) == 100u &&
               LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 750u &&
               LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 25u,
           "OFF partial failure must expose restored maxima without discarding retry ownership");

    const RuntimeResult retried = state.runtime.Restore(io);
    Expect(retried.code == ResultCode::Restored,
           "OFF retry must reconcile a previously partial max/current transaction");
    Expect(LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 375u &&
               LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 13u,
           "OFF retry must reuse the immutable pre-restore HP/MP ratios with half-up rounding");
}

void TestInterveningGameplayRejectsCurrentRetry() {
    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failCurrentHpWrites = 1;
        state.failCurrentMpWrites = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        const DifficultyConfig config = MakeDoubleVitalsConfig();

        const RuntimeResult failed = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(failed.code == ResultCode::Fault,
               "intervening gameplay RT1 must begin from deterministic paired write faults");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 425u);
        StoreActorValue<uint32_t>(state, kCurrentMpOffset, 20u);
        const RuntimeResult retried = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(retried.code == ResultCode::OwnershipLost && retried.ownershipLost >= 2,
               "damage and MP spend after failed writes must reject stale RT1 retries");
        Expect(LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 425u &&
                   LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 20u &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kMaxMpOffset) == 200u,
               "rejected RT1 retries must preserve gameplay currents and non-compounded maxima");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        const DifficultyConfig config = MakeDoubleVitalsConfig();
        const RuntimeResult applied = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(applied.code == ResultCode::Applied,
               "intervening heal RT1 must first own the doubled maxima");
        state.failCurrentHpWrites = 1;
        const RuntimeResult failed = state.runtime.Restore(io);
        Expect(failed.code == ResultCode::Fault,
               "intervening heal RT1 must fail the first OFF current write");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 900u);
        const RuntimeResult retried = state.runtime.Restore(io);
        Expect(retried.code == ResultCode::OwnershipLost && retried.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 900u &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u,
               "OFF RT1 retry must preserve healing published after its failed current write");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failCurrentHpReadbacksAfterWrite = 1;
        state.failCurrentMpReadbacksAfterWrite = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        const DifficultyConfig config = MakeDoubleVitalsConfig();

        const RuntimeResult failed = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(failed.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 100u,
               "ambiguous RT1 setup must commit current writes but fail both readbacks");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 875u);
        StoreActorValue<uint32_t>(state, kCurrentMpOffset, 40u);
        const RuntimeResult retried = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(retried.code == ResultCode::OwnershipLost && retried.ownershipLost >= 2 &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 875u &&
                   LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 40u,
               "RT1 retry must preserve damage and MP spend after ambiguous readbacks");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failCurrentHpWrites = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault,
               "RT1 edit-during-retry setup must retain a failed current transaction");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 1000u);
        config.global.hpMul = 3000;
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 gameplay at the prior target must block a later maximum edit before writes");
    }
}

void TestMaximumFailureFreshensOrRelinquishesCurrentRt1() {
    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failMaxHpWrites = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u,
               "RT1 ON HP maximum failure must leave the pair definitely unchanged");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 425u);
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 850u,
               "RT1 ON retry must freshen damage after an unchanged maximum failure");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failMaxMpWrites = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.hpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kMaxMpOffset) == 100u &&
                   LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 50u,
               "RT1 ON MP maximum failure must leave the pair definitely unchanged");
        StoreActorValue<uint32_t>(state, kCurrentMpOffset, 20u);
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied &&
                   LoadActorValue<uint32_t>(state, kMaxMpOffset) == 200u &&
                   LoadActorValue<uint32_t>(state, kCurrentMpOffset) == 40u,
               "RT1 ON retry must freshen MP spend after an unchanged maximum failure");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;
        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied,
               "RT1 OFF maximum failure setup must first own doubled HP");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 800u);
        state.failMaxHpWrites = 1;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 800u,
               "RT1 OFF maximum failure must leave the pair definitely unchanged");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 900u);
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::Restored &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 450u,
               "RT1 OFF retry must freshen healing after an unchanged maximum failure");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failMaxHpReadsAfterWrite = true;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u,
               "RT1 ambiguous ON maximum readback must stop before current HP");
        state.failMaxHpReadsAfterWrite = false;
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 425u);
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 425u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 ambiguous ON maximum must relinquish without a stale retry write");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;
        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied,
               "RT1 ambiguous OFF maximum setup must first own doubled HP");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 800u);
        state.maxHpWriteObserved = false;
        state.failMaxHpReadsAfterWrite = true;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 800u,
               "RT1 ambiguous OFF maximum readback must stop before current HP");
        state.failMaxHpReadsAfterWrite = false;
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 900u);
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 900u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 ambiguous OFF maximum must preserve healing without another write");
    }
}

void TestAmbiguousCurrentReadbackAbaFailsClosedRt1() {
    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.failCurrentHpReadbacksAfterWrite = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u,
               "RT1 ON ABA setup must apply current HP with an ambiguous readback");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 500u);
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 ON ABA to the pre-write value must not authorize another current write");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;
        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied,
               "RT1 OFF ABA setup must first own doubled HP");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 800u);
        state.failCurrentHpReadbacksAfterWrite = 1;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 400u,
               "RT1 OFF ABA setup must apply current HP with an ambiguous readback");
        StoreActorValue<uint32_t>(state, kCurrentHpOffset, 800u);
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 800u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 OFF ABA to the pre-write value must not authorize another restore write");
    }
}

void TestSuccessfulWriteReturningToExpectedFailsClosedRt1() {
    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.successfulNoopMaxHpWrites = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u,
               "RT1 successful maximum write returning to expected must be interference");
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 maximum interference must permanently reject the stale pair retry");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        state.successfulNoopCurrentHpWrites = 1;
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;

        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u,
               "RT1 successful current write returning to prevalue must be interference");
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 500u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 current interference must permanently reject the stale ratio retry");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;
        Expect(state.runtime.Update(io, config, true, -1, &actor, 1).code == ResultCode::Applied,
               "RT1 OFF maximum interference setup must first own doubled HP");
        state.successfulNoopMaxHpWrites = 1;

        RuntimeResult result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u,
               "RT1 successful maximum restore returning to owned must be interference");
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 2000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 maximum restore interference must permanently reject stale pair retry");
    }

    {
        ConcurrentRuntime state{};
        ResetVanillaActor(state);
        state.runtime.BeginGeneration(1);
        const ActorRef actor{ActorAddress(state), 0};
        const MemoryIo io{&state, &ReadMemory, &WriteMemory};
        DifficultyConfig config = MakeDoubleVitalsConfig();
        config.global.mpMul = 1000;
        Expect(state.runtime.Update(io, config, true, -1, &actor, 1).code == ResultCode::Applied,
               "RT1 OFF current interference setup must first own doubled HP");
        state.successfulNoopCurrentHpWrites = 1;

        RuntimeResult result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u,
               "RT1 successful current restore returning to prevalue must be interference");
        const int writesBeforeRetry = state.memoryWrites;
        result = state.runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost &&
                   LoadActorValue<uint32_t>(state, kMaxHpOffset) == 1000u &&
                   LoadActorValue<uint32_t>(state, kCurrentHpOffset) == 1000u &&
                   state.memoryWrites == writesBeforeRetry,
               "RT1 current restore interference must permanently reject stale ratio retry");
    }
}

}  // namespace

bool RefreshExactNativeAutoStatus(void* context, uintptr_t actor) {
    auto& state = *static_cast<ConcurrentRuntime*>(context);
    using NativeFn = void(__cdecl*)(uintptr_t);
    reinterpret_cast<NativeFn>(state.nativeAutoRemove)(actor);
    reinterpret_cast<NativeFn>(state.nativeAutoApply)(actor);
    ++state.autoRefreshes;
    return true;
}

void TestExactNativeAutoStatusRoundTrip() {
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Expect(code != nullptr, "RT1 native status fixture must allocate private memory");
    if (!code) return;
    std::memcpy(code, kAutoStatusRemoveBody.data(), kAutoStatusRemoveBody.size());
    std::memcpy(code + 512, kAutoStatusApplyBody.data(), kAutoStatusApplyBody.size());
    DWORD oldProtect = 0;
    const bool executable = VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &oldProtect) != FALSE &&
        FlushInstructionCache(GetCurrentProcess(), code, 4096) != FALSE;
    Expect(executable, "RT1 exact native fixture must become read/execute, never writable/executable");
    if (!executable) { VirtualFree(code, 0, MEM_RELEASE); return; }
    for (unsigned bit = 0; bit < 25; ++bit) {
        ConcurrentRuntime state{};
        state.actor.bytes.fill(0xA5);
        std::memset(reinterpret_cast<void*>(ActorAddress(state)), 0, kActorSpan);
        StoreActorValue(state, 0x00E, uint16_t{7});
        StoreActorValue(state, 0x594, uint32_t{1000});
        StoreActorValue(state, 0x5D0, uint32_t{500});
        StoreActorValue(state, 0x606, uint16_t{0x4008});
        for (size_t i = 0; i < 13; ++i)
            StoreActorValue(state, 0x608 + i, uint8_t{2});
        // Native Haste correctly dispels finite Slow. Keep that antagonism out
        // of the independent temporary-effect preservation assertion below.
        StoreActorValue(state, 0x614, uint8_t{0});
        state.nativeAutoRemove = code;
        state.nativeAutoApply = code + 512;
        const MemoryIo io{&state, &ReadMemory, &WriteMemory, &RefreshExactNativeAutoStatus};
        const ActorRef actor{ActorAddress(state), 0};
        DifficultyConfig config = MakeNeutralConfig();
        config.global.enabled = true;
        config.global.autoStatusMask = 1u << bit;
        state.runtime.BeginGeneration(1);
        RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
        const bool applied = bit < 12
            ? LoadActorValue<uint16_t>(state, 0x606) == static_cast<uint16_t>(0x4008u | (1u << bit))
            : LoadActorValue<uint8_t>(state, 0x608 + bit - 12) == 255;
        Expect(result.code == ResultCode::Applied && result.autoStatusRefreshed == 1 &&
               applied, "RT1 every AUTO choice must become a real status through the exact native machine code");
        const int writes = state.memoryWrites;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied && state.autoRefreshes == 1 &&
               state.memoryWrites == writes, "RT1 repeated Apply must not recapture permanent effects as temporary");
        const uint16_t unrelated = bit == 1 ? 4 : 2;
        StoreActorValue(state, 0x606,
            static_cast<uint16_t>(LoadActorValue<uint16_t>(state, 0x606) | unrelated));
        const size_t unrelatedDuration = bit == 12 ? 0x609 : 0x608;
        StoreActorValue(state, unrelatedDuration, uint8_t{7});
        config.global.enabled = false;
        result = state.runtime.Update(io, config, true, -1, &actor, 1);
        const bool restored = bit < 12 ||
            LoadActorValue<uint8_t>(state, 0x608 + bit - 12) == (bit == 24 ? 0 : 2);
        bool canaries = true;
        for (size_t i = 0; i < kGuardBytes; ++i)
            canaries = canaries && state.actor.bytes[i] == 0xA5 &&
                state.actor.bytes[kGuardBytes + kActorSpan + i] == 0xA5;
        Expect(result.code == ResultCode::Restored && result.autoStatusRefreshed == 1 &&
               restored && LoadActorValue<uint16_t>(state, 0x606) == (0x4008u | unrelated) &&
               LoadActorValue<uint8_t>(state, unrelatedDuration) == 7 &&
               LoadActorValue<uint16_t>(state, 0x62A) == 0 &&
               LoadActorValue<uint16_t>(state, 0x62C) == 0 && canaries,
               "RT1 OFF must remove permanent additions and preserve pre-existing/independent temporary effects");
    }
    VirtualFree(code, 0, MEM_RELEASE);
}

// Exact 78CE... VA 0x0078A420..0x0078A834; cdecl(actor*, unused, elementMask, damage).
// Offline consumer fixture: no calls, globals, or HIGHLOW relocations.
void TestExactNativeElementDamage() {
    static constexpr uint8_t body[] = {
        0x55,0x8B,0xEC,0x83,0xEC,0x20,0x57,0x8B,0x7D,0x10,0x85,0xFF,0x75,0x08,0x8B,0x45,
        0x14,0x5F,0x8B,0xE5,0x5D,0xC3,0x8B,0x45,0x08,0x53,0x0F,0xB6,0x88,0xDC,0x05,0x00,
        0x00,0x0F,0xB6,0x90,0xDD,0x05,0x00,0x00,0x0F,0xB6,0x98,0xDB,0x05,0x00,0x00,0x89,
        0x4D,0xE4,0x0F,0xB6,0x88,0xDA,0x05,0x00,0x00,0x56,0x8B,0xC7,0x33,0xF6,0x83,0xE0,
        0x01,0x89,0x55,0x10,0x89,0x45,0xF0,0x74,0x1A,0xF6,0xC2,0x01,0x74,0x15,0x8B,0x45,
        0x14,0xBE,0x01,0x00,0x00,0x00,0x8D,0x04,0x40,0x99,0x2B,0xC2,0x8B,0x55,0x10,0xD1,
        0xF8,0xEB,0x03,0x8B,0x45,0x14,0x89,0x7D,0xFC,0x83,0x65,0xFC,0x02,0x74,0x12,0xF6,
        0xC2,0x02,0x74,0x0D,0x8D,0x04,0x40,0x99,0x2B,0xC2,0xD1,0xF8,0xBE,0x01,0x00,0x00,
        0x00,0x8B,0xD7,0x83,0xE2,0x04,0x89,0x55,0xEC,0x8B,0x55,0x10,0x74,0x15,0xF6,0xC2,
        0x04,0x74,0x10,0x8D,0x04,0x40,0x99,0x2B,0xC2,0x8B,0x55,0x10,0xD1,0xF8,0xBE,0x01,
        0x00,0x00,0x00,0x89,0x7D,0xF8,0x83,0x65,0xF8,0x08,0x74,0x12,0xF6,0xC2,0x08,0x74,
        0x0D,0x8D,0x04,0x40,0x99,0x2B,0xC2,0xD1,0xF8,0xBE,0x01,0x00,0x00,0x00,0x8B,0xD7,
        0x83,0xE2,0x10,0x89,0x55,0xE8,0x8B,0x55,0x10,0x74,0x15,0xF6,0xC2,0x10,0x74,0x10,
        0x8D,0x04,0x40,0x99,0x2B,0xC2,0x8B,0x55,0x10,0xD1,0xF8,0xBE,0x01,0x00,0x00,0x00,
        0x89,0x7D,0xF4,0x83,0x65,0xF4,0x20,0x74,0x12,0xF6,0xC2,0x20,0x74,0x0D,0x8D,0x04,
        0x40,0x99,0x2B,0xC2,0xD1,0xF8,0xBE,0x01,0x00,0x00,0x00,0x8B,0xD7,0x83,0xE2,0x40,
        0x89,0x55,0xE0,0x8B,0x55,0x10,0x74,0x15,0xF6,0xC2,0x40,0x74,0x10,0x8D,0x04,0x40,
        0x99,0x2B,0xC2,0x8B,0x55,0x10,0xD1,0xF8,0xBE,0x01,0x00,0x00,0x00,0x81,0xE7,0x80,
        0x00,0x00,0x00,0x74,0x13,0x84,0xD2,0x79,0x0F,0x8D,0x04,0x40,0x99,0x5E,0x2B,0xC2,
        0x5B,0xD1,0xF8,0x5F,0x8B,0xE5,0x5D,0xC3,0x85,0xF6,0x0F,0x85,0xCE,0x02,0x00,0x00,
        0x8B,0x55,0xE4,0x39,0x75,0xF0,0x74,0x14,0xF6,0xC3,0x01,0x75,0x0F,0xF6,0xC2,0x01,
        0x75,0x0A,0xF6,0xC1,0x01,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xFC,0x00,
        0x74,0x14,0xF6,0xC3,0x02,0x75,0x0F,0xF6,0xC2,0x02,0x75,0x0A,0xF6,0xC1,0x02,0x75,
        0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xEC,0x00,0x74,0x14,0xF6,0xC3,0x04,0x75,
        0x0F,0xF6,0xC2,0x04,0x75,0x0A,0xF6,0xC1,0x04,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,
        0x83,0x7D,0xF8,0x00,0x74,0x14,0xF6,0xC3,0x08,0x75,0x0F,0xF6,0xC2,0x08,0x75,0x0A,
        0xF6,0xC1,0x08,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xE8,0x00,0x74,0x14,
        0xF6,0xC3,0x10,0x75,0x0F,0xF6,0xC2,0x10,0x75,0x0A,0xF6,0xC1,0x10,0x75,0x05,0xBE,
        0x01,0x00,0x00,0x00,0x83,0x7D,0xF4,0x00,0x74,0x14,0xF6,0xC3,0x20,0x75,0x0F,0xF6,
        0xC2,0x20,0x75,0x0A,0xF6,0xC1,0x20,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,
        0xE0,0x00,0x74,0x14,0xF6,0xC3,0x40,0x75,0x0F,0xF6,0xC2,0x40,0x75,0x0A,0xF6,0xC1,
        0x40,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x85,0xFF,0x74,0x10,0x84,0xDB,0x78,0x0C,
        0x84,0xD2,0x78,0x08,0x84,0xC9,0x0F,0x89,0x02,0x02,0x00,0x00,0x85,0xF6,0x0F,0x85,
        0xFA,0x01,0x00,0x00,0x39,0x75,0xF0,0x74,0x14,0xF6,0xC2,0x01,0x74,0x0F,0xF6,0xC3,
        0x01,0x75,0x0A,0xF6,0xC1,0x01,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xFC,
        0x00,0x74,0x14,0xF6,0xC2,0x02,0x74,0x0F,0xF6,0xC3,0x02,0x75,0x0A,0xF6,0xC1,0x02,
        0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xEC,0x00,0x74,0x14,0xF6,0xC2,0x04,
        0x74,0x0F,0xF6,0xC3,0x04,0x75,0x0A,0xF6,0xC1,0x04,0x75,0x05,0xBE,0x01,0x00,0x00,
        0x00,0x83,0x7D,0xF8,0x00,0x74,0x14,0xF6,0xC2,0x08,0x74,0x0F,0xF6,0xC3,0x08,0x75,
        0x0A,0xF6,0xC1,0x08,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xE8,0x00,0x74,
        0x14,0xF6,0xC2,0x10,0x74,0x0F,0xF6,0xC3,0x10,0x75,0x0A,0xF6,0xC1,0x10,0x75,0x05,
        0xBE,0x01,0x00,0x00,0x00,0x83,0x7D,0xF4,0x00,0x74,0x14,0xF6,0xC2,0x20,0x74,0x0F,
        0xF6,0xC3,0x20,0x75,0x0A,0xF6,0xC1,0x20,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x83,
        0x7D,0xE0,0x00,0x74,0x14,0xF6,0xC2,0x40,0x74,0x0F,0xF6,0xC3,0x40,0x75,0x0A,0xF6,
        0xC1,0x40,0x75,0x05,0xBE,0x01,0x00,0x00,0x00,0x85,0xFF,0x74,0x10,0x84,0xD2,0x79,
        0x0C,0x84,0xDB,0x78,0x08,0x84,0xC9,0x0F,0x89,0x4F,0xFE,0xFF,0xFF,0x85,0xF6,0x0F,
        0x85,0x47,0xFE,0xFF,0xFF,0x33,0xD2,0x39,0x55,0xF0,0x74,0x0F,0xF6,0xC3,0x01,0x74,
        0x0A,0xF6,0xC1,0x01,0x75,0x05,0x33,0xC0,0x8D,0x56,0x01,0x83,0x7D,0xFC,0x00,0x74,
        0x0F,0xF6,0xC3,0x02,0x74,0x0A,0xF6,0xC1,0x02,0x75,0x05,0x33,0xC0,0x8D,0x50,0x01,
        0x83,0x7D,0xEC,0x00,0x74,0x0F,0xF6,0xC3,0x04,0x74,0x0A,0xF6,0xC1,0x04,0x75,0x05,
        0x33,0xC0,0x8D,0x50,0x01,0x83,0x7D,0xF8,0x00,0x74,0x0F,0xF6,0xC3,0x08,0x74,0x0A,
        0xF6,0xC1,0x08,0x75,0x05,0x33,0xC0,0x8D,0x50,0x01,0x83,0x7D,0xE8,0x00,0x74,0x0F,
        0xF6,0xC3,0x10,0x74,0x0A,0xF6,0xC1,0x10,0x75,0x05,0x33,0xC0,0x8D,0x50,0x01,0x83,
        0x7D,0xF4,0x00,0x74,0x0F,0xF6,0xC3,0x20,0x74,0x0A,0xF6,0xC1,0x20,0x75,0x05,0x33,
        0xC0,0x8D,0x50,0x01,0x8B,0x75,0xE0,0x85,0xF6,0x74,0x0F,0xF6,0xC3,0x40,0x74,0x0A,
        0xF6,0xC1,0x40,0x75,0x05,0x33,0xC0,0x8D,0x50,0x01,0x85,0xFF,0x74,0x11,0x84,0xDB,
        0x79,0x0D,0x84,0xC9,0x78,0x09,0x5E,0x5B,0x33,0xC0,0x5F,0x8B,0xE5,0x5D,0xC3,0x85,
        0xD2,0x75,0x7B,0x39,0x55,0xF0,0x74,0x0A,0xF6,0xC1,0x01,0x74,0x05,0xBA,0x01,0x00,
        0x00,0x00,0x83,0x7D,0xFC,0x00,0x74,0x0A,0xF6,0xC1,0x02,0x74,0x05,0xBA,0x01,0x00,
        0x00,0x00,0x83,0x7D,0xEC,0x00,0x74,0x0A,0xF6,0xC1,0x04,0x74,0x05,0xBA,0x01,0x00,
        0x00,0x00,0x83,0x7D,0xF8,0x00,0x74,0x0A,0xF6,0xC1,0x08,0x74,0x05,0xBA,0x01,0x00,
        0x00,0x00,0x83,0x7D,0xE8,0x00,0x74,0x0A,0xF6,0xC1,0x10,0x74,0x05,0xBA,0x01,0x00,
        0x00,0x00,0x83,0x7D,0xF4,0x00,0x74,0x0A,0xF6,0xC1,0x20,0x74,0x05,0xBA,0x01,0x00,
        0x00,0x00,0x85,0xF6,0x74,0x0A,0xF6,0xC1,0x40,0x74,0x05,0xBA,0x01,0x00,0x00,0x00,
        0x85,0xFF,0x74,0x04,0x84,0xC9,0x78,0x04,0x85,0xD2,0x74,0x02,0xF7,0xD8,0x5E,0x5B,
        0x5F,0x8B,0xE5,0x5D,0xC3,
    };
    void* code = VirtualAlloc(nullptr, sizeof(body), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Expect(code != nullptr, "RT1 native element fixture must allocate private memory");
    if (!code) return;
    std::memcpy(code, body, sizeof(body));
    DWORD oldProtect = 0;
    const bool executable = VirtualProtect(code, sizeof(body), PAGE_EXECUTE_READ, &oldProtect) != FALSE &&
        FlushInstructionCache(GetCurrentProcess(), code, sizeof(body)) != FALSE;
    Expect(executable, "RT1 native damage consumer must become read/execute");
    if (!executable) { VirtualFree(code, 0, MEM_RELEASE); return; }
    using DamageFn = int(__cdecl*)(uintptr_t, int, int, int);
    const auto damage = reinterpret_cast<DamageFn>(code);
    for (unsigned bit = 0; bit < 5; ++bit) {
        for (int affinity = 0; affinity < 3; ++affinity) {
            ConcurrentRuntime state{};
            const uintptr_t actorAddress = ActorAddress(state);
            StoreActorValue(state, 0x00E, uint16_t{7});
            // Each selection must supersede a pre-existing native immunity.
            StoreActorValue(state, 0x5DB, static_cast<uint8_t>(1u << bit));
            const MemoryIo io{&state, &ReadMemory, &WriteMemory};
            const ActorRef actor{actorAddress, 0};
            DifficultyConfig config = MakeNeutralConfig();
            config.global.enabled = true;
            if (affinity == 0) config.global.elemWeak = static_cast<uint8_t>(1u << bit);
            if (affinity == 1) config.global.elemResist = static_cast<uint8_t>(1u << bit);
            if (affinity == 2) config.global.elemAbsorb = static_cast<uint8_t>(1u << bit);
            state.runtime.BeginGeneration(1);
            const RuntimeResult result = state.runtime.Update(io, config, true, -1, &actor, 1);
            const int expected[] = {150, 50, -100};
            Expect(result.code == ResultCode::Applied && damage(actorAddress, 0, 1 << bit, 100) == expected[affinity],
                   "RT1 each affinity must change actual native elemental damage");
            config.global.enabled = false;
            const RuntimeResult restored = state.runtime.Update(io, config, true, -1, &actor, 1);
            Expect(restored.code == ResultCode::Restored && damage(actorAddress, 0, 1 << bit, 100) == 0,
                   "RT1 OFF must restore original native elemental immunity");
        }
    }
    VirtualFree(code, 0, MEM_RELEASE);
}

int main() {
    TestExactNativeElementDamage();
    TestExactNativeAutoStatusRoundTrip();
    TestUpdateRetryPreservesPreTransactionRatio();
    TestRestoreRetryPreservesPreTransactionRatio();
    TestInterveningGameplayRejectsCurrentRetry();
    TestMaximumFailureFreshensOrRelinquishesCurrentRt1();
    TestAmbiguousCurrentReadbackAbaFailsClosedRt1();
    TestSuccessfulWriteReturningToExpectedFailsClosedRt1();
    TestProductionConfigSnapshotApis();
    TestSinRamTypedConfigSnapshotApis();
    TestPausedMachinePrologueForcesProcessLifetimeRestorePolicy();
    TestConcurrentRuntimePublication();
    if (g_failures != 0) {
        std::fprintf(stderr, "F7RuntimeRt1: FAIL (%d/%d failed)\n", g_failures, g_checks);
        return 1;
    }
    std::printf("F7RuntimeRt1: PASS (%d checks)\n", g_checks);
    return 0;
}
