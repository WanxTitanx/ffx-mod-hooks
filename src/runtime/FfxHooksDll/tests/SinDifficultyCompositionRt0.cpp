#include "../hooks/F7DifficultyCore.h"
#include "../hooks/SinRamScalingCore.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace FfxHooks::F7Difficulty;

int g_passed = 0;
int g_failed = 0;

template <typename T, typename = void>
struct HasContext : std::false_type {};
template <typename T>
struct HasContext<T, std::void_t<decltype(std::declval<T&>().context)>> : std::true_type {};

template <typename T, typename = void>
struct HasCallback : std::false_type {};
template <typename T>
struct HasCallback<T, std::void_t<decltype(std::declval<T&>().callback)>> : std::true_type {};

template <typename T, typename = void>
struct HasCompose : std::false_type {};
template <typename T>
struct HasCompose<T, std::void_t<decltype(std::declval<T&>().compose)>> : std::true_type {};

template <typename T, typename = void>
struct HasAddress : std::false_type {};
template <typename T>
struct HasAddress<T, std::void_t<decltype(std::declval<T&>().address)>> : std::true_type {};

template <typename T, typename = void>
struct HasMemory : std::false_type {};
template <typename T>
struct HasMemory<T, std::void_t<decltype(std::declval<T&>().memory)>> : std::true_type {};

template <typename T, typename = void>
struct HasPreDifficultyHp : std::false_type {};
template <typename T>
struct HasPreDifficultyHp<T, std::void_t<decltype(std::declval<T&>().preDifficultyHp)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasCurrentHp : std::false_type {};
template <typename T>
struct HasCurrentHp<T, std::void_t<decltype(std::declval<T&>().currentHp)>> : std::true_type {};

template <typename T, typename = void>
struct HasMaxMp : std::false_type {};
template <typename T>
struct HasMaxMp<T, std::void_t<decltype(std::declval<T&>().maxMp)>> : std::true_type {};

template <typename T, typename = void>
struct HasCurrentMp : std::false_type {};
template <typename T>
struct HasCurrentMp<T, std::void_t<decltype(std::declval<T&>().currentMp)>> : std::true_type {};

template <typename T, typename = void>
struct HasMonsterId : std::false_type {};
template <typename T>
struct HasMonsterId<T, std::void_t<decltype(std::declval<T&>().monsterId)>> : std::true_type {};

template <typename T, typename = void>
struct HasAfterDifficulty : std::false_type {};
template <typename T>
struct HasAfterDifficulty<T, std::void_t<decltype(std::declval<T&>().afterDifficulty)>>
    : std::true_type {};

static_assert(!HasContext<FfxHooks::SinRam::RuntimeRequest>::value,
              "the typed S.I.N. request must not contain an opaque context");
static_assert(!HasCallback<FfxHooks::SinRam::RuntimeRequest>::value &&
                  !HasCompose<FfxHooks::SinRam::RuntimeRequest>::value,
              "the typed S.I.N. request must not contain a callback channel");
static_assert(!HasAddress<FfxHooks::SinRam::RuntimeRequest>::value &&
                  !HasMemory<FfxHooks::SinRam::RuntimeRequest>::value,
              "the typed S.I.N. request must not contain actor addresses or memory IO");
static_assert(!HasPreDifficultyHp<FfxHooks::SinRam::RuntimeRequest>::value &&
                  !HasCurrentHp<FfxHooks::SinRam::RuntimeRequest>::value &&
                  !HasMaxMp<FfxHooks::SinRam::RuntimeRequest>::value &&
                  !HasCurrentMp<FfxHooks::SinRam::RuntimeRequest>::value,
              "the typed S.I.N. request must not contain HP or MP data");
static_assert(!HasMonsterId<FfxHooks::SinRam::RuntimeRequest>::value &&
                  !HasAfterDifficulty<FfxHooks::SinRam::RuntimeRequest>::value,
              "Runtime, not the caller, must supply actor identity and structural targets");

using ClosedUpdateSignature = FfxHooks::F7Difficulty::RuntimeResult (
    FfxHooks::F7Difficulty::Runtime::*)(
        const FfxHooks::F7Difficulty::MemoryIo&,
        const FfxHooks::F7Difficulty::DifficultyConfig&, bool, std::int32_t,
        const FfxHooks::SinRam::RuntimeRequest&,
        const FfxHooks::F7Difficulty::ActorRef*, std::size_t);
static_assert(
    std::is_same<decltype(&FfxHooks::F7Difficulty::Runtime::UpdateComposed),
                 ClosedUpdateSignature>::value,
    "UpdateComposed must accept the typed request as an immutable reference");

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string SliceBetween(
    const std::string& source, const char* beginToken, const char* endToken) {
    const size_t begin = source.find(beginToken);
    if (begin == std::string::npos) return {};
    const size_t end = source.find(endToken, begin + std::strlen(beginToken));
    if (end == std::string::npos) return {};
    return source.substr(begin, end - begin);
}

struct WriteEvent {
    size_t offset = 0;
    uint32_t value = 0;
    size_t width = 0;
};

struct FakeActorMemory {
    static constexpr uintptr_t kBase = 0x10000000u;

    std::array<uint8_t, kActorSpan> bytes{};
    std::vector<WriteEvent> writes{};

    template <typename T>
    void Store(size_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    template <typename T>
    T Load(size_t offset) const {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    size_t WriteCount(size_t offset) const {
        return static_cast<size_t>(std::count_if(
            writes.begin(), writes.end(),
            [offset](const WriteEvent& event) { return event.offset == offset; }));
    }

    size_t FirstWrite(size_t offset) const {
        for (size_t index = 0; index < writes.size(); ++index) {
            if (writes[index].offset == offset) return index;
        }
        return writes.size();
    }
};

bool ReadMemory(void* context, uintptr_t address, void* output, size_t width) {
    auto& memory = *static_cast<FakeActorMemory*>(context);
    if (!output || address < FakeActorMemory::kBase) return false;
    const uintptr_t relative = address - FakeActorMemory::kBase;
    if (relative > memory.bytes.size() || width > memory.bytes.size() - relative) return false;
    std::memcpy(output, memory.bytes.data() + relative, width);
    return true;
}

bool WriteMemory(void* context, uintptr_t address, const void* value, size_t width) {
    auto& memory = *static_cast<FakeActorMemory*>(context);
    if (!value || address < FakeActorMemory::kBase) return false;
    const uintptr_t relative = address - FakeActorMemory::kBase;
    if (relative > memory.bytes.size() || width > memory.bytes.size() - relative) return false;

    uint32_t numeric = 0;
    std::memcpy(&numeric, value, width);
    memory.writes.push_back({static_cast<size_t>(relative), numeric, width});
    std::memcpy(memory.bytes.data() + relative, value, width);
    return true;
}

MemoryIo MemoryIoFor(FakeActorMemory& memory) {
    return MemoryIo{&memory, &ReadMemory, &WriteMemory};
}

void InitializeActor(FakeActorMemory& memory) {
    memory.Store<uint16_t>(kFormationIdOffset, 0x1003u);
    memory.Store<uint32_t>(kMaxHpOffset, 100u);
    memory.Store<uint32_t>(kMaxMpOffset, 40u);
    memory.Store<uint32_t>(kOverkillOffset, 50u);
    for (size_t index = 0; index < kStatOffsets.size(); ++index) {
        memory.Store<uint8_t>(kStatOffsets[index], static_cast<uint8_t>(10u + index));
    }
    memory.Store<uint32_t>(kCurrentHpOffset, 21u);
    memory.Store<uint32_t>(kCurrentMpOffset, 10u);
}

DifficultyConfig DifficultyEnabled() {
    DifficultyConfig config = MakeNeutralConfig();
    config.global.enabled = true;
    config.global.hpMul = 1250;
    config.global.mpMul = 1500;
    config.global.overkillMul = 2000;
    config.global.strMul = 2000;
    config.global.defMul = 2000;
    config.global.magMul = 2000;
    config.global.mdfMul = 2000;
    config.global.agiMul = 2000;
    config.global.lckMul = 2000;
    config.global.evaMul = 2000;
    config.global.accMul = 2000;
    return config;
}

FfxHooks::SinRam::RuntimeRequest ValidSinRequest(int threatLevel = 1) {
    using namespace FfxHooks::SinRam;

    RuntimeRequest request{};
    request.config.enabled = true;
    request.config.threatLevel = threatLevel;
    request.encounterToken = 0x01360000u;
    request.origin = EncounterOrigin::Natural;
    request.transitionCallerRva = 0x00471CEFu;
    request.transitionRequestId = 0x1020304050607080ull;
    request.actorRequestId = 0x1020304050607080ull;
    request.transitionGeneration = 7u;
    return request;
}

void ExpectActor(
    const FakeActorMemory& memory,
    uint32_t maxHp, uint32_t currentHp,
    uint32_t maxMp, uint32_t currentMp,
    uint32_t overkill, const std::array<uint8_t, kStatCount>& stats,
    const char* message) {
    bool matches = memory.Load<uint32_t>(kMaxHpOffset) == maxHp &&
                   memory.Load<uint32_t>(kCurrentHpOffset) == currentHp &&
                   memory.Load<uint32_t>(kMaxMpOffset) == maxMp &&
                   memory.Load<uint32_t>(kCurrentMpOffset) == currentMp &&
                   memory.Load<uint32_t>(kOverkillOffset) == overkill;
    for (size_t index = 0; index < stats.size(); ++index) {
        matches = matches && memory.Load<uint8_t>(kStatOffsets[index]) == stats[index];
    }
    Expect(matches, message);
}

void TestStructuralPlanLeavesCurrentHpToTheSingleWriter() {
    using namespace FfxHooks::SinRam;

    const RuntimeRequest request = ValidSinRequest();
    DifficultyValues afterDifficulty{};
    afterDifficulty.maxHp = 125u;
    afterDifficulty.overkill = 100u;
    afterDifficulty.stats = {{20u, 22u, 24u, 26u, 28u, 30u, 32u, 34u}};

    const StructuralScalePlan structural = BuildRuntimeStructuralScalePlan(
        request, 7u, 310, 0x1003u, afterDifficulty);
    const std::array<uint8_t, 8> expectedStats =
        {{22u, 24u, 26u, 28u, 30u, 32u, 34u, 36u}};
    Expect(structural.admitted && structural.writeback.maxHp == 137u &&
               structural.writeback.overkill == 110u &&
               structural.writeback.stats == expectedStats,
           "the structural S.I.N. stage must consume Difficulty targets before HP ratio work");
    Expect(StructuralWritebackInDomain(structural.writeback),
           "every admitted typed runtime plan must remain inside the signed actor-field domain");
}

void TestNeitherModePerformsNoWrite() {
    FakeActorMemory memory{};
    InitializeActor(memory);
    Runtime runtime{};
    runtime.BeginGeneration(7u);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    const RuntimeResult result = runtime.UpdateComposed(
        MemoryIoFor(memory), MakeNeutralConfig(), true, 310,
        FfxHooks::SinRam::RuntimeRequest{}, &actor, 1u);

    Expect(memory.writes.empty(),
           "neither Difficulty nor S.I.N. may publish an actor write");
    Expect(result.code == ResultCode::NoActors,
           "a fresh disabled composition must retain the existing restore/no-actor result");
}

void TestDifficultyOnlyModePreservesExistingBehavior() {
    FakeActorMemory memory{};
    InitializeActor(memory);
    Runtime runtime{};
    runtime.BeginGeneration(7u);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    const RuntimeResult result = runtime.UpdateComposed(
        MemoryIoFor(memory), DifficultyEnabled(), true, 310,
        FfxHooks::SinRam::RuntimeRequest{}, &actor, 1u);

    ExpectActor(memory, 125u, 26u, 60u, 15u, 100u,
                {{20u, 22u, 24u, 26u, 28u, 30u, 32u, 34u}},
                "Difficulty-only composition must retain its exact HP, MP, overkill, and stat targets");
    Expect(result.code == ResultCode::Applied &&
               memory.WriteCount(kCurrentHpOffset) == 1u &&
               memory.WriteCount(kCurrentMpOffset) == 1u,
           "Difficulty-only must preserve one current write for each owned maximum pair");
}

void TestSinOnlyModeLeavesMpUntouched() {
    FakeActorMemory memory{};
    InitializeActor(memory);
    Runtime runtime{};
    runtime.BeginGeneration(7u);
    const FfxHooks::SinRam::RuntimeRequest sin = ValidSinRequest(1);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    const RuntimeResult result = runtime.UpdateComposed(
        MemoryIoFor(memory), MakeNeutralConfig(), true, 310,
        sin, &actor, 1u);

    ExpectActor(memory, 110u, 23u, 40u, 10u, 55u,
                {{11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u}},
                "S.I.N.-only composition must scale HP, overkill, and stats while leaving MP unchanged");
    Expect(result.code == ResultCode::Applied &&
               memory.WriteCount(kCurrentHpOffset) == 1u &&
               memory.WriteCount(kMaxMpOffset) == 0u &&
               memory.WriteCount(kCurrentMpOffset) == 0u,
           "S.I.N.-only must admit once, calculate HP once, and expose no MP write surface");
}

void TestReportedSnowfieldEncounterUsesPreviewAndDifficulty() {
    for(bool difficultyOn:{false,true}){
        FakeActorMemory memory{};InitializeActor(memory);
        memory.Store<uint16_t>(kFormationIdOffset,0x1013u);
        memory.Store<uint32_t>(kMaxHpOffset,1000);memory.Store<uint32_t>(kCurrentHpOffset,500);
        memory.Store<uint32_t>(kOverkillOffset,1000);
        for(auto offset:kStatOffsets)memory.Store<uint8_t>(offset,20);
        Runtime runtime{};runtime.BeginGeneration(7);
        auto sin=ValidSinRequest();sin.config.seeded=true;sin.config.seed=1259714269u;sin.config.distribution=80;
        sin.areaVisit=1;sin.encounterToken=(333u<<16)|1u;sin.nativeFieldRow=43;
        sin.scriptManaged=true;sin.scriptActorMask=1;
        auto config=MakeNeutralConfig();config.global.enabled=difficultyOn;
        config.global.hpMul=config.global.overkillMul=2000;
        config.global.strMul=config.global.defMul=config.global.magMul=config.global.mdfMul=2000;
        config.global.agiMul=config.global.accMul=config.global.evaMul=config.global.lckMul=2000;
        const ActorRef actor{FakeActorMemory::kBase,0};
        const auto result=runtime.UpdateComposed(MemoryIoFor(memory),config,true,43,sin,&actor,1);
        const unsigned expectedHp=difficultyOn?2400:1200;const unsigned expectedStat=difficultyOn?46:24;
        bool stats=true;for(auto offset:kStatOffsets)stats=stats&&memory.Load<uint8_t>(offset)==expectedStat;
        Expect(result.code==ResultCode::Applied && memory.Load<uint32_t>(kMaxHpOffset)==expectedHp &&
            memory.Load<uint32_t>(kCurrentHpOffset)==expectedHp/2 && stats,
            "reported field333/row43/seed1259714269 Flan receives T2 scaling after optional Difficulty");
        const auto writes=memory.writes.size();runtime.UpdateComposed(MemoryIoFor(memory),config,true,43,sin,&actor,1);
        Expect(memory.writes.size()==writes && memory.Load<uint32_t>(kMaxHpOffset)==expectedHp,
            "repeating the exact reported encounter does not compound its bonuses");
    }
}

void TestManagedScriptsGateOnlyTheirRegisteredActor() {
    FakeActorMemory memory{};InitializeActor(memory);
    Runtime runtime{};runtime.BeginGeneration(7u);
    auto sin=ValidSinRequest(1);sin.scriptManaged=true;sin.scriptActorMask=0;
    const ActorRef actor{FakeActorMemory::kBase,0u};
    runtime.UpdateComposed(MemoryIoFor(memory),MakeNeutralConfig(),true,310,sin,&actor,1u);
    Expect(memory.writes.empty(),"managed S.I.N. cannot grant stats before a matching script registration succeeds");
    sin.scriptActorMask=2;
    runtime.UpdateComposed(MemoryIoFor(memory),MakeNeutralConfig(),true,310,sin,&actor,1u);
    Expect(memory.writes.empty(),"another actor slot's script cannot authorize this actor's stats");
    sin.scriptActorMask=1;
    runtime.UpdateComposed(MemoryIoFor(memory),MakeNeutralConfig(),true,310,sin,&actor,1u);
    ExpectActor(memory,110u,23u,40u,10u,55u,{{11u,12u,13u,14u,15u,16u,17u,18u}},
        "registered scripts compose through the same single HP/stat writer");
}

void TestBothModeComposesBeforeOneHpRatio() {
    FakeActorMemory memory{};
    InitializeActor(memory);
    Runtime runtime{};
    runtime.BeginGeneration(7u);
    const FfxHooks::SinRam::RuntimeRequest sin = ValidSinRequest(1);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    const RuntimeResult result = runtime.UpdateComposed(
        MemoryIoFor(memory), DifficultyEnabled(), true, 310,
        sin, &actor, 1u);

    ExpectActor(memory, 137u, 29u, 60u, 15u, 110u,
                {{22u, 24u, 26u, 28u, 30u, 32u, 34u, 36u}},
                "combined mode must apply baseline, Difficulty, then S.I.N. before final HP ratio");
    Expect(result.code == ResultCode::Applied &&
               memory.WriteCount(kCurrentHpOffset) == 1u &&
               memory.FirstWrite(kMaxHpOffset) < memory.FirstWrite(kCurrentHpOffset),
           "combined mode must publish final maximum before exactly one current-HP write");
    Expect(memory.WriteCount(kMaxMpOffset) == 1u &&
               memory.WriteCount(kCurrentMpOffset) == 1u,
           "combined mode must leave the MP pair exclusively under Difficulty ownership");
}

void TestThreatZeroAndRejectedAdmissionRemainNeutral() {
    FakeActorMemory neutralMemory{};
    InitializeActor(neutralMemory);
    Runtime neutralRuntime{};
    neutralRuntime.BeginGeneration(7u);
    const FfxHooks::SinRam::RuntimeRequest neutralSin = ValidSinRequest(0);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    const RuntimeResult neutral = neutralRuntime.UpdateComposed(
        MemoryIoFor(neutralMemory), MakeNeutralConfig(), true, 310,
        neutralSin, &actor, 1u);
    Expect(neutral.code == ResultCode::Applied &&
               neutralMemory.writes.empty(),
           "threat zero must be admitted but remain structurally neutral");

    FakeActorMemory rejectedMemory{};
    InitializeActor(rejectedMemory);
    Runtime rejectedRuntime{};
    rejectedRuntime.BeginGeneration(7u);
    FfxHooks::SinRam::RuntimeRequest rejectedSin = ValidSinRequest(1);
    rejectedSin.transitionCallerRva = 0x00381D8Du;
    rejectedRuntime.UpdateComposed(
        MemoryIoFor(rejectedMemory), MakeNeutralConfig(), true, 310,
        rejectedSin, &actor, 1u);
    Expect(rejectedMemory.writes.empty(),
           "a non-natural caller must not turn S.I.N. rejection into an actor write");

    FakeActorMemory difficultyMemory{};
    InitializeActor(difficultyMemory);
    Runtime difficultyRuntime{};
    difficultyRuntime.BeginGeneration(7u);
    FfxHooks::SinRam::RuntimeRequest staleSin = ValidSinRequest(1);
    ++staleSin.transitionGeneration;
    difficultyRuntime.UpdateComposed(
        MemoryIoFor(difficultyMemory), DifficultyEnabled(), true, 310,
        staleSin, &actor, 1u);
    ExpectActor(difficultyMemory, 125u, 26u, 60u, 15u, 100u,
                {{20u, 22u, 24u, 26u, 28u, 30u, 32u, 34u}},
                "a rejected stale S.I.N. observation must not suppress valid Difficulty targets");
}

void TestTypedDomainAndCorrelationFailClosed() {
    using namespace FfxHooks::SinRam;

    DifficultyValues huge{};
    huge.maxHp = 0xFFFFFFFFu;
    huge.overkill = 0xFFFFFFFFu;
    huge.stats.fill(255u);
    const StructuralScalePlan saturated = BuildRuntimeStructuralScalePlan(
        ValidSinRequest(2), 7u, 310, 0x1003u, huge);
    Expect(saturated.admitted && saturated.writeback.maxHp == 0x7FFFFFFFu &&
               saturated.writeback.overkill == 0x7FFFFFFFu &&
               StructuralWritebackInDomain(saturated.writeback),
           "the closed runtime plan must saturate dword outputs before Runtime can own them");

    DifficultyValues forged = saturated.writeback;
    forged.maxHp = 0x80000000u;
    Expect(!StructuralWritebackInDomain(forged),
           "the final Runtime domain gate must reject a forged signed-dword overflow");

    const DifficultyValues ordinary{125u, 100u,
        {{20u, 22u, 24u, 26u, 28u, 30u, 32u, 34u}}};
    RuntimeRequest mismatchedRequest = ValidSinRequest(1);
    ++mismatchedRequest.actorRequestId;
    const StructuralScalePlan requestMismatch = BuildRuntimeStructuralScalePlan(
        mismatchedRequest, 7u, 310, 0x1003u, ordinary);
    RuntimeRequest staleGeneration = ValidSinRequest(1);
    ++staleGeneration.transitionGeneration;
    const StructuralScalePlan generationMismatch = BuildRuntimeStructuralScalePlan(
        staleGeneration, 7u, 310, 0x1003u, ordinary);
    const StructuralScalePlan fieldMismatch = BuildRuntimeStructuralScalePlan(
        ValidSinRequest(1), 7u, 340, 0x1003u, ordinary);
    Expect(!requestMismatch.admitted &&
               requestMismatch.reason == AdmissionReason::RequestMismatch &&
               !generationMismatch.admitted &&
               generationMismatch.reason == AdmissionReason::StaleObservation &&
               !fieldMismatch.admitted &&
               fieldMismatch.reason == AdmissionReason::RuntimeFieldMismatch,
           "request identity, Runtime generation, and Runtime field must bind the typed admission");

    FakeActorMemory invalidMemory{};
    InitializeActor(invalidMemory);
    Runtime invalidRuntime{};
    invalidRuntime.BeginGeneration(7u);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    RuntimeRequest invalidThreat = ValidSinRequest(3);
    invalidRuntime.UpdateComposed(
        MemoryIoFor(invalidMemory), MakeNeutralConfig(), true, 310,
        invalidThreat, &actor, 1u);
    ExpectActor(invalidMemory, 100u, 21u, 40u, 10u, 50u,
                {{10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u}},
                "an invalid typed request must leave every actor value unchanged");
    Expect(invalidMemory.writes.empty(),
           "an invalid typed request may not acquire structural ownership");
}

void TestClosedTypedSourceContracts() {
    const std::filesystem::path testPath = std::filesystem::path(__FILE__);
    const std::filesystem::path root = testPath.parent_path().parent_path();
    const std::string difficultyHeader =
        ReadText(root / "hooks" / "F7DifficultyCore.h");
    const std::string difficultySource =
        ReadText(root / "hooks" / "F7DifficultyCore.cpp");
    const std::string sinHeader =
        ReadText(root / "hooks" / "SinRamScalingCore.h");
    const std::string sinSource =
        ReadText(root / "hooks" / "SinRamScalingCore.cpp");
    Expect(!difficultyHeader.empty() && !difficultySource.empty() &&
               !sinHeader.empty() && !sinSource.empty(),
           "the four production core files must be readable for closed-surface contracts");

    const std::string typedRequest = SliceBetween(
        sinHeader, "struct RuntimeRequest", "struct HpRatioIdentity");
    const char* forbiddenRequestTokens[] = {
        "void*", "context", "callback", "compose", "address", "MemoryIo",
        "preDifficultyHp", "currentHp", "maxMp", "currentMp", "monsterId",
        "afterDifficulty",
    };
    bool requestSurfaceClosed = !typedRequest.empty();
    for (const char* token : forbiddenRequestTokens) {
        requestSurfaceClosed =
            requestSurfaceClosed && typedRequest.find(token) == std::string::npos;
    }
    Expect(requestSurfaceClosed,
           "the typed runtime request source must expose only admission metadata");
    Expect(difficultyHeader.find("ActorStructuralComposeFn") == std::string::npos &&
               difficultyHeader.find("ActorStructuralComposer") == std::string::npos &&
               difficultySource.find("composer.compose") == std::string::npos &&
               difficultySource.find("composer.context") == std::string::npos,
           "the generic opaque callback and arbitrary structural output channel must be absent");

    const std::string update = SliceBetween(
        difficultySource, "RuntimeResult Runtime::UpdateComposed(",
        "RuntimeResult Runtime::Restore(");
    const size_t closedPlan = update.find("BuildRuntimeStructuralScalePlan(");
    const size_t domainGate = update.find("StructuralWritebackInDomain(");
    const size_t acceptMaximum =
        update.find("desired[kMaxHpField] = plan.writeback.maxHp");
    const size_t firstWrite = update.find("WriteValue(");
    Expect(!update.empty() && closedPlan != std::string::npos &&
               domainGate != std::string::npos && acceptMaximum != std::string::npos &&
               firstWrite != std::string::npos && closedPlan < domainGate &&
               domainGate < acceptMaximum && domainGate < firstWrite,
           "Runtime must validate the closed saturating plan before ownership or memory write");
    Expect(update.find("request.config.enabled") != std::string::npos &&
               update.find("generation_") != std::string::npos &&
               update.find("fieldRow") != std::string::npos &&
               update.find("formation") != std::string::npos,
           "Runtime must bind its own generation, field, and exact actor identity to admission");

    const std::string closedBuilder = SliceBetween(
        sinSource, "StructuralScalePlan BuildRuntimeStructuralScalePlan(",
        "ScalePlan BuildScalePlan(");
    Expect(!closedBuilder.empty() &&
               closedBuilder.find("scaleRequest.encounter.monsterId = formationId") !=
                   std::string::npos &&
               closedBuilder.find("scaleRequest.afterDifficulty = afterDifficulty") !=
                   std::string::npos &&
               closedBuilder.find("actorGeneration = runtimeGeneration") !=
                   std::string::npos &&
               closedBuilder.find("currentGeneration = runtimeGeneration") !=
                   std::string::npos,
           "the closed builder must derive actor values and freshness from Runtime-owned inputs");
}

void TestThirdPartyDriftAndRestoreRemainCompareSafe() {
    FakeActorMemory driftMemory{};
    InitializeActor(driftMemory);
    Runtime driftRuntime{};
    driftRuntime.BeginGeneration(7u);
    const FfxHooks::SinRam::RuntimeRequest driftSin = ValidSinRequest(1);
    const ActorRef actor{FakeActorMemory::kBase, 0u};
    driftRuntime.UpdateComposed(
        MemoryIoFor(driftMemory), DifficultyEnabled(), true, 310,
        driftSin, &actor, 1u);
    driftMemory.writes.clear();
    driftMemory.Store<uint32_t>(kMaxHpOffset, 999u);
    const RuntimeResult drift = driftRuntime.UpdateComposed(
        MemoryIoFor(driftMemory), DifficultyEnabled(), true, 310,
        driftSin, &actor, 1u);
    Expect(drift.code == ResultCode::OwnershipLost &&
               driftMemory.Load<uint32_t>(kMaxHpOffset) == 999u &&
               driftMemory.WriteCount(kMaxHpOffset) == 0u &&
               driftMemory.WriteCount(kCurrentHpOffset) == 0u,
           "combined mode must preserve third-party maximum drift without a compensating HP write");

    FakeActorMemory restoreMemory{};
    InitializeActor(restoreMemory);
    Runtime restoreRuntime{};
    restoreRuntime.BeginGeneration(7u);
    const FfxHooks::SinRam::RuntimeRequest restoreSin = ValidSinRequest(1);
    restoreRuntime.UpdateComposed(
        MemoryIoFor(restoreMemory), DifficultyEnabled(), true, 310,
        restoreSin, &actor, 1u);
    restoreMemory.Store<uint32_t>(kCurrentHpOffset, 14u);
    restoreMemory.Store<uint32_t>(kCurrentMpOffset, 12u);
    restoreMemory.writes.clear();
    const RuntimeResult restored = restoreRuntime.UpdateComposed(
        MemoryIoFor(restoreMemory), MakeNeutralConfig(), true, 310,
        FfxHooks::SinRam::RuntimeRequest{}, &actor, 1u);
    ExpectActor(restoreMemory, 100u, 10u, 40u, 8u, 50u,
                {{10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u}},
                "OFF must compare-restore structural baselines and preserve live HP and MP ratios");
    Expect(restored.code == ResultCode::Restored &&
               restoreMemory.WriteCount(kCurrentHpOffset) == 1u &&
               restoreMemory.WriteCount(kCurrentMpOffset) == 1u,
           "OFF restoration must retain exactly one compare-safe current write per owned pair");
}

}  // namespace

int main() {
    TestStructuralPlanLeavesCurrentHpToTheSingleWriter();
    TestNeitherModePerformsNoWrite();
    TestDifficultyOnlyModePreservesExistingBehavior();
    TestSinOnlyModeLeavesMpUntouched();
    TestReportedSnowfieldEncounterUsesPreviewAndDifficulty();
    TestManagedScriptsGateOnlyTheirRegisteredActor();
    TestBothModeComposesBeforeOneHpRatio();
    TestThreatZeroAndRejectedAdmissionRemainNeutral();
    TestTypedDomainAndCorrelationFailClosed();
    TestClosedTypedSourceContracts();
    TestThirdPartyDriftAndRestoreRemainCompareSafe();

    std::printf(
        "S.I.N./Difficulty composition RT0: %d/%d passed\n",
        g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
