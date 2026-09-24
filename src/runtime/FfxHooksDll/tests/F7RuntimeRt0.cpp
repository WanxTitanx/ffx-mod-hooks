#include "../hooks/F7DifficultyCore.h"
#include "../hooks/MinHookBatchCoordinator.h"
#include "../hooks/SinNaturalCore.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace FfxHooks::F7Difficulty;

int g_checks = 0;
int g_failures = 0;

void Expect(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

void ExpectNeutral(const Preset& preset, const char* context) {
    const bool multipliersNeutral =
        preset.hpMul == 1000 && preset.mpMul == 1000 && preset.strMul == 1000 &&
        preset.defMul == 1000 && preset.magMul == 1000 && preset.mdfMul == 1000 &&
        preset.agiMul == 1000 && preset.accMul == 1000 && preset.evaMul == 1000 &&
        preset.lckMul == 1000 && preset.overkillMul == 1000;
    bool resistancesNeutral = true;
    for (uint8_t value : preset.statusResist) resistancesNeutral &= value == 0;
    Expect(!preset.enabled, context);
    Expect(multipliersNeutral, "neutral preset must set every multiplier to exactly 1000");
    Expect(preset.autoStatusMask == 0 && preset.elemWeak == 0 &&
           preset.elemResist == 0 && preset.elemAbsorb == 0,
           "neutral preset must clear all optional status and element masks");
    Expect(resistancesNeutral, "neutral preset must clear all 25 status resistance bytes");
}

void TestNeutralDefaults() {
    const Preset preset = MakeNeutralPreset();
    ExpectNeutral(preset, "neutral preset must be disabled");
    const DifficultyConfig config = MakeNeutralConfig();
    ExpectNeutral(config.global, "neutral global preset must be disabled");
    Expect(!config.byArea && config.areaCount == 0,
           "neutral config must disable area replacement and contain no rules");
}

void TestStrictBooleanAndMalformedInput() {
    DifficultyConfig config{};
    const char falseThenTrue[] =
        "{\"diff_enabled\":false,\"unrelated\":true,\"version\":1}";
    ConfigResult result = ParseConfig(falseThenTrue, sizeof(falseThenTrue) - 1, &config);
    Expect(result.code == ConfigCode::Ok, "exact false followed by unrelated true must parse");
    Expect(!config.global.enabled,
           "diff_enabled=false must not scan forward into an unrelated true token");

    const char truncated[] = "{\"diff_enabled\":tru";
    config.global.enabled = true;
    result = ParseConfig(truncated, sizeof(truncated) - 1, &config);
    Expect(result.code == ConfigCode::Malformed,
           "truncated boolean token must fail as malformed JSON");
    ExpectNeutral(config.global, "failed parse must return a neutral output preset");

    const char malformedNumber[] = "{\"diff_hpMul\":10oops}";
    result = ParseConfig(malformedNumber, sizeof(malformedNumber) - 1, &config);
    Expect(result.code == ConfigCode::Malformed,
           "numeric tokens with trailing characters must fail closed");
}

void TestStatusResistanceArraysAndCanaries() {
    struct GuardedConfig {
        std::array<uint8_t, 32> before;
        DifficultyConfig config;
        std::array<uint8_t, 32> after;
    } guarded{};
    guarded.before.fill(0xA5);
    guarded.after.fill(0x5A);

    const char exact[] =
        "{\"diff_statusResist\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
        "16,17,18,19,20,21,22,23,24]}";
    ConfigResult result = ParseConfig(exact, sizeof(exact) - 1, &guarded.config);
    Expect(result.code == ConfigCode::Ok, "an exact 25-byte resistance array must parse");
    for (size_t i = 0; i < kStatusCount; ++i) {
        Expect(guarded.config.global.statusResist[i] == static_cast<uint8_t>(i),
               "exact status resistance array value must be preserved");
    }
    bool canariesIntact = true;
    for (uint8_t value : guarded.before) canariesIntact &= value == 0xA5;
    for (uint8_t value : guarded.after) canariesIntact &= value == 0x5A;
    Expect(canariesIntact, "parser must not write across the guarded config object");

    const char shortArray[] = "{\"diff_statusResist\":[9,8]}";
    result = ParseConfig(shortArray, sizeof(shortArray) - 1, &guarded.config);
    Expect(result.code == ConfigCode::Ok, "short resistance arrays must parse");
    Expect(guarded.config.global.statusResist[0] == 9 &&
           guarded.config.global.statusResist[1] == 8,
           "short resistance array values must be preserved");
    bool zeroFilled = true;
    for (size_t i = 2; i < kStatusCount; ++i) zeroFilled &= guarded.config.global.statusResist[i] == 0;
    Expect(zeroFilled, "short resistance arrays must zero-fill the remaining entries");

    const char longArray[] =
        "{\"diff_statusResist\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}";
    result = ParseConfig(longArray, sizeof(longArray) - 1, &guarded.config);
    Expect(result.code == ConfigCode::TooManyArrayItems,
           "resistance arrays longer than 25 bytes must fail closed");
}

void TestRangesUnknownKeysAndInputCap() {
    DifficultyConfig config{};
    const char clamped[] =
        "{\"diff_enabled\":true,\"diff_hpMul\":1,\"diff_mpMul\":20000,"
        "\"diff_strMul\":99999,\"diff_defMul\":-10,\"unknown\":{\"nested\":[true,null,3]}}";
    ConfigResult result = ParseConfig(clamped, sizeof(clamped) - 1, &config);
    Expect(result.code == ConfigCode::Ok && result.clamped,
           "documented multiplier ranges must clamp and report the clamp");
    Expect(config.global.enabled && config.global.hpMul == 100 && config.global.mpMul == 10000 &&
           config.global.strMul == 5000 && config.global.defMul == 100,
           "multiplier clamps must use the documented HP/MP/overkill and stat ranges");

    const char invalidMask[] = "{\"diff_elemWeak\":32}";
    result = ParseConfig(invalidMask, sizeof(invalidMask) - 1, &config);
    Expect(result.code == ConfigCode::OutOfRange,
           "element masks with unknown high bits must fail validation");

    std::string tooLarge(kMaxJsonBytes + 1, ' ');
    result = ParseConfig(tooLarge.data(), tooLarge.size(), &config);
    Expect(result.code == ConfigCode::TooLarge,
           "difficulty JSON input over the fixed cap must fail before parsing");
}

bool PresetsEqual(const Preset& a, const Preset& b) {
    return a.enabled == b.enabled && a.hpMul == b.hpMul && a.mpMul == b.mpMul &&
           a.strMul == b.strMul && a.defMul == b.defMul && a.magMul == b.magMul &&
           a.mdfMul == b.mdfMul && a.agiMul == b.agiMul && a.accMul == b.accMul &&
           a.evaMul == b.evaMul && a.lckMul == b.lckMul &&
           a.overkillMul == b.overkillMul && a.autoStatusMask == b.autoStatusMask &&
           a.elemWeak == b.elemWeak && a.elemResist == b.elemResist &&
           a.elemAbsorb == b.elemAbsorb && a.statusResist == b.statusResist;
}

void TestExactRoundTrip() {
    DifficultyConfig source = MakeNeutralConfig();
    source.global.enabled = true;
    source.global.hpMul = 4321;
    source.global.mpMul = 9876;
    source.global.strMul = 1101;
    source.global.defMul = 1202;
    source.global.magMul = 1303;
    source.global.mdfMul = 1404;
    source.global.agiMul = 1505;
    source.global.accMul = 1606;
    source.global.evaMul = 1707;
    source.global.lckMul = 1808;
    source.global.overkillMul = 7654;
    source.global.autoStatusMask = 0x01020304u;
    source.global.elemWeak = 1;
    source.global.elemResist = 2;
    source.global.elemAbsorb = 4;
    for (size_t i = 0; i < kStatusCount; ++i) source.global.statusResist[i] = static_cast<uint8_t>(24 - i);
    source.byArea = true;
    source.areaCount = 1;
    source.areas[0].enabled = true;
    source.areas[0].fieldRow = 42;
    source.areas[0].preset = source.global;
    source.areas[0].preset.hpMul = 2222;

    std::array<char, kMaxJsonBytes + 1> json{};
    size_t length = 0;
    const ConfigResult save = SerializeConfig(source, json.data(), json.size(), &length);
    Expect(save.code == ConfigCode::Ok && length > 0,
           "difficulty config serialization must fit the bounded output buffer");

    DifficultyConfig loaded{};
    const ConfigResult load = ParseConfig(json.data(), length, &loaded);
    Expect(load.code == ConfigCode::Ok, "serialized difficulty config must parse");
    Expect(PresetsEqual(source.global, loaded.global),
           "save-load round trip must preserve enabled, MP, overkill, all multipliers, masks, and 25 resistances");
    Expect(loaded.byArea && loaded.areaCount == 1 && loaded.areas[0].enabled &&
           loaded.areas[0].fieldRow == 42 &&
           PresetsEqual(source.areas[0].preset, loaded.areas[0].preset),
           "save-load round trip must preserve the complete area replacement preset");
}

struct PersistenceSpy {
    int reads = 0;
    int writes = 0;
    std::string lastPath;
    std::string stored = "{\"diff_enabled\":false}";
};

bool SpyRead(void* context, const char* path, char* output, size_t capacity, size_t* lengthOut) {
    PersistenceSpy& spy = *static_cast<PersistenceSpy*>(context);
    ++spy.reads;
    spy.lastPath = path ? path : "";
    if (spy.stored.size() > capacity) return false;
    std::memcpy(output, spy.stored.data(), spy.stored.size());
    *lengthOut = spy.stored.size();
    return true;
}

bool SpyWrite(void* context, const char* path, const char* data, size_t length) {
    PersistenceSpy& spy = *static_cast<PersistenceSpy*>(context);
    ++spy.writes;
    spy.lastPath = path ? path : "";
    spy.stored.assign(data, length);
    return true;
}

void TestPersistenceAllowlist() {
    PersistenceSpy spy{};
    const PersistenceIo io{&spy, &SpyRead, &SpyWrite};
    std::array<char, kMaxJsonBytes + 1> buffer{};
    size_t length = 0;
    const char allowed[] = "C:\\FFX\\modules\\config\\f7_inlive.json";
    Expect(ReadDocument(io, allowed, buffer.data(), buffer.size(), &length) == PersistenceCode::Ok,
           "difficulty persistence must admit only its bounded auxiliary JSON path");
    Expect(WriteDocument(io, allowed, buffer.data(), length) == PersistenceCode::Ok,
           "difficulty persistence must write its allowed JSON through the atomic adapter");
    Expect(spy.reads == 1 && spy.writes == 1 && spy.lastPath == allowed,
           "difficulty persistence must make exactly the requested JSON I/O calls");

    const std::array<const char*, 6> forbidden = {{
        "C:\\FFX\\data\\battle\\mon\\m001.bin",
        "C:\\FFX\\PPSAVE1",
        "C:\\FFX\\FFX.exe",
        "C:\\FFX\\modules\\config\\f7_inlive.json.tmp",
        "C:\\FFX\\modules\\config\\..\\f7_inlive.json",
        "C:\\FFX\\modules\\f7_inlive.json"
    }};
    for (const char* path : forbidden) {
        Expect(ReadDocument(io, path, buffer.data(), buffer.size(), &length) == PersistenceCode::PathRejected,
               "difficulty persistence must reject non-allowlisted read paths");
        Expect(WriteDocument(io, path, "{}", 2) == PersistenceCode::PathRejected,
               "difficulty persistence must reject non-allowlisted write paths");
    }
    Expect(spy.reads == 1 && spy.writes == 1,
           "rejected bin, battle, save, executable, traversal, and temp leaves must never reach filesystem callbacks");
}

constexpr size_t kGuardBytes = 32;

struct ActorStorage {
    std::array<uint8_t, kActorSpan + kGuardBytes * 2> bytes{};
};

struct MemorySpy {
    std::array<ActorStorage, kActorSlots> actors{};
    int reads = 0;
    int writes = 0;
    uintptr_t failReadAddress = 0;
    uintptr_t failWriteAddress = 0;
    uintptr_t failReadAfterWriteAddress = 0;
    uintptr_t successfulNoopWriteAddress = 0;
    bool watchedWriteOccurred = false;
    int autoRefreshCalls = 0;
    bool failAutoRefresh = false;
};

bool CompleteAutoRefresh(void* context, uintptr_t) {
    MemorySpy& spy = *static_cast<MemorySpy*>(context);
    ++spy.autoRefreshCalls;
    return !spy.failAutoRefresh;
}

uintptr_t ActorAddress(MemorySpy& spy, size_t slot) {
    return reinterpret_cast<uintptr_t>(spy.actors[slot].bytes.data() + kGuardBytes);
}

bool ResolveMemory(
    MemorySpy& spy, uintptr_t address, size_t length, uint8_t** bytesOut, size_t* offsetOut) {
    if (!bytesOut || !offsetOut || length > kActorSpan) return false;
    for (size_t slot = 0; slot < spy.actors.size(); ++slot) {
        const uintptr_t base = ActorAddress(spy, slot);
        if (address >= base && address - base <= kActorSpan &&
            length <= kActorSpan - static_cast<size_t>(address - base)) {
            *bytesOut = reinterpret_cast<uint8_t*>(address);
            *offsetOut = static_cast<size_t>(address - base);
            return true;
        }
    }
    return false;
}

bool MemoryRead(void* context, uintptr_t address, void* output, size_t length) {
    MemorySpy& spy = *static_cast<MemorySpy*>(context);
    ++spy.reads;
    if (address == spy.failReadAddress) return false;
    if (address == spy.failReadAfterWriteAddress && spy.watchedWriteOccurred) return false;
    uint8_t* source = nullptr;
    size_t offset = 0;
    if (!output || !ResolveMemory(spy, address, length, &source, &offset)) return false;
    (void)offset;
    std::memcpy(output, source, length);
    return true;
}

bool MemoryWrite(void* context, uintptr_t address, const void* input, size_t length) {
    MemorySpy& spy = *static_cast<MemorySpy*>(context);
    ++spy.writes;
    if (address == spy.failWriteAddress) return false;
    uint8_t* destination = nullptr;
    size_t offset = 0;
    if (!input || !ResolveMemory(spy, address, length, &destination, &offset)) return false;
    (void)offset;
    if (address == spy.successfulNoopWriteAddress) return true;
    std::memcpy(destination, input, length);
    if (address == spy.failReadAfterWriteAddress) spy.watchedWriteOccurred = true;
    return true;
}

template <typename T>
void Put(MemorySpy& spy, size_t slot, size_t offset, T value) {
    std::memcpy(reinterpret_cast<void*>(ActorAddress(spy, slot) + offset), &value, sizeof(value));
}

template <typename T>
T Get(MemorySpy& spy, size_t slot, size_t offset) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(ActorAddress(spy, slot) + offset), sizeof(value));
    return value;
}

void InitializeActor(
    MemorySpy& spy, size_t slot, uint16_t formation, uint32_t maxHp, uint32_t currentHp,
    uint32_t maxMp, uint32_t currentMp, uint32_t overkill, uint8_t statBase) {
    spy.actors[slot].bytes.fill(0xCD);
    std::memset(reinterpret_cast<void*>(ActorAddress(spy, slot)), 0, kActorSpan);
    Put(spy, slot, kFormationIdOffset, formation);
    Put(spy, slot, kMaxHpOffset, maxHp);
    Put(spy, slot, kCurrentHpOffset, currentHp);
    Put(spy, slot, kMaxMpOffset, maxMp);
    Put(spy, slot, kCurrentMpOffset, currentMp);
    Put(spy, slot, kOverkillOffset, overkill);
    for (size_t i = 0; i < kStatCount; ++i) {
        Put(spy, slot, kStatOffsets[i], static_cast<uint8_t>(statBase + i));
    }
    Put(spy, slot, kElementAbsorbOffset, static_cast<uint8_t>(0x11));
    Put(spy, slot, kElementResistOffset, static_cast<uint8_t>(0x12));
    Put(spy, slot, kElementWeakOffset, static_cast<uint8_t>(0x13));
    Put(spy, slot, kUnsupportedCurrentHpScratchOffset, static_cast<uint32_t>(0x11223344u));
    Put(spy, slot, kUnsupportedCurrentMpScratchOffset, static_cast<uint32_t>(0x55667788u));
}

bool ActorCanariesIntact(const MemorySpy& spy, size_t slot) {
    for (size_t i = 0; i < kGuardBytes; ++i) {
        if (spy.actors[slot].bytes[i] != 0xCD ||
            spy.actors[slot].bytes[kGuardBytes + kActorSpan + i] != 0xCD) return false;
    }
    return true;
}

DifficultyConfig EnabledConfig(int hpMul) {
    DifficultyConfig config = MakeNeutralConfig();
    config.global.enabled = true;
    config.global.hpMul = hpMul;
    return config;
}

void TestTransactionalBaselineApplyEditAndRestore() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 7, 1000, 250, 100, 25, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(1);

    DifficultyConfig config = EnabledConfig(2000);
    config.global.mpMul = 1500;
    config.global.overkillMul = 1250;
    config.global.strMul = 1500;

    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied && result.actorsSeen == 1,
           "enabled validated fields must report one applied actor");
    Expect(Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000 &&
           Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500,
           "HP apply must scale max and preserve current percentage from baseline");
    Expect(Get<uint32_t>(spy, 0, kMaxMpOffset) == 150 &&
           Get<uint32_t>(spy, 0, kCurrentMpOffset) == 38,
           "MP apply must use checked half-up percentage rounding");
    Expect(Get<uint32_t>(spy, 0, kOverkillOffset) == 250,
           "validated overkill dword must scale from its baseline");
    Expect(Get<uint8_t>(spy, 0, kStatOffsets[0]) == 15,
           "validated stat bytes must scale with their exact width");
    Expect(Get<uint8_t>(spy, 0, kElementAbsorbOffset) == 0x11 &&
           Get<uint8_t>(spy, 0, kElementResistOffset) == 0x12 &&
           Get<uint8_t>(spy, 0, kElementWeakOffset) == 0x13,
           "neutral element settings must preserve native affinities");
    Expect(Get<uint32_t>(spy, 0, kUnsupportedCurrentHpScratchOffset) == 0x11223344u &&
           Get<uint32_t>(spy, 0, kUnsupportedCurrentMpScratchOffset) == 0x55667788u,
           "+0x6E4/+0x6E8 per-hit scratch snapshots must never be treated as current HP/MP");
    Expect(ActorCanariesIntact(spy, 0), "actor apply must stay within the exact 0xF90 actor span");

    const int writesAfterFirstApply = spy.writes;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied && spy.writes == writesAfterFirstApply,
           "repeated Apply with the same preset must be idempotent and perform no writes");

    config.global.hpMul = 3000;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
           Get<uint32_t>(spy, 0, kMaxHpOffset) == 3000 &&
           Get<uint32_t>(spy, 0, kCurrentHpOffset) == 750,
           "live edit must recompute from immutable vanilla baseline, not already-scaled RAM");

    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Restored &&
           Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000 &&
           Get<uint32_t>(spy, 0, kCurrentHpOffset) == 250 &&
           Get<uint32_t>(spy, 0, kMaxMpOffset) == 100 &&
           Get<uint32_t>(spy, 0, kCurrentMpOffset) == 25 &&
           Get<uint32_t>(spy, 0, kOverkillOffset) == 200 &&
           Get<uint8_t>(spy, 0, kStatOffsets[0]) == 10,
           "OFF must restore every still-owned validated field exactly");
    const int writesAfterRestore = spy.writes;
    result = runtime.Restore(io);
    Expect(result.code == ResultCode::Restored && spy.writes == writesAfterRestore,
           "a second teardown restore must be idempotent");
}

void TestAreaReplacementAndActorReuse() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 11, 1000, 1000, 10, 10, 50, 20);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(9);
    DifficultyConfig config = EnabledConfig(2000);
    config.byArea = true;
    config.areaCount = 1;
    config.areas[0].enabled = true;
    config.areas[0].fieldRow = 42;
    config.areas[0].preset = MakeNeutralPreset();
    config.areas[0].preset.enabled = true;
    config.areas[0].preset.hpMul = 1500;

    RuntimeResult result = runtime.Update(io, config, true, 42, &actor, 1);
    Expect(result.code == ResultCode::Applied && Get<uint32_t>(spy, 0, kMaxHpOffset) == 1500,
           "a matching enabled area preset must replace, never compound with, the global preset");

    InitializeActor(spy, 0, 12, 500, 250, 20, 10, 80, 30);
    result = runtime.Update(io, config, true, 42, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
           Get<uint32_t>(spy, 0, kMaxHpOffset) == 750 &&
           Get<uint32_t>(spy, 0, kCurrentHpOffset) == 375,
           "same-slot actor identity reuse must capture a fresh immutable baseline");
}

void TestRestoreRejectsReusedActorIdentity() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 18, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(10);
    const DifficultyConfig config = EnabledConfig(2000);
    const RuntimeResult applied = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(applied.code == ResultCode::Applied &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u,
           "identity-loss setup must first own the original actor's scaled HP");

    // Match the prior modified values deliberately: formation identity, not coincidental
    // structural bytes, must prevent restoration into a reused slot.
    InitializeActor(spy, 0, 19, 2000, 1000, 100, 50, 200, 10);
    const int writesBeforeRestore = spy.writes;
    const RuntimeResult restored = runtime.Restore(io);
    Expect(restored.code == ResultCode::OwnershipLost &&
               restored.ownershipLost == 1 && spy.writes == writesBeforeRestore &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u,
           "OFF must never restore captured bytes into a different formation identity");
}

void TestBattleFieldPublicationIsOneShotAndConsecutive() {
    BattleFieldPublication publication{};
    const BattleFieldTicket missing = publication.Consume();
    Expect(missing.fieldRow == -1 && missing.source == BattleFieldSource::Missing,
           "a generation without a publication must consume an explicit missing-field ticket");

    const std::array<BattleFieldTicket, 5> expected = {{
        {10, BattleFieldSource::Natural},
        {20, BattleFieldSource::Force},
        {30, BattleFieldSource::Arena},
        {40, BattleFieldSource::Ultra},
        {50, BattleFieldSource::CustomMix},
    }};
    for (const BattleFieldTicket& ticket : expected) {
        publication.Publish(ticket.fieldRow, ticket.source);
        const BattleFieldTicket consumed = publication.Consume();
        Expect(consumed.fieldRow == ticket.fieldRow && consumed.source == ticket.source,
               "each natural or explicit launch must publish its own next-generation field");
        const BattleFieldTicket consumedAgain = publication.Consume();
        Expect(consumedAgain.fieldRow == -1 && consumedAgain.source == BattleFieldSource::Missing,
               "a consumed battle field must never leak into the following generation");
    }

    publication.Publish(-1, BattleFieldSource::Arena);
    BattleFieldTicket invalid = publication.Consume();
    Expect(invalid.fieldRow == -1 && invalid.source == BattleFieldSource::Missing,
           "a negative explicit field must fail closed instead of becoming a valid ticket");
    publication.Publish(99, BattleFieldSource::Missing);
    invalid = publication.Consume();
    Expect(invalid.fieldRow == -1 && invalid.source == BattleFieldSource::Missing,
           "a Missing source must not publish a reusable current-battle field");
    publication.Publish(99, static_cast<BattleFieldSource>(0xFFu));
    invalid = publication.Consume();
    Expect(invalid.fieldRow == -1 && invalid.source == BattleFieldSource::Missing,
           "an out-of-range source must not overlap request-generation bits");

    const BattleFieldRequest failed = publication.BeginRequest();
    publication.Cancel(failed);
    publication.Publish(61, BattleFieldSource::Natural);
    BattleFieldTicket afterFailed = publication.Consume();
    Expect(afterFailed.fieldRow == 61 && afterFailed.source == BattleFieldSource::Natural,
           "a failed explicit launch must not suppress or replace the following Natural route");

    const BattleFieldRequest direct = publication.BeginRequest();
    Expect(publication.Commit(direct, 62, BattleFieldSource::Arena),
           "a live direct-route request must commit exactly its correlated field");
    BattleFieldTicket directTicket = publication.Consume();
    Expect(directTicket.fieldRow == 62 && directTicket.source == BattleFieldSource::Arena,
           "a committed direct route must reach exactly one initializer generation");

    const BattleFieldRequest cancelled = publication.BeginRequest();
    Expect(publication.Commit(cancelled, 63, BattleFieldSource::CustomMix),
           "a queued explicit route must be cancellable after commit but before consumption");
    publication.Cancel(cancelled);
    publication.Publish(64, BattleFieldSource::Natural);
    Expect(!publication.Commit(cancelled, 65, BattleFieldSource::CustomMix),
           "a stale cancelled request must not overwrite a newer Natural publication");
    const BattleFieldTicket afterCancel = publication.Consume();
    Expect(afterCancel.fieldRow == 64 && afterCancel.source == BattleFieldSource::Natural,
           "a cancelled queued route followed by Natural must attribute the Natural field");
}

void TestExplicitCaptureSuppressionIsScoped() {
    EncounterCaptureSuppression suppression{};
    Expect(!suppression.Active(), "natural capture suppression must start inactive");
    suppression.Begin();
    Expect(suppression.Active(), "an explicit route must suppress synchronous Natural capture");
    suppression.Begin();
    suppression.End();
    Expect(suppression.Active(), "nested explicit routes must retain outer suppression ownership");
    suppression.End();
    Expect(!suppression.Active(),
           "leaving an explicit route must never suppress the next Natural route");
    suppression.End();
    Expect(!suppression.Active(), "an unmatched defensive end must remain fail-open for Natural capture");
}

void TestMissingBattleFieldUsesExplicitFallbackRule() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 29, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(14);
    DifficultyConfig config = EnabledConfig(2000);
    config.byArea = true;
    config.areaCount = 2;
    config.areas[0].enabled = true;
    config.areas[0].fieldRow = 77;
    config.areas[0].preset = EnabledConfig(3000).global;
    config.areas[1].enabled = true;
    config.areas[1].fieldRow = -1;
    config.areas[1].preset = EnabledConfig(1500).global;

    const RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 1500,
           "a missing current-battle field must use only the explicit -1 fallback rule");
}

void TestDamageStateSurvivesEditAndOff() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 30, 1000, 800, 200, 150, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(11);
    DifficultyConfig config = EnabledConfig(2000);

    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1600,
           "initial HP apply must preserve the vanilla 80 percent state");
    Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(1000));
    config.global.hpMul = 3000;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied && result.ownershipLost == 0 &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 3000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1500,
           "an edit after ordinary damage must preserve the live 50 percent HP state");

    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Restored && result.ownershipLost == 0 &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500,
           "OFF after damage must restore max HP while preserving live HP percentage");
}

void TestHealingStateSurvivesOff() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 31, 1000, 400, 200, 100, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(12);
    DifficultyConfig config = EnabledConfig(2000);
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(1800));
    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Restored && result.ownershipLost == 0 &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 900,
           "OFF after healing must preserve the live 90 percent HP state");
}

void TestOverhealClampsOnOff() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 33, 1000, 500, 200, 100, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(15);
    DifficultyConfig config = EnabledConfig(2000);
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(2500));
    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Restored &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000,
           "OFF must clamp an overhealed current HP value to the restored maximum");
}

void TestMpSpendStateSurvivesEditAndOff() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 32, 1000, 500, 200, 100, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(13);
    DifficultyConfig config = EnabledConfig(1000);
    config.global.mpMul = 2000;
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Put(spy, 0, kCurrentMpOffset, static_cast<uint32_t>(80));
    config.global.mpMul = 1500;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied && result.ownershipLost == 0 &&
               Get<uint32_t>(spy, 0, kMaxMpOffset) == 300 &&
               Get<uint32_t>(spy, 0, kCurrentMpOffset) == 60,
           "an edit after MP spend must preserve the live 20 percent MP state");
    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Restored && result.ownershipLost == 0 &&
               Get<uint32_t>(spy, 0, kMaxMpOffset) == 200 &&
               Get<uint32_t>(spy, 0, kCurrentMpOffset) == 40,
           "OFF after MP spend must preserve live MP percentage and remain within restored max MP");
}

void TestExactStatOffsetMapping() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 18, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(10);
    DifficultyConfig config = EnabledConfig(1000);
    config.global.strMul = 1100;
    config.global.defMul = 1200;
    config.global.magMul = 1300;
    config.global.mdfMul = 1400;
    config.global.agiMul = 1500;
    config.global.lckMul = 1600;
    config.global.evaMul = 1700;
    config.global.accMul = 1800;
    const RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    const std::array<uint8_t, kStatCount> expected = {{11, 13, 16, 18, 21, 24, 27, 31}};
    bool exact = result.code == ResultCode::Applied;
    for (size_t i = 0; i < kStatCount; ++i) {
        exact &= Get<uint8_t>(spy, 0, kStatOffsets[i]) == expected[i];
    }
    Expect(exact,
           "stat multipliers must map STR/DEF/MAG/MDF/AGI/LCK/EVA/ACC to +0x5A8..+0x5AF exactly");
}

void TestInvalidFormationOverflowAndFaults() {
    Runtime emptyRuntime{};
    DifficultyConfig disabled = MakeNeutralConfig();
    MemorySpy emptySpy{};
    const MemoryIo emptyIo{&emptySpy, &MemoryRead, &MemoryWrite};
    RuntimeResult emptyResult = emptyRuntime.Update(
        emptyIo, disabled, true, -1, nullptr, 0);
    Expect(emptyResult.code == ResultCode::NoActors,
           "OFF without a captured generation must report NoActors, not a false restoration");

    MemorySpy spy{};
    InitializeActor(spy, 0, 0xFFFFu, 1000, 500, 10, 5, 50, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(2);
    DifficultyConfig config = EnabledConfig(2000);
    const int writesBefore = spy.writes;
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::NoActors && spy.writes == writesBefore,
           "0xFFFF formation sentinel must be rejected without actor writes");

    InitializeActor(spy, 0, 9, 0x7FFFFFFFu, 0x3FFFFFFFu, 0x7FFFFFFFu,
                    0x3FFFFFFFu, 0x7FFFFFFFu, 250);
    runtime.BeginGeneration(3);
    config.global.hpMul = 10000;
    config.global.mpMul = 10000;
    config.global.overkillMul = 10000;
    config.global.strMul = 5000;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
           Get<uint32_t>(spy, 0, kMaxHpOffset) == 0x7FFFFFFFu &&
           Get<uint32_t>(spy, 0, kMaxMpOffset) == 0x7FFFFFFFu &&
           Get<uint32_t>(spy, 0, kOverkillOffset) == 0x7FFFFFFFu &&
           Get<uint8_t>(spy, 0, kStatOffsets[0]) == 255,
           "overflow paths must clamp to the exact signed dword and byte widths");

    runtime.BeginGeneration(4);
    InitializeActor(spy, 0, 10, 1000, 500, 100, 50, 200, 10);
    spy.failReadAddress = ActorAddress(spy, 0) + kMaxHpOffset;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Fault && result.faults > 0,
           "a validated-field read failure must return Fault without a success claim");
    spy.failReadAddress = 0;

    result = runtime.Update(io, config, false, -1, &actor, 1);
    Expect(result.code == ResultCode::InvalidConfig,
           "invalid configuration must fail closed before actor mutation");
    result = runtime.Update(io, config, true, -1, nullptr, 0);
    Expect(result.code == ResultCode::NoActors,
           "an empty actor set must return NoActors instead of Applied");
}

void TestPartialOwnershipLoss() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 15, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(5);
    DifficultyConfig config = EnabledConfig(2000);
    config.global.strMul = 2000;
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied, "ownership-loss setup apply must succeed");

    Put(spy, 0, kMaxHpOffset, static_cast<uint32_t>(7777));
    config.global.hpMul = 3000;
    config.global.strMul = 3000;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
           Get<uint32_t>(spy, 0, kMaxHpOffset) == 7777 &&
           Get<uint8_t>(spy, 0, kStatOffsets[0]) == 30,
           "edit must preserve a third-party value while recomputing still-owned fields from baseline");

    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::OwnershipLost &&
           Get<uint32_t>(spy, 0, kMaxHpOffset) == 7777 &&
           Get<uint8_t>(spy, 0, kStatOffsets[0]) == 10,
           "OFF must not overwrite ownership loss and must restore independent still-owned fields");
}

void TestMaximumWriteSuccessReadbackFailureFailsClosed() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 16, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(6);
    DifficultyConfig config = EnabledConfig(2000);
    spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;

    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000,
           "a successful maximum write with failed readback must relinquish the pair");

    spy.failReadAfterWriteAddress = 0;
    const int writesBeforeRestore = spy.writes;
    result = runtime.Restore(io);
    Expect(result.code == ResultCode::OwnershipLost &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500 &&
               spy.writes == writesBeforeRestore,
           "OFF must not guess whether an unreadable maximum write applied");
}

void TestDynamicWriteSuccessReadbackFailureFailsClosed() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 34, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(16);
    DifficultyConfig config = EnabledConfig(2000);
    spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000,
           "a current write with failed readback must relinquish its structural pair");
    spy.failReadAfterWriteAddress = 0;
    const int writesBeforeRestore = spy.writes;
    result = runtime.Restore(io);
    Expect(result.code == ResultCode::OwnershipLost &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000 &&
               Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000 &&
               spy.writes == writesBeforeRestore,
           "OFF must not use a later numeric match to authorize an ambiguous current restore");
}

void TestInterveningGameplayCurrentRejectsStaleRetry() {
    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 35, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(17);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.failWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u,
               "damage retry setup must leave a failed current write behind a changed maximum");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(425u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 425u &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   spy.writes == writesBeforeRetry,
               "retry must not overwrite damage that changed current HP after a failed write");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 36, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(18);
        DifficultyConfig config = EnabledConfig(1000);
        config.global.mpMul = 2000;
        spy.failWriteAddress = ActorAddress(spy, 0) + kCurrentMpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxMpOffset) == 200u &&
                   Get<uint32_t>(spy, 0, kCurrentMpOffset) == 50u,
               "MP-spend retry setup must leave a failed current write behind a changed maximum");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentMpOffset, static_cast<uint32_t>(20u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kCurrentMpOffset) == 20u &&
                   Get<uint32_t>(spy, 0, kMaxMpOffset) == 200u &&
                   spy.writes == writesBeforeRetry,
               "retry must not overwrite MP spend that followed a failed current write");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 37, 1000, 400, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(19);
        const DifficultyConfig config = EnabledConfig(2000);
        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 800u,
               "heal retry setup must first apply the evidenced HP ratio");
        spy.failWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 800u,
               "heal retry setup must fail only the paired OFF current write");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(900u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 900u &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   spy.writes == writesBeforeRetry,
               "OFF retry must not overwrite healing that followed a failed current write");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 38, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(20);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u,
               "readback retry setup must expose a possibly successful current write");
        spy.failReadAfterWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(875u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 875u &&
                   spy.writes == writesBeforeRetry,
               "retry must not overwrite gameplay after an ambiguous current write readback");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 48, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(30);
        DifficultyConfig config = EnabledConfig(2000);
        spy.failWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault,
               "edit-during-retry setup must retain a definitely failed current transaction");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(1000u));
        config.global.hpMul = 3000;
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u &&
                   spy.writes == writesBeforeRetry,
               "an old retry target reached by gameplay must block a later maximum edit too");
    }
}

void TestMaximumFailureDoesNotReuseStaleCurrentEvidence() {
    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 39, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(21);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.failWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u,
               "ON max-failure setup must prove that neither member of the HP pair moved");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(425u));
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 850u,
               "ON retry must freshen damage after a definitely unchanged maximum write");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 40, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(22);
        DifficultyConfig config = EnabledConfig(1000);
        config.global.mpMul = 2000;
        spy.failWriteAddress = ActorAddress(spy, 0) + kMaxMpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxMpOffset) == 100u &&
                   Get<uint32_t>(spy, 0, kCurrentMpOffset) == 50u,
               "ON max-failure setup must prove that neither member of the MP pair moved");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentMpOffset, static_cast<uint32_t>(20u));
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied &&
                   Get<uint32_t>(spy, 0, kMaxMpOffset) == 200u &&
                   Get<uint32_t>(spy, 0, kCurrentMpOffset) == 40u,
               "ON retry must freshen MP spend after a definitely unchanged maximum write");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 41, 1000, 400, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(23);
        const DifficultyConfig config = EnabledConfig(2000);
        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 800u,
               "OFF max-failure setup must first apply the doubled HP pair");
        spy.failWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 800u,
               "OFF max-failure setup must leave both HP values unchanged");
        spy.failWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(900u));
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::Restored &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 450u,
               "OFF retry must freshen healing after a definitely unchanged maximum restore");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 42, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(24);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u,
               "ambiguous ON maximum readback must stop before the current write");
        spy.failReadAfterWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(425u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 425u &&
                   spy.writes == writesBeforeRetry,
               "ambiguous ON maximum ownership must fail closed without a stale current retry");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 43, 1000, 400, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(25);
        const DifficultyConfig config = EnabledConfig(2000);
        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied,
               "ambiguous OFF maximum setup must first own the doubled HP pair");
        spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 800u,
               "ambiguous OFF maximum readback must stop before the current restore");
        spy.failReadAfterWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(900u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 900u &&
                   spy.writes == writesBeforeRetry,
               "ambiguous OFF maximum ownership must preserve healing without another write");
    }
}

void TestAmbiguousCurrentReadbackAbaFailsClosed() {
    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 44, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(26);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u,
               "ON ABA setup must apply current HP while hiding its readback");
        spy.failReadAfterWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(500u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u &&
                   spy.writes == writesBeforeRetry,
               "ON ABA to the pre-write value must not authorize a stale current retry");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 45, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(27);
        const DifficultyConfig config = EnabledConfig(2000);
        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied,
               "OFF ABA setup must first own the doubled HP pair");
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(800u));
        spy.failReadAfterWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::Fault &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 400u,
               "OFF ABA setup must apply current HP while hiding its readback");
        spy.failReadAfterWriteAddress = 0;
        Put(spy, 0, kCurrentHpOffset, static_cast<uint32_t>(800u));
        const int writesBeforeRetry = spy.writes;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 800u &&
                   spy.writes == writesBeforeRetry,
               "OFF ABA to the pre-write value must not authorize a stale current restore");
    }
}

void TestSuccessfulWriteReturningToExpectedFailsClosed() {
    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 46, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(28);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.successfulNoopWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u,
               "a successful maximum write returning to the prior value must be interference");
        spy.successfulNoopWriteAddress = 0;
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u &&
                   spy.writes == writesBeforeRetry,
               "maximum interference must not be retried from stale pair evidence");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 47, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(29);
        const DifficultyConfig config = EnabledConfig(2000);
        spy.successfulNoopWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u,
               "a successful current write returning to its prevalue must be interference");
        spy.successfulNoopWriteAddress = 0;
        const int writesBeforeRetry = spy.writes;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::OwnershipLost &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 500u &&
                   spy.writes == writesBeforeRetry,
               "current interference must not authorize a stale ratio retry");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 48, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(30);
        const DifficultyConfig config = EnabledConfig(2000);
        Expect(runtime.Update(io, config, true, -1, &actor, 1).code == ResultCode::Applied,
               "OFF maximum interference setup must first own doubled HP");
        spy.successfulNoopWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;

        RuntimeResult result = runtime.Restore(io);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u,
               "a successful maximum restore returning to the owned value must be interference");
        spy.successfulNoopWriteAddress = 0;
        const int writesBeforeRetry = spy.writes;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u &&
                   spy.writes == writesBeforeRetry,
               "maximum restore interference must not retry from stale pair evidence");
    }

    {
        MemorySpy spy{};
        InitializeActor(spy, 0, 49, 1000, 500, 100, 50, 200, 10);
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        Runtime runtime{};
        runtime.BeginGeneration(31);
        const DifficultyConfig config = EnabledConfig(2000);
        Expect(runtime.Update(io, config, true, -1, &actor, 1).code == ResultCode::Applied,
               "OFF current interference setup must first own doubled HP");
        spy.successfulNoopWriteAddress = ActorAddress(spy, 0) + kCurrentHpOffset;

        RuntimeResult result = runtime.Restore(io);
        Expect(result.code == ResultCode::Fault && result.ownershipLost > 0 &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u,
               "a successful current restore returning to its prevalue must be interference");
        spy.successfulNoopWriteAddress = 0;
        const int writesBeforeRetry = spy.writes;
        result = runtime.Restore(io);
        Expect(result.code == ResultCode::OwnershipLost &&
                   Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000u &&
                   Get<uint32_t>(spy, 0, kCurrentHpOffset) == 1000u &&
                   spy.writes == writesBeforeRetry,
               "current restore interference must not authorize a stale ratio retry");
    }
}

void TestRestoreFaultRetainsRetryableOwnership() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 17, 1000, 500, 100, 50, 200, 10);
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    Runtime runtime{};
    runtime.BeginGeneration(7);
    DifficultyConfig config = EnabledConfig(2000);
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied,
           "restore-fault setup must own an applied maximum HP value");

    spy.failWriteAddress = ActorAddress(spy, 0) + kMaxHpOffset;
    result = runtime.Restore(io);
    Expect(result.code == ResultCode::Fault &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 2000,
           "a restore write fault must retain the applied value and report Fault");
    spy.failWriteAddress = 0;
    result = runtime.Restore(io);
    Expect(result.code == ResultCode::Restored &&
               Get<uint32_t>(spy, 0, kMaxHpOffset) == 1000,
           "a second normal-context restore must retry retained fault ownership");
}

ExecutableIdentity SupportedIdentity() {
    ExecutableIdentity identity{};
    identity.machine = kSupportedMachine;
    identity.timestamp = kSupportedTimestamp;
    identity.sizeOfImage = kSupportedSizeOfImage;
    identity.imageBase = kSupportedImageBase;
    identity.sha256 = kSupportedSha256;
    return identity;
}

constexpr std::array<uint8_t, 21> kResolverIdaPrefix = {{
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x53,
    0x0F, 0xB7, 0xD8, 0x56, 0xC1, 0xF8, 0x10, 0x25,
    0xFF, 0xFF, 0x00, 0x00, 0x57,
}};

constexpr std::array<uint8_t, 24> kInitScenePreferredIdaPrefix = {{
    0x8B, 0x0D, 0xA8, 0xA9, 0x12, 0x01, 0x56, 0x8B,
    0x41, 0x04, 0x0F, 0xBE, 0x35, 0xD9, 0xC9, 0x12,
    0x01, 0x03, 0xC1, 0xA3, 0xAC, 0xA9, 0x12, 0x01,
}};

constexpr std::array<uint8_t, 24> kInitSceneRelocatedIdaPrefix = {{
    0x8B, 0x0D, 0xA8, 0xA9, 0x01, 0x01, 0x56, 0x8B,
    0x41, 0x04, 0x0F, 0xBE, 0x35, 0xD9, 0xC9, 0x01,
    0x01, 0x03, 0xC1, 0xA3, 0xAC, 0xA9, 0x01, 0x01,
}};

constexpr std::array<uint8_t, 15> kInitializerPreferredIdaPrefix = {{
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x6A,
    0x60, 0x68, 0x00, 0x60, 0x13, 0x01, 0xE8,
}};

constexpr std::array<uint8_t, 15> kInitializerRelocatedIdaPrefix = {{
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x6A,
    0x60, 0x68, 0x00, 0x60, 0x02, 0x01, 0xE8,
}};

constexpr std::array<uint8_t, 21> kAccessorPreferredIdaSignature = {{
    0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x45, 0x08,
    0x69, 0xC0, 0x90, 0x0F, 0x00, 0x00,
    0x03, 0x05, 0x60, 0x44, 0x13, 0x01, 0x5D, 0xC3,
}};

constexpr std::array<uint8_t, 21> kAccessorRelocatedIdaSignature = {{
    0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x45, 0x08,
    0x69, 0xC0, 0x90, 0x0F, 0x00, 0x00,
    0x03, 0x05, 0x60, 0x44, 0x02, 0x01, 0x5D, 0xC3,
}};

void TestSinCallerEvidenceConstants() {
    const std::array<uintptr_t, 4> expectedResolveCallsites{{
        0x00381D87u, 0x00382A0Cu, 0x00382C0Eu, 0x00382C6Eu,
    }};
    Expect(kResolveEncounterCallsiteRvas == expectedResolveCallsites,
           "the supported Resolver target must retain exactly its four IDA-evidenced direct callsites");
    Expect(kNaturalResolveReturnRva == 0x00381D8Cu,
           "the historical label resolver return retains its binary identity without natural authority");
    Expect(kActorInitializerCallsiteRva == 0x00383FB1u,
           "ActorInitializer must retain its sole IDA-evidenced caller inside InitScene");
}

void TestProfileAndSignatureGate() {
    const ExecutableIdentity supported = SupportedIdentity();
    const AdapterEvidence evidence{
        &supported,
        kSupportedImageBase,
        kResolverIdaPrefix.data(), kResolverIdaPrefix.size(),
        kInitScenePreferredIdaPrefix.data(), kInitScenePreferredIdaPrefix.size(),
        kInitializerPreferredIdaPrefix.data(), kInitializerPreferredIdaPrefix.size(),
        kAccessorPreferredIdaSignature.data(), kAccessorPreferredIdaSignature.size(),
    };
    Expect(ValidateAdapterEvidence(evidence) == AdapterGateCode::Supported,
           "the exact PE identity and all four loaded signatures must pass the Difficulty gate");

    ExecutableIdentity wrong = supported;
    wrong.machine = 0x8664u;
    AdapterEvidence changed = evidence;
    changed.identity = &wrong;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::WrongMachine,
           "a non-I386 PE must fail closed");
    wrong = supported;
    wrong.timestamp ^= 1u;
    changed.identity = &wrong;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::WrongTimestamp,
           "a timestamp mismatch must fail closed");
    wrong = supported;
    wrong.sizeOfImage -= 0x1000u;
    changed.identity = &wrong;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::WrongImageSize,
           "a SizeOfImage mismatch must fail closed");
    wrong = supported;
    wrong.imageBase += 0x10000u;
    changed.identity = &wrong;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::WrongImageBase,
           "a preferred image-base mismatch must fail closed");
    wrong = supported;
    wrong.sha256[0] ^= 1u;
    changed.identity = &wrong;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::WrongSha256,
           "a SHA-256 mismatch must fail closed");

    changed = evidence;
    changed.resolverLength = kResolverIdaPrefix.size() - 1;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::ResolverSignatureMismatch,
           "a short Resolver prologue must fail before any hook creation");
    for (size_t index = 0; index < kResolverIdaPrefix.size(); ++index) {
        std::array<uint8_t, kResolverIdaPrefix.size()> mutated = kResolverIdaPrefix;
        mutated[index] ^= 1u;
        changed = evidence;
        changed.resolverBytes = mutated.data();
        Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::ResolverSignatureMismatch,
               "every Resolver prefix byte must be part of the exact loaded-memory gate");
    }

    changed = evidence;
    changed.initSceneLength = kInitScenePreferredIdaPrefix.size() - 1;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitSceneSignatureMismatch,
           "a short InitScene prologue must fail before any hook creation");
    for (size_t index = 0; index < kInitScenePreferredIdaPrefix.size(); ++index) {
        std::array<uint8_t, kInitScenePreferredIdaPrefix.size()> mutated =
            kInitScenePreferredIdaPrefix;
        mutated[index] ^= 1u;
        changed = evidence;
        changed.initSceneBytes = mutated.data();
        Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitSceneSignatureMismatch,
               "every InitScene prefix byte, including relocated abs32 bytes, must be exact");
    }

    changed = evidence;
    changed.loadedImageBase = 0x002F0000u;
    changed.initSceneBytes = kInitSceneRelocatedIdaPrefix.data();
    changed.initializerBytes = kInitializerRelocatedIdaPrefix.data();
    changed.accessorBytes = kAccessorRelocatedIdaSignature.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::Supported,
           "all HIGHLOW operands must compare at the actual loaded module base");
    changed.initSceneBytes = kInitScenePreferredIdaPrefix.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitSceneSignatureMismatch,
           "disk-profile bytes must not masquerade as loaded-memory proof after relocation");

    changed.initSceneBytes = kInitSceneRelocatedIdaPrefix.data();
    changed.initializerBytes = kInitializerPreferredIdaPrefix.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitializerSignatureMismatch,
           "the initializer preferred operand must fail against a relocated loaded image");
    changed.initializerBytes = kInitializerRelocatedIdaPrefix.data();
    changed.accessorBytes = kAccessorPreferredIdaSignature.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::AccessorSignatureMismatch,
           "the accessor preferred operand must fail against a relocated loaded image");

    for (size_t index = 0; index < kInitializerRelocatedIdaPrefix.size(); ++index) {
        std::array<uint8_t, kInitializerRelocatedIdaPrefix.size()> mutated =
            kInitializerRelocatedIdaPrefix;
        mutated[index] ^= 1u;
        changed = evidence;
        changed.loadedImageBase = 0x002F0000u;
        changed.initSceneBytes = kInitSceneRelocatedIdaPrefix.data();
        changed.initializerBytes = mutated.data();
        changed.accessorBytes = kAccessorRelocatedIdaSignature.data();
        Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitializerSignatureMismatch,
               "every relocated initializer prefix byte must remain exact");
    }
    for (size_t index = 0; index < kAccessorRelocatedIdaSignature.size(); ++index) {
        std::array<uint8_t, kAccessorRelocatedIdaSignature.size()> mutated =
            kAccessorRelocatedIdaSignature;
        mutated[index] ^= 1u;
        changed = evidence;
        changed.loadedImageBase = 0x002F0000u;
        changed.initSceneBytes = kInitSceneRelocatedIdaPrefix.data();
        changed.initializerBytes = kInitializerRelocatedIdaPrefix.data();
        changed.accessorBytes = mutated.data();
        Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::AccessorSignatureMismatch,
               "every relocated accessor signature byte must remain exact");
    }

    constexpr std::array<uint8_t, 15> kInitializerRelocationAtOffsetMinusOne = {{
        0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x6A,
        0x60, 0x00, 0x60, 0x02, 0x01, 0x01, 0xE8,
    }};
    constexpr std::array<uint8_t, 15> kInitializerRelocationAtOffsetPlusOne = {{
        0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x6A,
        0x60, 0x68, 0x00, 0x00, 0x60, 0x02, 0x01,
    }};
    changed = evidence;
    changed.loadedImageBase = 0x002F0000u;
    changed.initSceneBytes = kInitSceneRelocatedIdaPrefix.data();
    changed.initializerBytes = kInitializerRelocationAtOffsetMinusOne.data();
    changed.accessorBytes = kAccessorRelocatedIdaSignature.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitializerSignatureMismatch,
           "initializer relocation at operand offset minus one must fail closed");
    changed.initializerBytes = kInitializerRelocationAtOffsetPlusOne.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitializerSignatureMismatch,
           "initializer relocation at operand offset plus one must fail closed");

    constexpr std::array<uint8_t, 21> kAccessorRelocationAtOffsetMinusOne = {{
        0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x45, 0x08,
        0x69, 0xC0, 0x90, 0x0F, 0x00, 0x00,
        0x03, 0x60, 0x44, 0x02, 0x01, 0x01, 0x5D, 0xC3,
    }};
    constexpr std::array<uint8_t, 21> kAccessorRelocationAtOffsetPlusOne = {{
        0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x45, 0x08,
        0x69, 0xC0, 0x90, 0x0F, 0x00, 0x00,
        0x03, 0x05, 0x60, 0x60, 0x44, 0x02, 0x01, 0xC3,
    }};
    changed.initializerBytes = kInitializerRelocatedIdaPrefix.data();
    changed.accessorBytes = kAccessorRelocationAtOffsetMinusOne.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::AccessorSignatureMismatch,
           "accessor relocation at operand offset minus one must fail closed");
    changed.accessorBytes = kAccessorRelocationAtOffsetPlusOne.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::AccessorSignatureMismatch,
           "accessor relocation at operand offset plus one must fail closed");

    changed = evidence;
    changed.loadedImageBase = 0xFF400000u;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InvalidArgument,
           "HIGHLOW reconstruction must reject a loaded address that exceeds abs32 width");

    changed = evidence;
    changed.initializerLength = kActorInitializerPrefix.size() - 1;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitializerSignatureMismatch,
           "a short initializer prefix must fail before hook creation");
    std::array<uint8_t, kInitializerPreferredIdaPrefix.size()> mutatedInitializer =
        kInitializerPreferredIdaPrefix;
    mutatedInitializer.back() ^= 1u;
    changed = evidence;
    changed.initializerBytes = mutatedInitializer.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::InitializerSignatureMismatch,
           "a mutated initializer prefix must fail before hook creation");
    changed = evidence;
    changed.accessorLength = kActorAccessorSignature.size() - 1;
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::AccessorSignatureMismatch,
           "a short accessor body must fail before hook creation");
    std::array<uint8_t, kAccessorPreferredIdaSignature.size()> mutatedAccessor =
        kAccessorPreferredIdaSignature;
    mutatedAccessor[8] ^= 1u;
    changed = evidence;
    changed.accessorBytes = mutatedAccessor.data();
    Expect(ValidateAdapterEvidence(changed) == AdapterGateCode::AccessorSignatureMismatch,
           "a mutated accessor body must fail before hook creation");
}

void TestValidationOnlyStartupPolicy() {
    Expect(ShouldInstallAtStartup(true, false),
           "normal startup may install Difficulty only when shared MinHook is ready");
    Expect(!ShouldInstallAtStartup(false, false),
           "normal startup must fail closed when shared MinHook is unavailable");
    Expect(!ShouldInstallAtStartup(true, true) &&
               !ShouldInstallAtStartup(false, true),
           "validation-only startup must never activate Difficulty regardless of MinHook state");
}

struct HookSpy {
    int creates = 0;
    int enables = 0;
    int disables = 0;
    int removes = 0;
    bool failCreate = false;
    bool failEnable = false;
    bool failDisable = false;
    bool failRemove = false;
};

bool HookCreate(void* context, uintptr_t, void*, void** originalOut) {
    HookSpy& spy = *static_cast<HookSpy*>(context);
    ++spy.creates;
    if (spy.failCreate) return false;
    if (originalOut) *originalOut = reinterpret_cast<void*>(0x12345678u);
    return true;
}

bool HookEnable(void* context, uintptr_t) {
    HookSpy& spy = *static_cast<HookSpy*>(context);
    ++spy.enables;
    return !spy.failEnable;
}

bool HookDisable(void* context, uintptr_t) {
    HookSpy& spy = *static_cast<HookSpy*>(context);
    ++spy.disables;
    return !spy.failDisable;
}

bool HookRemove(void* context, uintptr_t) {
    HookSpy& spy = *static_cast<HookSpy*>(context);
    ++spy.removes;
    return !spy.failRemove;
}

HookIo MakeHookIo(HookSpy& spy) {
    return {&spy, &HookCreate, &HookEnable, &HookDisable, &HookRemove};
}

void TestAllOrNothingHookTransaction() {
    const ExecutableIdentity supported = SupportedIdentity();
    const AdapterEvidence evidence{
        &supported,
        kSupportedImageBase,
        kResolverIdaPrefix.data(), kResolverIdaPrefix.size(),
        kInitScenePreferredIdaPrefix.data(), kInitScenePreferredIdaPrefix.size(),
        kInitializerPreferredIdaPrefix.data(), kInitializerPreferredIdaPrefix.size(),
        kAccessorPreferredIdaSignature.data(), kAccessorPreferredIdaSignature.size(),
    };
    void* original = nullptr;

    HookSpy profileFailureSpy{};
    HookTransaction profileFailure{};
    AdapterEvidence bad = evidence;
    bad.accessorLength = 1;
    InstallResult result = profileFailure.Install(
        MakeHookIo(profileFailureSpy), bad, 0x79C130u,
        reinterpret_cast<void*>(0x11111111u), &original);
    Expect(result.code == AdapterGateCode::AccessorSignatureMismatch &&
           !result.installed && profileFailureSpy.creates == 0,
           "profile/signature failure must create no hook");

    HookSpy missingDisableSpy{};
    HookTransaction missingDisable{};
    HookIo incompleteIo = MakeHookIo(missingDisableSpy);
    incompleteIo.disable = nullptr;
    result = missingDisable.Install(
        incompleteIo, evidence, 0x79C130u,
        reinterpret_cast<void*>(0x11111111u), &original);
    Expect(result.code == AdapterGateCode::InvalidArgument && !result.installed &&
               missingDisableSpy.creates == 0,
           "install must reject a backend that cannot retire a possibly reachable target");

    HookSpy partialSpy{};
    partialSpy.failEnable = true;
    HookTransaction partial{};
    result = partial.Install(
        MakeHookIo(partialSpy), evidence, 0x79C130u,
        reinterpret_cast<void*>(0x11111111u), &original);
    Expect(result.code == AdapterGateCode::HookRollbackFailed && !result.installed &&
            partialSpy.creates == 1 && partialSpy.enables == 1 &&
            partialSpy.removes == 0 && partial.HasHookState() && partial.EverReachable(),
            "an attempted activation is conservatively reachable and must retain its target and trampoline");
    Expect(partial.Remove(MakeHookIo(partialSpy)) && partial.HasHookState() &&
            partialSpy.disables == 1 && partialSpy.removes == 0,
            "normal-context retirement must disable but never remove a possibly reachable hook");

    HookSpy successSpy{};
    HookTransaction success{};
    result = success.Install(
        MakeHookIo(successSpy), evidence, 0x79C130u,
        reinterpret_cast<void*>(0x11111111u), &original);
    Expect(result.code == AdapterGateCode::Installed && result.installed &&
            success.Installed() && success.EverReachable() &&
            original == reinterpret_cast<void*>(0x12345678u),
            "an exact gate must install once and retain the original trampoline");
    Expect(success.Remove(MakeHookIo(successSpy)) &&
            successSpy.disables == 1 && successSpy.removes == 0 &&
            !success.Installed() && success.HasHookState(),
            "normal-context teardown must disable and retain a reachable hook for process lifetime");
    Expect(success.Remove(MakeHookIo(successSpy)) &&
            successSpy.disables == 1 && successSpy.removes == 0,
            "hook teardown must be idempotent");

    HookSpy disableRetrySpy{};
    HookTransaction disableRetry{};
    result = disableRetry.Install(
        MakeHookIo(disableRetrySpy), evidence, 0x79C130u,
        reinterpret_cast<void*>(0x11111111u), &original);
    disableRetrySpy.failDisable = true;
    Expect(!disableRetry.Remove(MakeHookIo(disableRetrySpy)) &&
            disableRetry.HasHookState() && disableRetrySpy.disables == 1 &&
            disableRetrySpy.removes == 0,
            "disable failure must retain hook ownership and skip unsafe removal");
    disableRetrySpy.failDisable = false;
    Expect(disableRetry.Remove(MakeHookIo(disableRetrySpy)) &&
            disableRetry.HasHookState() && !disableRetry.Installed() &&
            disableRetrySpy.disables == 2 && disableRetrySpy.removes == 0,
            "normal-context teardown must retry disable while retaining process-lifetime ownership");

    HookSpy removeRetrySpy{};
    removeRetrySpy.failRemove = true;
    HookTransaction removeRetry{};
    result = removeRetry.Install(
        MakeHookIo(removeRetrySpy), evidence, 0x79C130u,
        reinterpret_cast<void*>(0x11111111u), &original);
    Expect(removeRetry.Remove(MakeHookIo(removeRetrySpy)) &&
            removeRetry.HasHookState() && removeRetrySpy.disables == 1 &&
            removeRetrySpy.removes == 0,
            "a reachable hook must never call the remove backend, even when that backend would fail");
}

struct DifficultyBatchSpy {
    std::vector<uintptr_t> created;
    std::vector<uintptr_t> enabled;
    std::map<uintptr_t, bool> pending;
    int createCalls = 0;
    int removeCalls = 0;
    int queueEnableCalls = 0;
    int queueDisableCalls = 0;
    int applyCalls = 0;
    int disableCalls = 0;
    int closeAndDrainCalls = 0;
    int postDisableDrainCalls = 0;
    int failCreateAt = 0;
    int failRemoveAt = 0;
    int failQueueEnableAt = 0;
    int failQueueDisableAt = 0;
    int failApplyAt = 0;
    int failDisableAt = 0;
};

bool DifficultyBatchInitialize(void*) { return true; }

bool DifficultyBatchCreate(
    void* context, uintptr_t target, void* detour, void** originalOut) {
    DifficultyBatchSpy& spy = *static_cast<DifficultyBatchSpy*>(context);
    ++spy.createCalls;
    if (spy.failCreateAt == spy.createCalls) return false;
    if (!target || !detour || !originalOut ||
        std::find(spy.created.begin(), spy.created.end(), target) != spy.created.end()) {
        return false;
    }
    spy.created.push_back(target);
    *originalOut = reinterpret_cast<void*>(target + 0x1000u);
    return true;
}

bool DifficultyBatchRemove(void* context, uintptr_t target) {
    DifficultyBatchSpy& spy = *static_cast<DifficultyBatchSpy*>(context);
    ++spy.removeCalls;
    if (spy.failRemoveAt == spy.removeCalls) return false;
    const auto found = std::find(spy.created.begin(), spy.created.end(), target);
    if (found == spy.created.end()) return false;
    spy.created.erase(found);
    spy.enabled.erase(std::remove(spy.enabled.begin(), spy.enabled.end(), target), spy.enabled.end());
    spy.pending.erase(target);
    return true;
}

bool DifficultyBatchQueueEnable(void* context, uintptr_t target) {
    DifficultyBatchSpy& spy = *static_cast<DifficultyBatchSpy*>(context);
    ++spy.queueEnableCalls;
    if (spy.failQueueEnableAt == spy.queueEnableCalls) return false;
    if (std::find(spy.created.begin(), spy.created.end(), target) == spy.created.end()) return false;
    spy.pending[target] = true;
    return true;
}

bool DifficultyBatchQueueDisable(void* context, uintptr_t target) {
    DifficultyBatchSpy& spy = *static_cast<DifficultyBatchSpy*>(context);
    ++spy.queueDisableCalls;
    if (spy.failQueueDisableAt == spy.queueDisableCalls) return false;
    if (std::find(spy.created.begin(), spy.created.end(), target) == spy.created.end()) return false;
    spy.pending[target] = false;
    return true;
}

bool DifficultyBatchApply(void* context) {
    DifficultyBatchSpy& spy = *static_cast<DifficultyBatchSpy*>(context);
    ++spy.applyCalls;
    for (const auto& operation : spy.pending) {
        const auto found = std::find(spy.enabled.begin(), spy.enabled.end(), operation.first);
        if (operation.second && found == spy.enabled.end()) {
            spy.enabled.push_back(operation.first);
        } else if (!operation.second && found != spy.enabled.end()) {
            spy.enabled.erase(found);
        }
    }
    spy.pending.clear();
    return spy.failApplyAt != spy.applyCalls;
}

bool DifficultyBatchDisable(void* context, uintptr_t target) {
    DifficultyBatchSpy& spy = *static_cast<DifficultyBatchSpy*>(context);
    ++spy.disableCalls;
    if (spy.failDisableAt == spy.disableCalls) return false;
    spy.enabled.erase(std::remove(spy.enabled.begin(), spy.enabled.end(), target), spy.enabled.end());
    return true;
}

bool DifficultyBatchCloseAndDrain(void* context) {
    ++static_cast<DifficultyBatchSpy*>(context)->closeAndDrainCalls;
    return true;
}

bool DifficultyBatchPostDisableDrain(void* context) {
    ++static_cast<DifficultyBatchSpy*>(context)->postDisableDrainCalls;
    return true;
}

DifficultyDetourIo DifficultyCreateIo(DifficultyBatchSpy& spy) {
    return {&spy, &DifficultyBatchCreate, &DifficultyBatchRemove};
}

FfxHooks::MinHookBatch::BatchIo DifficultyCoordinatorIo(DifficultyBatchSpy& spy) {
    return {&spy, &DifficultyBatchQueueEnable, &DifficultyBatchQueueDisable,
            &DifficultyBatchApply, &DifficultyBatchDisable};
}

FfxHooks::MinHookBatch::NeutralizationFence DifficultyFence(DifficultyBatchSpy& spy) {
    return {&spy, &DifficultyBatchCloseAndDrain, &DifficultyBatchPostDisableDrain};
}

void PrimeDifficultyCoordinator(FfxHooks::MinHookBatch::Coordinator& coordinator) {
    const auto ready = FfxHooks::MinHookBatch::EnsureInitialized(
        &coordinator, {nullptr, &DifficultyBatchInitialize});
    Expect(ready == FfxHooks::MinHookBatch::InitializationResult::Ready,
           "the isolated Difficulty coordinator must initialize before hook creation");
}

std::array<DifficultyDetourSpec, kDifficultyDetourCount> DifficultySpecs(
    std::array<void*, kDifficultyDetourCount>& originals) {
    return {{
        {0x007828B0u, reinterpret_cast<void*>(0x1010u), &originals[0]},
        {0x00783ED0u, reinterpret_cast<void*>(0x2020u), &originals[1]},
        {0x0079C130u, reinterpret_cast<void*>(0x3030u), &originals[2]},
    }};
}

void TestDifficultyThreeTargetCoordinatorAdapter() {
    std::array<void*, kDifficultyDetourCount> originals{};
    const auto specs = DifficultySpecs(originals);

    FfxHooks::MinHookBatch::Coordinator uninitializedCoordinator;
    DifficultyBatchSpy uninitializedSpy{};
    DifficultyDetourOwner uninitializedOwner{};
    DifficultyDetourResult result = InstallDifficultyDetours(
        DifficultyCreateIo(uninitializedSpy), &uninitializedCoordinator,
        DifficultyCoordinatorIo(uninitializedSpy), DifficultyFence(uninitializedSpy),
        specs, &uninitializedOwner);
    Expect(result.code == DifficultyDetourCode::CoordinatorNotReady &&
               uninitializedSpy.createCalls == 0,
           "Difficulty must reject an uninitialized process coordinator before creating a hook");

    FfxHooks::MinHookBatch::Coordinator createFailureCoordinator;
    PrimeDifficultyCoordinator(createFailureCoordinator);
    DifficultyBatchSpy createFailureSpy{};
    createFailureSpy.failCreateAt = 2;
    DifficultyDetourOwner createFailureOwner{};
    result = InstallDifficultyDetours(
        DifficultyCreateIo(createFailureSpy), &createFailureCoordinator,
        DifficultyCoordinatorIo(createFailureSpy), DifficultyFence(createFailureSpy),
        specs, &createFailureOwner);
    Expect(result.code == DifficultyDetourCode::HookCreateFailed &&
               createFailureSpy.createCalls == 2 && createFailureSpy.removeCalls == 1 &&
               createFailureSpy.applyCalls == 0 && createFailureSpy.created.empty(),
           "a partial three-target create must roll back completely before the first Apply boundary");

    FfxHooks::MinHookBatch::Coordinator rollbackCoordinator;
    PrimeDifficultyCoordinator(rollbackCoordinator);
    DifficultyBatchSpy rollbackSpy{};
    rollbackSpy.failCreateAt = 2;
    rollbackSpy.failRemoveAt = 1;
    DifficultyDetourOwner rollbackOwner{};
    result = InstallDifficultyDetours(
        DifficultyCreateIo(rollbackSpy), &rollbackCoordinator,
        DifficultyCoordinatorIo(rollbackSpy), DifficultyFence(rollbackSpy),
        specs, &rollbackOwner);
    Expect(result.code == DifficultyDetourCode::HookRollbackFailed &&
               rollbackOwner.created[0] && !rollbackOwner.applyAttempted &&
               rollbackSpy.applyCalls == 0,
           "a failed pre-Apply rollback must retain exact create-only ownership for retry");
    rollbackSpy.failRemoveAt = 0;
    result = RetireDifficultyDetours(
        DifficultyCreateIo(rollbackSpy), &rollbackCoordinator,
        DifficultyCoordinatorIo(rollbackSpy), DifficultyFence(rollbackSpy),
        specs, &rollbackOwner);
    Expect(result.code == DifficultyDetourCode::Removed && rollbackSpy.created.empty() &&
               originals[0] == nullptr,
           "create-only ownership must be exactly removable on a later owner-thread retry");

    originals = {};
    FfxHooks::MinHookBatch::Coordinator queueFailureCoordinator;
    PrimeDifficultyCoordinator(queueFailureCoordinator);
    DifficultyBatchSpy queueFailureSpy{};
    queueFailureSpy.failQueueEnableAt = 2;
    DifficultyDetourOwner queueFailureOwner{};
    result = InstallDifficultyDetours(
        DifficultyCreateIo(queueFailureSpy), &queueFailureCoordinator,
        DifficultyCoordinatorIo(queueFailureSpy), DifficultyFence(queueFailureSpy),
        DifficultySpecs(originals), &queueFailureOwner);
    Expect(result.code == DifficultyDetourCode::EnableFailedRetained &&
               queueFailureOwner.applyAttempted && queueFailureOwner.mayHaveRun &&
               queueFailureOwner.retainedInert && queueFailureSpy.created.size() == 3 &&
               queueFailureSpy.removeCalls == 0 && queueFailureSpy.closeAndDrainCalls == 1 &&
               queueFailureSpy.postDisableDrainCalls == 1,
           "a partial queue failure must neutralize one exact Difficulty batch and retain all trampolines");
    queueFailureSpy.failQueueEnableAt = 0;
    result = InstallDifficultyDetours(
        DifficultyCreateIo(queueFailureSpy), &queueFailureCoordinator,
        DifficultyCoordinatorIo(queueFailureSpy), DifficultyFence(queueFailureSpy),
        DifficultySpecs(originals), &queueFailureOwner);
    Expect(result.code == DifficultyDetourCode::Installed && queueFailureOwner.active &&
               queueFailureSpy.createCalls == 3 && queueFailureSpy.enabled.size() == 3,
           "a neutralized retained Difficulty batch must re-enable without recreating targets");
    result = RetireDifficultyDetours(
        DifficultyCreateIo(queueFailureSpy), &queueFailureCoordinator,
        DifficultyCoordinatorIo(queueFailureSpy), DifficultyFence(queueFailureSpy),
        DifficultySpecs(originals), &queueFailureOwner);
    Expect(result.code == DifficultyDetourCode::RetainedInert &&
               !queueFailureOwner.active && queueFailureOwner.retainedInert &&
               queueFailureSpy.enabled.empty() && queueFailureSpy.removeCalls == 0,
           "normal retirement must disable and retain all three ever-reachable Difficulty targets");

    originals = {};
    FfxHooks::MinHookBatch::Coordinator disableFailureCoordinator;
    PrimeDifficultyCoordinator(disableFailureCoordinator);
    DifficultyBatchSpy disableFailureSpy{};
    DifficultyDetourOwner disableFailureOwner{};
    result = InstallDifficultyDetours(
        DifficultyCreateIo(disableFailureSpy), &disableFailureCoordinator,
        DifficultyCoordinatorIo(disableFailureSpy), DifficultyFence(disableFailureSpy),
        DifficultySpecs(originals), &disableFailureOwner);
    Expect(result.code == DifficultyDetourCode::Installed &&
               disableFailureOwner.active && disableFailureSpy.enabled.size() == 3,
           "partial-disable RT0 must begin from one applied exact Difficulty batch");
    disableFailureSpy.failQueueDisableAt = 2;
    result = RetireDifficultyDetours(
        DifficultyCreateIo(disableFailureSpy), &disableFailureCoordinator,
        DifficultyCoordinatorIo(disableFailureSpy), DifficultyFence(disableFailureSpy),
        DifficultySpecs(originals), &disableFailureOwner);
    Expect(result.code == DifficultyDetourCode::CoordinatorPoisoned &&
               disableFailureOwner.coordinatorPoisoned &&
               disableFailureOwner.active && !disableFailureOwner.retainedInert &&
               disableFailureSpy.created.size() == 3 &&
               disableFailureSpy.removeCalls == 0,
           "a partial disable transaction must poison and conservatively retain active ownership");
    const int poisonedDisableApplyCalls = disableFailureSpy.applyCalls;
    result = RetireDifficultyDetours(
        DifficultyCreateIo(disableFailureSpy), &disableFailureCoordinator,
        DifficultyCoordinatorIo(disableFailureSpy), DifficultyFence(disableFailureSpy),
        DifficultySpecs(originals), &disableFailureOwner);
    Expect(result.code == DifficultyDetourCode::CoordinatorPoisoned &&
               disableFailureSpy.applyCalls == poisonedDisableApplyCalls &&
               disableFailureSpy.removeCalls == 0,
           "a poisoned partial disable must reject retry without another Apply or target removal");

    originals = {};
    FfxHooks::MinHookBatch::Coordinator applyFailureCoordinator;
    PrimeDifficultyCoordinator(applyFailureCoordinator);
    DifficultyBatchSpy applyFailureSpy{};
    applyFailureSpy.failApplyAt = 1;
    DifficultyDetourOwner applyFailureOwner{};
    result = InstallDifficultyDetours(
        DifficultyCreateIo(applyFailureSpy), &applyFailureCoordinator,
        DifficultyCoordinatorIo(applyFailureSpy), DifficultyFence(applyFailureSpy),
        DifficultySpecs(originals), &applyFailureOwner);
    Expect(result.code == DifficultyDetourCode::CoordinatorPoisoned &&
               applyFailureOwner.coordinatorPoisoned && applyFailureOwner.mayHaveRun &&
               applyFailureOwner.retainedInert && applyFailureSpy.created.size() == 3 &&
               applyFailureSpy.removeCalls == 0,
           "a failed Apply must poison restart-wide ownership while retaining its exact neutralized batch");
    const int poisonedApplyCalls = applyFailureSpy.applyCalls;
    result = InstallDifficultyDetours(
        DifficultyCreateIo(applyFailureSpy), &applyFailureCoordinator,
        DifficultyCoordinatorIo(applyFailureSpy), DifficultyFence(applyFailureSpy),
        DifficultySpecs(originals), &applyFailureOwner);
    Expect(result.code == DifficultyDetourCode::CoordinatorPoisoned &&
               applyFailureSpy.applyCalls == poisonedApplyCalls &&
               applyFailureSpy.removeCalls == 0,
           "a poisoned Difficulty owner must fail closed without a second queue or removal attempt");
}

struct InitializerSpy {
    MemorySpy memory{};
    std::string events;
    bool originalCalled = false;
    bool menuOpen = false;
    uint16_t formation = 21u;
    uintptr_t invalidPointer = 0x00001000u;
};

int CallOriginal(void* context) {
    InitializerSpy& spy = *static_cast<InitializerSpy*>(context);
    spy.events.push_back('O');
    spy.originalCalled = true;
    InitializeActor(spy.memory, 0, spy.formation, 1000, 500, 100, 50, 200, 10);
    return 37;
}

uintptr_t AccessActor(void* context, uint8_t slot) {
    InitializerSpy& spy = *static_cast<InitializerSpy*>(context);
    spy.events.push_back('A');
    if (!spy.originalCalled) return spy.invalidPointer;
    if (slot == 0) return ActorAddress(spy.memory, 0);
    if (slot == 1) return spy.invalidPointer;
    return 0;
}

bool ValidateActor(void* context, uintptr_t address, size_t span) {
    InitializerSpy& spy = *static_cast<InitializerSpy*>(context);
    spy.events.push_back('V');
    return span == kActorSpan && address == ActorAddress(spy.memory, 0);
}

void TestPostOriginalInitializerSeam() {
    InitializerSpy spy{};
    Runtime runtime{};
    DifficultyConfig config = EnabledConfig(2000);
    const InitializerIo io{
        &spy, &CallOriginal, &AccessActor, &ValidateActor,
        {&spy.memory, &MemoryRead, &MemoryWrite},
    };

    const InitializerResult result = RunInitializerPostOriginal(
        runtime, 44, config, true, -1, io);
    Expect(result.originalCalled && result.originalReturn == 37 &&
           !spy.events.empty() && spy.events.front() == 'O',
           "the unique initializer seam must call and preserve the original result first");
    Expect(spy.events == "O" && spy.memory.reads == 0 && spy.memory.writes == 0 &&
               result.runtime.code == ResultCode::NoActors &&
               result.runtime.actorsSeen == 0 && result.pointersRejected == 0 &&
               Get<uint32_t>(spy.memory, 0, kMaxHpOffset) == 1000,
           "state-0x11 processing must begin the generation without enumerating, capturing, or writing actors");

    // The proved scene-init state machine populates actors later in state 0x12. Reusing
    // the same address and formation models the RT2 ownership-loss case: only a deferred
    // first capture may treat these post-populate bytes as this battle's baseline.
    InitializeActor(spy.memory, 0, spy.formation, 1500, 750, 120, 60, 300, 20);
    const ActorRef actor{ActorAddress(spy.memory, 0), 0};
    const RuntimeResult applied = runtime.Update(
        {&spy.memory, &MemoryRead, &MemoryWrite}, config, true, -1, &actor, 1);
    Expect(applied.code == ResultCode::Applied && applied.actorsSeen == 1 &&
               applied.ownershipLost == 0 &&
               Get<uint32_t>(spy.memory, 0, kMaxHpOffset) == 3000 &&
               Get<uint32_t>(spy.memory, 0, kCurrentHpOffset) == 1500,
           "the first post-populate update must capture the populated baseline and retain ownership");
    Expect(!spy.menuOpen,
           "Difficulty post-original application must not depend on native-menu open state");
}

void TestDifficultyFrameProducerWithoutMenuPump() {
    InitializerSpy spy{};
    Runtime runtime{};
    const DifficultyConfig config = EnabledConfig(7000);
    const InitializerIo init{&spy, &CallOriginal, &AccessActor, &ValidateActor,
                             {&spy.memory, &MemoryRead, &MemoryWrite}};
    RunInitializerPostOriginal(runtime, 73, config, true, -1, init);
    InitializeActor(spy.memory, 0, spy.formation, 1500, 750, 100, 50, 200, 10);
    const ActorRef actor{ActorAddress(spy.memory, 0), 0};
    int captures = 0;
    auto frame = [&](uint32_t owner, uint32_t current, bool inInitializer, uint8_t phase) {
        if (!CanServiceDifficultyRetry(owner, current, inInitializer) ||
            !IsPostPopulateBattlePhase(phase)) return;
        ++captures;
        const auto result = runtime.Update({&spy.memory, &MemoryRead, &MemoryWrite},
                                          config, true, -1, &actor, 1);
        Expect(result.code == ResultCode::Applied, "the admitted frame uses the real Difficulty writer");
    };
    frame(0, 9, false, 0x13);
    frame(7, 9, false, 0x13);
    frame(7, 7, true, 0x13);
    frame(7, 7, false, 0x11);
    frame(7, 7, false, 0x12);
    frame(7, 7, false, 0);
    Expect(captures == 0 && spy.memory.writes == 0,
           "unknown/foreign/reentrant threads and pre-populate/cleanup phases cannot touch actors");
    frame(7, 7, false, 1);
    Expect(captures == 1 && !spy.menuOpen &&
               Get<uint32_t>(spy.memory, 0, kMaxHpOffset) == 10500 &&
               Get<uint32_t>(spy.memory, 0, kCurrentHpOffset) == 5250,
           "a continuing owner-thread frame applies 7x HP without a single menu-pump callback");
    for (unsigned phase = 0; phase < 256; ++phase) {
        const bool expected = phase == 1 || (phase >= 0x13 && phase <= 0x16);
        Expect(IsPostPopulateBattlePhase(static_cast<uint8_t>(phase)) == expected,
               "only active battle and proven post-populate phases admit capture; zero is cleanup");
    }
}

void TestNativePopulationSignature() {
    const std::array<uint8_t,89> literal{{0x53,0x0F,0xBE,0x1D,0x29,0xA9,0x12,0x01,0xC6,0x05,0xE0,0xA8,0x12,0x01,0x12,0x83,0xFB,0x12,0x7D,0x2D,0x53,0xE8,0x06,0x00,0x01,0x00,0x83,0xC4,0x04,0x83,0xFB,0x12,0x7D,0x19,0x80,0xB8,0xC8,0x0D,0x00,0x00,0x00,0x74,0x10,0x6A,0x00,0x50,0x53,0xE8,0x8C,0x01,0x00,0x00,0x83,0xC4,0x0C,0x85,0xC0,0x74,0x11,0x43,0x83,0xFB,0x12,0x7C,0xD3,0xC6,0x05,0xE0,0xA8,0x12,0x01,0x13,0x33,0xC0,0x5B,0xC3,0xFE,0xC3,0x88,0x1D,0x29,0xA9,0x12,0x01,0x83,0xC8,0xFF,0x5B,0xC3}};
    const std::array<std::pair<size_t,uint32_t>,4> operands={{{4,0xD2A929},{10,0xD2A8E0},{67,0xD2A8E0},{80,0xD2A929}}};
    Expect(kActorPopulateRva==0x00384010,"population detour uses the exact supported native entry");
    for(uintptr_t base:{uintptr_t{0x00400000},uintptr_t{0x002F0000},uintptr_t{0x10000000}}) {
        auto bytes=literal;
        for(const auto& operand:operands)for(size_t i=0;i<4;++i)
            bytes[operand.first+i]=static_cast<uint8_t>((base+operand.second)>>(8*i));
        Expect(ValidatePopulateEvidence(bytes.data(),bytes.size(),base),"full relocated population body accepted");
        for(size_t i=0;i<bytes.size();++i)for(unsigned bit=0;bit<8;++bit) {
            bytes[i]^=static_cast<uint8_t>(1u<<bit);
            Expect(!ValidatePopulateEvidence(bytes.data(),bytes.size(),base),"every altered population instruction/operand bit rejected");
            bytes[i]^=static_cast<uint8_t>(1u<<bit);
        }
        Expect(!ValidatePopulateEvidence(nullptr,bytes.size(),base),"null population evidence rejected");
        Expect(!ValidatePopulateEvidence(bytes.data(),bytes.size()-1,base),"short population proof rejected");
        Expect(!ValidatePopulateEvidence(bytes.data(),bytes.size()+1,base),"overlong population proof rejected before reads");
    }
    Expect(!ValidatePopulateEvidence(literal.data(),literal.size(),UINT32_MAX),"relocated population operand overflow rejected");
}

void TestNativePopulationCompletionProducer() {
    struct Producer {
        InitializerSpy actors{};Runtime runtime{};DifficultyConfig config=EnabledConfig(7000);
        uint32_t owner=460,current=460;uint8_t phase=0x12;
        bool accepting=true,armed=true,reentrant=false,readable=true,stopOnRead=false,populated=false;
        int returned=-1,originals=0,phaseReads=0,retries=0;
    };
    auto run=[](Producer& p) {
        const PopulateIo io{&p,
            [](void* c) {auto& p=*static_cast<Producer*>(c);++p.originals;
                if(p.returned==0&&!p.populated) {
                    InitializeActor(p.actors.memory,0,21,1500,750,100,50,200,10);p.populated=true;
                }
                return p.returned;},
            [](void* c) {auto& p=*static_cast<Producer*>(c);
                return p.accepting&&p.armed&&CanServiceDifficultyRetry(p.owner,p.current,p.reentrant);},
            [](void* c,uint8_t* phase) {auto& p=*static_cast<Producer*>(c);++p.phaseReads;*phase=p.phase;
                if(p.stopOnRead)p.accepting=false;
                return p.readable;},
            [](void* c) {auto& p=*static_cast<Producer*>(c);++p.retries;
                const ActorRef actor{ActorAddress(p.actors.memory,0),0};
                const auto result=p.runtime.Update({&p.actors.memory,&MemoryRead,&MemoryWrite},p.config,true,-1,&actor,1);
                Expect(result.code==ResultCode::Applied,"native completion drives the real Difficulty writer");p.armed=false;}};
        const int previous=p.originals;const int result=RunPopulatePostOriginal(io);
        Expect(result==p.returned&&p.originals==previous+1,"every population callback preserves exactly one vanilla call and its int result");
    };
    Producer p;p.runtime.BeginGeneration(74);
    run(p);
    Expect(p.retries==0&&p.phaseReads==0&&p.actors.memory.writes==0,"incomplete population cannot read phase or actors");
    p.returned=0;p.phase=0x13;run(p);
    Expect(p.retries==1&&!p.armed&&Get<uint32_t>(p.actors.memory,0,kMaxHpOffset)==10500&&
               Get<uint32_t>(p.actors.memory,0,kCurrentHpOffset)==5250,
           "owner 460 applies 7x HP after actual native completion without Present 580 or menu callbacks");
    const auto writes=p.actors.memory.writes;const int phaseReads=p.phaseReads;
    run(p);
    Expect(p.retries==1&&p.actors.memory.writes==writes&&p.phaseReads==phaseReads&&
               Get<uint32_t>(p.actors.memory,0,kMaxHpOffset)==10500,
           "a completed retry calls vanilla but never repeats capture or compounds HP");
    for(int failure=0;failure<8;++failure) {
        Producer denied;denied.runtime.BeginGeneration(75);denied.returned=0;denied.phase=0x13;
        switch(failure){case 0:denied.current=580;break;case 1:denied.owner=0;break;
        case 2:denied.accepting=false;break;case 3:denied.reentrant=true;break;case 4:denied.armed=false;break;
        case 5:denied.readable=false;break;case 6:denied.phase=0x12;break;case 7:denied.stopOnRead=true;break;}
        run(denied);Expect(denied.retries==0&&denied.actors.memory.writes==0,
            "foreign, stopped, recursive, stale, faulted or incomplete completion cannot write actors");
    }
    for(int value:{-1,0,1,8,10,17,18,19})
        Expect(IsInitializedBattleSceneReturn(value)==(value==8||value==10),"only actor-initializing scene return arms a battle generation");
}

void TestPostOriginalInitializerComposesSinThroughDifficulty() {
    InitializerSpy spy{};
    spy.formation = 0x1003u;
    Runtime runtime{};
    const DifficultyConfig difficulty = MakeNeutralConfig();
    FfxHooks::SinRam::RuntimeRequest sin{};
    sin.config = {true, 1};
    sin.encounterToken = 0x01360000u;
    sin.origin = FfxHooks::SinRam::EncounterOrigin::Natural;
    sin.transitionCallerRva = FfxHooks::SinNatural::kCallerRva;
    sin.transitionRequestId = 77u;
    sin.actorRequestId = 77u;
    sin.transitionGeneration = 45u;
    const InitializerIo io{
        &spy, &CallOriginal, &AccessActor, &ValidateActor,
        {&spy.memory, &MemoryRead, &MemoryWrite},
    };

    const InitializerResult result = RunInitializerPostOriginal(
        runtime, 45u, difficulty, true, 310, sin, io);
    Expect(result.originalCalled && result.originalReturn == 37 &&
               std::count(spy.events.begin(), spy.events.end(), 'O') == 1,
           "the composed initializer seam must call vanilla exactly once before S.I.N. composition");
    Expect(spy.events == "O" && spy.memory.reads == 0 && spy.memory.writes == 0 &&
               result.runtime.code == ResultCode::NoActors &&
               Get<uint32_t>(spy.memory, 0, kMaxHpOffset) == 1000u,
           "the initializer must preserve S.I.N. evidence without applying it before populate");

    InitializeActor(spy.memory, 0, spy.formation, 2000, 1000, 100, 50, 400, 10);
    const ActorRef actor{ActorAddress(spy.memory, 0), 0};
    const RuntimeResult applied = runtime.UpdateComposed(
        {&spy.memory, &MemoryRead, &MemoryWrite}, difficulty, true, 310, sin,
        &actor, 1);
    Expect(applied.code == ResultCode::Applied && applied.actorsSeen == 1 &&
               applied.ownershipLost == 0 &&
               Get<uint32_t>(spy.memory, 0, kMaxHpOffset) == 2200u &&
               Get<uint32_t>(spy.memory, 0, kCurrentHpOffset) == 1100u,
           "S.I.N. must compose once from the populated baseline through Difficulty's sole HP writer");
    Expect(Get<uint32_t>(spy.memory, 0, kMaxMpOffset) == 100u &&
               Get<uint32_t>(spy.memory, 0, kCurrentMpOffset) == 50u,
           "deferred S.I.N. composition must leave the MP pair unchanged");
}

std::string RuntimeSourcePath(const char* relativePath) {
    if (!relativePath || !*relativePath) return {};
    std::string path = __FILE__;
    const size_t fileSlash = path.find_last_of("\\/");
    if (fileSlash == std::string::npos) return {};
    path.resize(fileSlash);
    const size_t testsSlash = path.find_last_of("\\/");
    if (testsSlash == std::string::npos) return {};
    path.resize(testsSlash);
    return path + "\\" + relativePath;
}

bool ReadWholeSource(const std::string& path, std::string* output) {
    if (!output) return false;
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return false;
    const bool seekEnd = std::fseek(file, 0, SEEK_END) == 0;
    const long length = seekEnd ? std::ftell(file) : -1;
    const bool seekStart = length >= 0 && length <= 1024 * 1024 &&
                           std::fseek(file, 0, SEEK_SET) == 0;
    std::string source;
    if (seekStart) {
        source.resize(static_cast<size_t>(length));
        if (!source.empty() &&
            std::fread(source.data(), 1, source.size(), file) != source.size()) {
            source.clear();
        }
    }
    const bool closed = std::fclose(file) == 0;
    if (!seekStart || !closed || (length != 0 && source.empty())) return false;
    *output = source;
    return true;
}

std::string SourceBetween(
    const std::string& source, const char* beginToken, const char* endToken) {
    const size_t begin = source.find(beginToken);
    if (begin == std::string::npos) return {};
    const size_t end = source.find(endToken, begin + std::strlen(beginToken));
    if (end == std::string::npos) return {};
    return source.substr(begin, end - begin);
}

size_t CountSourceToken(const std::string& source, const char* token) {
    if (!token || !*token) return 0u;
    size_t count = 0u;
    size_t offset = 0u;
    while ((offset = source.find(token, offset)) != std::string::npos) {
        ++count;
        offset += std::strlen(token);
    }
    return count;
}

void TestProductionAdapterSourceContracts() {
    std::string source;
    Expect(ReadWholeSource(RuntimeSourcePath("hooks\\F7InLive.cpp"), &source),
           "F7 adapter source must be readable for RT0 source contracts");
    Expect(source.find("#include \"F7DifficultyCore.h\"") != std::string::npos &&
           source.find("#include \"SinRamConfigCore.h\"") != std::string::npos &&
           source.find("#include \"SinTransitionPublication.h\"") != std::string::npos &&
           source.find("#include \"MinHookBatchCoordinator.h\"") != std::string::npos &&
           source.find("ValidateAdapterEvidence") != std::string::npos &&
           source.find("RunPopulatePostOriginal") != std::string::npos &&
           source.find("InstallDifficultyDetours") != std::string::npos &&
           source.find("RetireDifficultyDetours") != std::string::npos,
           "the Win32 adapter must consume the tested gate, post-original seam, and batch owner");
    Expect(source.find("ComputeExecutableSha256") != std::string::npos &&
           source.find("VirtualQuery") != std::string::npos &&
           source.find("MEM_COMMIT") != std::string::npos &&
           source.find("kActorSpan") != std::string::npos,
           "the adapter must hash the executable and validate committed writable actor spans");
    Expect(source.find("EnsureProcessInitialized()") != std::string::npos &&
               source.find("RuntimeBatchIo()") != std::string::npos &&
               source.find("MH_Initialize(") == std::string::npos &&
               source.find("MH_EnableHook(") == std::string::npos &&
               source.find("MH_DisableHook(") == std::string::npos &&
               source.find("MH_QueueEnableHook(") == std::string::npos &&
               source.find("MH_QueueDisableHook(") == std::string::npos &&
               source.find("MH_ApplyQueued(") == std::string::npos &&
               source.find("MH_Uninitialize(") == std::string::npos &&
               CountSourceToken(source, "MH_CreateHook(") == 1u &&
               CountSourceToken(source, "MH_RemoveHook(") == 1u,
           "F7 must use the sole shared batch coordinator and reserve direct MinHook calls for create-only ownership");
    Expect(source.find("(int*)p->statusResist") == std::string::npos &&
           source.find("F7_OFF_CURRENT_HP") == std::string::npos &&
           source.find("RVA_ENEMY_LIST_PTR") == std::string::npos &&
           source.find("ApplyPresetToEnemy") == std::string::npos,
           "the adapter must remove the aliasing parser, scratch HP offsets, guessed list, and raw writer");
    Expect(source.find("ParseConfig(json") != std::string::npos &&
               source.find("SinRamConfig::ParseDocument(json") != std::string::npos,
           "Difficulty and S.I.N. loading must route independently through their bounded parsers");
    const std::string updateCurrent = SourceBetween(
        source, "static RuntimeResult DifficultyUpdateCurrentActorsLocked",
        "static void DifficultyCallbackLeave()");
    const std::string loadConfig = SourceBetween(
        source, "bool F7_LoadConfig()", "// ── Config save");
    const std::string saveConfig = SourceBetween(
        source, "bool F7_SaveConfig()", "void F7_SetDifficultyGlobal");
    const std::string shim = SourceBetween(
        source, "static void DifficultyArmBattleGeneration()",
        "static bool DifficultyPopulateAdmitted(void*)");
    const std::string install = SourceBetween(
        source, "static bool InstallDifficultyHook()", "// ── Hooks");
    const std::string installOwner = SourceBetween(
        source, "bool F7_InstallHooks(", "void F7_RemoveHooks()");
    Expect(installOwner.find("F7_LoadConfig();") < installOwner.find("const bool flagGate"),
           "saved S.I.N. seeds and OFF preferences load before gameplay-hook admission");
    Expect(source.find("static F7Config g_cfg") == std::string::npos &&
               source.find("g_difficultyPublishedConfig") == std::string::npos &&
               updateCurrent.find("configSnapshot.difficulty") != std::string::npos &&
               updateCurrent.find("configSnapshot.difficultyValid") != std::string::npos &&
               shim.find("F7_GetConfigSnapshot()") != std::string::npos,
           "Apply Now and initializer callbacks must consume a locked immutable production config snapshot");
    const size_t saveSnapshot = saveConfig.find("F7_GetConfigSnapshot");
    const size_t saveWrite = saveConfig.find("WriteDocument");
    const size_t loadRead = loadConfig.find("ReadDocument(");
    const size_t loadFirstPublish = loadConfig.find("F7_ReplaceConfigSnapshot");
    const size_t loadParse = loadConfig.find("ParseConfig(json");
    const size_t loadSinParse = loadConfig.find("SinRamConfig::ParseDocument(json");
    const size_t loadPublish = loadParse == std::string::npos ? std::string::npos :
        loadConfig.find("F7_ReplaceConfigSnapshot", loadParse);
    Expect(saveSnapshot != std::string::npos && saveWrite != std::string::npos &&
               saveSnapshot < saveWrite && loadRead != std::string::npos &&
               loadFirstPublish != std::string::npos && loadRead < loadFirstPublish &&
               loadParse != std::string::npos && loadSinParse != std::string::npos &&
               loadPublish != std::string::npos && loadParse < loadPublish &&
               loadSinParse < loadPublish,
            "independent config parsing must finish before one validated production snapshot is published");
    Expect(saveConfig.find("SinRamConfig::SerializeValue") != std::string::npos &&
               saveConfig.find("\\\"sinRam\\\":%s") != std::string::npos,
           "the sole F7 saver must splice the canonical sinRam value before its existing atomic WriteDocument boundary");
    std::string configState;
    Expect(ReadWholeSource(RuntimeSourcePath("hooks\\F7ConfigState.cpp"), &configState) &&
                configState.find("std::lock_guard<std::mutex>") != std::string::npos &&
                configState.find("F7ConfigStateSnapshot F7_GetConfigSnapshot()") !=
                    std::string::npos &&
                configState.find("F7ConfigStateSnapshot F7_UpdateConfigSnapshot") !=
                    std::string::npos,
            "the production config APIs tested by RT1 must serialize mutation and return copies");
    std::string rt1Source;
    const bool readRt1 = ReadWholeSource(
        RuntimeSourcePath("tests\\F7RuntimeRt1.cpp"), &rt1Source);
    const std::string rt1ApplyNow = SourceBetween(
        rt1Source, "void ApplyNow(ConcurrentRuntime& state)",
        "void InitializeBattle(ConcurrentRuntime& state)");
    const std::string rt1Initializer = SourceBetween(
        rt1Source, "void InitializeBattle(ConcurrentRuntime& state)",
        "void PublishReload(");
    Expect(readRt1 &&
                rt1ApplyNow.find("F7_GetConfigSnapshot()") != std::string::npos &&
                rt1Initializer.find("F7_GetConfigSnapshot()") != std::string::npos &&
                rt1Source.find("state.published") == std::string::npos,
            "the concurrent lifecycle RT1 must consume the actual production snapshot API, not a replica");
    const std::string setGlobal = SourceBetween(
        source, "void F7_SetDifficultyGlobal", "// Difficulty actor access");
    Expect(setGlobal.find("ValidateDifficultySnapshot") != std::string::npos,
           "UI edits must validate a complete immutable value before publishing it under the runtime lock");
    Expect(source.find("g_difficultyPendingBattleField.Consume()") != std::string::npos &&
               source.find("g_cfg.force.lastField, io") == std::string::npos &&
               source.find("BattleFieldSource::Natural") != std::string::npos &&
               source.find("BattleFieldSource::Force") != std::string::npos,
           "Difficulty generations must consume a dedicated field published by natural and F7 Force launches");
    std::string dllmain;
    Expect(ReadWholeSource(RuntimeSourcePath("dllmain.cpp"), &dllmain) &&
               dllmain.find("BattleFieldSource::Arena") != std::string::npos &&
               dllmain.find("BattleFieldSource::CustomMix") != std::string::npos &&
               dllmain.find("BattleFieldSource::Ultra") != std::string::npos &&
               dllmain.find("CustomMixUltraExactCarrier") != std::string::npos,
           "Arena, standard Custom Mix, and exact-carrier Ultra routes must publish typed fields");
    const std::string resolveEncounter = SourceBetween(
        source, "static unsigned __int8* __cdecl ResolveEncounter_Shim(uint32_t a1, int* a2, int* a3, int* a4) {",
        "static int __cdecl InitScene_Shim() {");
    const std::string initScene = SourceBetween(
        source, "static int __cdecl InitScene_Shim() {",
        "void F7_TickMainThread()");
    Expect(initScene.find("ClassifyInitSceneCaller(returnRva)") != std::string::npos &&
               initScene.find("InitSceneCaller::BattleState") != std::string::npos &&
               initScene.find("RunProductionBattle") != std::string::npos &&
               initScene.find("RunSharedBattleInsideCustomMix") != std::string::npos &&
               initScene.find("ReservedSeamIo reserved{}") != std::string::npos &&
               initScene.find("beforeOriginal") == std::string::npos &&
               initScene.find("afterOriginal") == std::string::npos &&
               CountSourceToken(source, "g_initSceneComposer.Register(") == 1u &&
               CountSourceToken(initScene, "RunProductionBattle") == 1u,
           "only the battle caller may wrap Seymour's sole composer slot without reserved callbacks");
    Expect(source.find("using ResolveEncounterFn = unsigned __int8* (__cdecl*)(uint32_t, int*, int*, int*)") !=
               std::string::npos &&
               source.find("using ActorPopulateFn = int (__cdecl*)()") != std::string::npos,
           "the existing Resolver and ActorInitializer detours must retain their exact x86 cdecl ABI");
    Expect(source.find("g_skipForceCapture") == std::string::npos &&
               source.find("F7_SetSkipForceCapture") == std::string::npos &&
               source.find("static thread_local EncounterCaptureSuppression") !=
                   std::string::npos &&
               resolveEncounter.find("g_explicitCaptureSuppression.Active()") !=
                   std::string::npos,
           "Natural capture suppression must be scoped and must not use a sticky consumed flag");
    Expect(dllmain.find("F7_SetSkipForceCapture") == std::string::npos &&
               dllmain.find("F7_BeginExplicitLaunchCapture") != std::string::npos &&
               dllmain.find("F7_EndExplicitLaunchCapture") != std::string::npos,
           "direct explicit calls must bracket Natural-capture suppression without sticky state");
    const size_t callerCapture = resolveEncounter.find("_ReturnAddress()");
    const size_t vanillaCall = resolveEncounter.find("original ? original(a1, a2, a3, a4)");
    const std::string natural = SourceBetween(source,"bool F7_SinObserveNaturalEncounter(","static void DifficultyArmBattleGeneration()");
    const size_t beginNatural = natural.find("F7_BeginPendingBattleFieldRequest()");
    const size_t stageNatural = natural.find("g_sinTransitionPublication.Stage(");
    const size_t commitNatural = natural.find("F7_CommitPendingBattleFieldRequest(");
    const size_t cancelNatural = natural.find("g_sinTransitionPublication.Cancel(");
    Expect(callerCapture != std::string::npos && vanillaCall != std::string::npos &&
               CountSourceToken(resolveEncounter, "original ? original(a1, a2, a3, a4)") == 1u &&
               beginNatural != std::string::npos && stageNatural != std::string::npos &&
               commitNatural != std::string::npos && cancelNatural != std::string::npos &&
               callerCapture < vanillaCall &&
               beginNatural < stageNatural && stageNatural < commitNatural &&
               commitNatural < cancelNatural &&
                natural.find("SinNatural::Admitted(evidence)") != std::string::npos &&
                natural.find("g_explicitCaptureSuppression.Active()") != std::string::npos &&
                natural.find("evidence.nativeFieldRow") != std::string::npos &&
                natural.find("ticket.callerRva=SinNatural::kCallerRva") != std::string::npos &&
                resolveEncounter.find("g_sinTransitionPublication.Stage(") == std::string::npos &&
                CountSourceToken(source, "g_sinTransitionPublication.Stage(") == 1u,
            "only admitted walking evidence stages a curse before field commit; label resolution remains original-only for S.I.N.");
    std::string aiSource;Expect(ReadWholeSource(RuntimeSourcePath("hooks/SinAiHook.cpp"),&aiSource),"natural adapter source is available");
    const auto walking=SourceBetween(aiSource,"int __cdecl NaturalStepShim(","int __cdecl RegisterShim(");
    Expect(walking.find("_ReturnAddress()")<walking.find("original?original(field,group,distance):0") &&
        walking.find("original?original(field,group,distance):0")<walking.find("SinNatural::Admitted(evidence)") &&
        walking.find("g_naturalReporter(evidence)")!=std::string::npos,
        "the walking observer preserves original arguments/result and publishes only after verified native selection");
    const std::string forceDirect = SourceBetween(
        dllmain, "static bool ArenaPlus_ForceBattleDirect",
        "static bool ArenaPlus_Battle7002SwapArgs");
    const std::string directRequest = SourceBetween(
        dllmain, "static bool ArenaPlus_LaunchBattle781D60Request",
        "static bool ArenaPlus_LaunchBattle7002Template");
    Expect(forceDirect.find("F7_BeginPendingBattleFieldRequest") != std::string::npos &&
               forceDirect.find("F7_CommitPendingBattleFieldRequest") != std::string::npos &&
               forceDirect.find("F7_CancelPendingBattleFieldRequest") != std::string::npos &&
               directRequest.find("F7_BeginPendingBattleFieldRequest") != std::string::npos &&
               directRequest.find("F7_CommitPendingBattleFieldRequest") != std::string::npos &&
               directRequest.find("F7_CancelPendingBattleFieldRequest") != std::string::npos,
           "direct launch paths must correlate success/failure publication with one request token");
    const std::string ultraQueue = SourceBetween(
        dllmain, "ArenaPlus_UltraQueueCarrier(void* rawContext) noexcept",
        "static bool ArenaPlus_UltraArmRequest");
    const std::string ultraLaunch = SourceBetween(
        dllmain, "ArenaPlus_Ultra_LaunchFromPump() {",
        "static bool ArenaPlus_LaunchBattle7002Template");
    const std::string ultraUi = SourceBetween(
        dllmain, "static void ArenaPlus_Ultra_HandleConfirm(int row) {",
        "static void ArenaPlus_HandleMenuConfirm(int row) {");
    Expect(!ultraQueue.empty() &&
               ultraQueue.find("kArenaPlusUltraCarrierRoute") != std::string::npos &&
               ultraQueue.find("CustomMixUltraExactCarrier") != std::string::npos &&
               ultraQueue.find("BattleFieldSource::Ultra") != std::string::npos &&
               ultraLaunch.find("LaunchEditorSelection(") != std::string::npos,
           "Ultra must route its closed 781D60 carrier through the tested queue-before-arm editor adapter");
    Expect(dllmain.find("route.field == 517") != std::string::npos &&
               dllmain.find("route.group == 0") != std::string::npos &&
               dllmain.find("route.formation == 0") != std::string::npos &&
               dllmain.find("route.battleToken == 0x02050000u") != std::string::npos &&
               dllmain.find("route.transition == 2") != std::string::npos &&
               dllmain.find("strcmp(route.battleId, \"dome02_00\") == 0") !=
                   std::string::npos &&
               dllmain.find("ProductionOperational()") != std::string::npos,
           "the Ultra authority must admit only the operational dome02_00 carrier tuple");
    Expect(!ultraUi.empty() &&
               dllmain.find("ARENA_PLUS_ULTRA_ROW_COUNT = 19") != std::string::npos &&
               dllmain.find("ARENA_PLUS_ULTRA_ROW_SCENERY = 0") != std::string::npos &&
               dllmain.find("ARENA_PLUS_ULTRA_ROW_CAMERA = 1") != std::string::npos &&
               dllmain.find("ARENA_PLUS_ULTRA_CHOICE_COUNT = 8") != std::string::npos &&
               ultraUi.find("TryAddChoice(") != std::string::npos &&
               ultraUi.find("RemoveLastChoice(") != std::string::npos &&
               ultraUi.find("ClearSelection()") != std::string::npos &&
               ultraUi.find("FfxHooks::ArenaMix::CanLaunch(") != std::string::npos &&
               ultraUi.find("CancelReason::Back") != std::string::npos,
           "the native Ultra editor must keep arena selection, eight symbolic choices, position controls and launch actions");
    const std::string ultraClose = SourceBetween(
        dllmain, "static void ArenaPlus_UltraCancelForClose",
        "static void F7CloseTransition(");
    Expect(!ultraClose.empty() &&
               ultraClose.find("CancelReason::FocusLoss") != std::string::npos &&
               ultraClose.find("CancelReason::Stop") != std::string::npos &&
               ultraClose.find("CancelReason::Close") != std::string::npos &&
               dllmain.find("F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost)") !=
                   std::string::npos,
           "focus loss, F7 close, and stop must all cancel Ultra through shared input ownership");
    const std::string presentTick = SourceBetween(
        dllmain, "static void NativeMenu_PresentTick() {",
        "static void F7_LeverApply");
    Expect(!presentTick.empty() &&
               presentTick.find("ProductionTick(GetTickCount64())") != std::string::npos &&
               presentTick.find("StatusCode::Queued") != std::string::npos &&
               presentTick.find("CancelReason::FocusLoss") != std::string::npos,
           "the all-context Present producer must expire or focus-cancel Ultra after the menu pump stops");
    Expect(dllmain.find("\"Custom Mix x3\"") != std::string::npos &&
               dllmain.find("\"Custom Mix x4\"") != std::string::npos &&
               dllmain.find("\"Custom Mix x5\"") != std::string::npos &&
               dllmain.find("{ 340,  0, 22, 0x01540016u, 2, \"mcyt00_22\"") !=
                   std::string::npos &&
               dllmain.find("{ 430,  2, 23, 0x01AE0017u, 2, \"nagi05_23\"") !=
                   std::string::npos &&
               dllmain.find("{ 430,  2, 22, 0x01AE0016u, 2, \"nagi05_22\"") !=
                   std::string::npos &&
               dllmain.find("g_arenaPlusMixRequiredSlots = static_cast<uint8_t>(row + 3)") != std::string::npos &&
               dllmain.find("ArenaPlusComposePick_Open(combo)") == std::string::npos &&
               CountSourceToken(dllmain, "ArenaPlusDirectRequestAuthority::LegacyExperimental") ==
                   4u,
           "fixed Mix labels route through the bounded RAM editor while legacy experimental authority stays contained");
    std::string customRuntime;
    std::string customCore;
    std::string customAdapter;
    Expect(ReadWholeSource(RuntimeSourcePath("hooks\\CustomMixRuntime.cpp"), &customRuntime) &&
               ReadWholeSource(RuntimeSourcePath("hooks\\CustomMixUltraCore.cpp"), &customCore) &&
               ReadWholeSource(RuntimeSourcePath("hooks\\CustomMixWindowsAdapter.cpp"), &customAdapter) &&
               customRuntime.find("MH_CreateHook") == std::string::npos &&
               customRuntime.find("MH_EnableHook") == std::string::npos &&
               customCore.find("MH_CreateHook") == std::string::npos &&
               customAdapter.find("MH_CreateHook") == std::string::npos &&
               CountSourceToken(customCore, "std::memcpy(carrier.bytes + kFormationSlotOffset") ==
                   2u,
           "the reviewed CustomMix path must add no MinHook target and keep both exact writes in ExecuteTransaction");
    const std::string armOverride = SourceBetween(
        dllmain, "static bool ArenaPlus_ArmBattle7002Override",
        "static bool ArenaPlus_LaunchSafeBattleFromPump");
    const std::string applyOverride = SourceBetween(
        dllmain, "static bool ArenaPlus_TryOverrideBattle7002",
        "static int ArenaTrace_CallOriginalAtel");
    Expect(armOverride.find("g_arenaPlusPendingDifficultyField") != std::string::npos &&
               armOverride.find("g_arenaPlusPendingDifficultySource") != std::string::npos &&
               applyOverride.find("F7_PublishPendingBattleField") != std::string::npos,
           "deferred Arena and Custom Mix overrides must publish the target route only when the override applies");
    const std::string commitUi = SourceBetween(
        dllmain, "static void F7_CommitValsToConfig()", "static void F7Sub_HandleConfirm");
    Expect(commitUi.find("F7_SetDifficultyGlobal(p)") != std::string::npos &&
               commitUi.find("F7_GetConfig()") == std::string::npos &&
               dllmain.find("const_cast<FfxHooks::F7Config&>") == std::string::npos,
           "UI edits must publish through the synchronized Difficulty snapshot API");
    const std::string saveFeedback = SourceBetween(
        dllmain, "static bool F7_SaveConfigWithFeedback",
        "static void F7_CommitValsToConfig()");
    Expect(saveFeedback.find("FfxHooks::F7_SaveConfig()") != std::string::npos &&
               saveFeedback.find("Config save failed; changes remain in memory") !=
                   std::string::npos &&
               saveFeedback.find("PlaySfx(saved ? 4 : 3)") != std::string::npos &&
               dllmain.find("FfxHooks::F7_SaveConfig(); F7DiffSetStatus") ==
                   std::string::npos,
           "F7 UI save actions must surface persistence failure and must not emit success SFX");
    const std::string resetMusic = SourceBetween(
        source, "bool F7_ResetMusic()", "void F7_SetMusicLock");
    Expect(resetMusic.find("const bool saved = F7_SaveConfig()") != std::string::npos &&
               resetMusic.find("return saved") != std::string::npos &&
               dllmain.find("const bool resetSaved = FfxHooks::F7_ResetMusic()") !=
                   std::string::npos,
           "music reset must propagate its real persistence result to the same UI feedback path");
    Expect(source.find("&g_trampActorPopulate") != std::string::npos &&
           source.find("void* original = nullptr;") == std::string::npos,
           "the original initializer trampoline must be published before hook enablement");
    Expect(source.find("!status.configured && configSnapshot.difficulty.byArea") !=
               std::string::npos,
           "configured status must ignore dormant area rules when diffByArea is OFF");
    const auto sceneOriginal=SourceBetween(source,"static int DifficultyCallInitSceneOriginal(void* context)",
        "struct CustomMixSharedBattleContext");
    Expect(!sceneOriginal.empty() && sceneOriginal.find("scene->battleCaller")!=std::string::npos &&
        sceneOriginal.find("IsInitializedBattleSceneReturn(result)")!=std::string::npos &&
        sceneOriginal.find("const int result=SharedBattleCallInitSceneOriginal(nullptr)")<
            sceneOriginal.find("DifficultyArmBattleGeneration()") &&
        sceneOriginal.find("InterlockedCompareExchange(&g_difficultyInShim,1,0)")!=std::string::npos,
        "only the guarded native battle InitScene return can arm metadata, after one original, outside actor capture");
    const size_t clearSinCurrent = shim.find("g_sinCurrentRequest = {}");
    const size_t consumeBattle = shim.find("g_difficultyPendingBattleField.Consume()");
    const size_t consumeSin = shim.find("g_sinTransitionPublication.Consume(battleField.request)");
    const size_t composedInitializer = shim.find("g_difficultyRuntime.BeginGeneration(");
    Expect(clearSinCurrent != std::string::npos && consumeBattle != std::string::npos &&
               consumeSin != std::string::npos && composedInitializer != std::string::npos &&
               clearSinCurrent < consumeBattle && consumeBattle < consumeSin &&
               consumeSin < composedInitializer &&
               shim.find("ConsumeCode::Matched") != std::string::npos &&
               shim.find("BattleFieldSource::Natural") != std::string::npos &&
               shim.find("transitionGeneration == generation") != std::string::npos &&
               shim.find("transitionGeneration > g_sinLastAcceptedGeneration") != std::string::npos,
           "ActorInitializer must clear first, consume correlated publications, reject stale evidence, and use the composed Difficulty seam");
    const auto admittedBeforeTrampoline = [](const std::string& callback, const char* trampoline) {
        const size_t enter = callback.find("InterlockedIncrement(&g_difficultyCallbacks)");
        const size_t admission = callback.find(
            "InterlockedCompareExchange(&g_difficultyAccepting");
        const size_t original = callback.find(trampoline);
        const size_t leave = callback.rfind("DifficultyCallbackLeave()");
        return enter != std::string::npos && admission != std::string::npos &&
               original != std::string::npos && leave != std::string::npos &&
               enter < admission && admission < original && original < leave;
    };
    Expect(admittedBeforeTrampoline(resolveEncounter, "g_trampResolve") &&
               admittedBeforeTrampoline(initScene, "SharedBattleCallInitSceneOriginal"),
           "ResolveEncounter and InitScene must join shared callback admission before trampoline access");
    const auto population=SourceBetween(source,"static int __cdecl ActorPopulate_Shim() {",
        "static bool DifficultyPopulateSignatureMatches()");
    Expect(population.find("InterlockedIncrement(&g_difficultyCallbacks)")<population.find("g_difficultyAccepting") &&
        population.find("DifficultyCallbackLeave()")!=std::string::npos &&
        population.find("RunPopulatePostOriginal(io)")!=std::string::npos &&
        sceneOriginal.find("DifficultyArmBattleGeneration()")<sceneOriginal.rfind("InterlockedExchange(&g_difficultyInShim,0)"),
        "scene and population keep re-entry/callback accounting through their guarded completion");
    const std::string applyNow = SourceBetween(
        source, "void F7_DifficultyApplyNow()", "F7DifficultyRuntimeStatus F7_DifficultyStatus()");
    Expect(CountSourceToken(applyNow,"g_difficultyInShim")>=2 &&
        applyNow.rfind("F7_Log(")!=std::string::npos &&
        applyNow.rfind("DifficultyCallbackLeave()")!=std::string::npos &&
        applyNow.rfind("F7_Log(")<applyNow.rfind("DifficultyCallbackLeave()"),
        "Apply Now rejects in-flight initialization before/after its lock wait and retains callback accounting through final logging");
    const size_t applyRetryGate = applyNow.find("g_difficultyRetryArmed");
    const size_t applyRetryUpdate = applyNow.find("DifficultyUpdateCurrentActorsLocked");
    Expect(applyRetryGate != std::string::npos &&
               applyRetryUpdate != std::string::npos &&
               applyRetryGate < applyRetryUpdate &&
               applyNow.find("deferred until post-populate") != std::string::npos,
           "Apply Now must not capture actors while the generation's first post-populate apply is pending");
    const size_t applyAdmission = applyNow.find("g_difficultyAccepting");
    const size_t applyInfra = applyNow.find("g_difficultyDetours.active");
    Expect(applyNow.find("g_difficultyHook.Installed") == std::string::npos &&
               applyAdmission != std::string::npos && applyInfra != std::string::npos &&
               applyAdmission < applyInfra &&
               applyNow.find("F7_IsEnabled()") == std::string::npos,
           "Apply Now must close on shared admission plus the installed batch; the broad "
           "F7 master may not gate the dedicated Difficulty path");
    Expect(updateCurrent.find("g_sinCurrentRequest") != std::string::npos &&
               updateCurrent.find("configSnapshot.sinRam") != std::string::npos &&
               updateCurrent.find("UpdateComposed(") != std::string::npos &&
               updateCurrent.find("transitionGeneration == g_difficultyGeneration") !=
                   std::string::npos,
           "Apply Now must reuse only the current natural request/generation while replacing its config snapshot");
    const size_t validateEvidence = install.find("ValidateAdapterEvidence(evidence)");
    const size_t ensureCoordinator = install.find("EnsureProcessInitialized()");
    const size_t eventCreate = install.find("CreateEventA(");
    const size_t installBatch = install.find("InstallDifficultyDetours(");
    const size_t admissionOpen = install.find(
        "InterlockedExchange(&g_difficultyAccepting, 1)");
    const std::string detourSpecs = SourceBetween(
        source, "DifficultyDetourSpecs() {", "static bool DifficultyHasCreatedTargets()");
    Expect(validateEvidence != std::string::npos &&
               install.find("g_base + kResolveEncounterRva") != std::string::npos &&
               install.find("kResolveEncounterPrefix.size()") != std::string::npos &&
               install.find("g_base + kInitSystemSceneRva") != std::string::npos &&
               install.find("kInitSystemScenePreferredPrefix.size()") != std::string::npos &&
               ensureCoordinator != std::string::npos && eventCreate != std::string::npos &&
               installBatch != std::string::npos && admissionOpen != std::string::npos &&
               validateEvidence < ensureCoordinator && install.find("DifficultyPopulateSignatureMatches()")<ensureCoordinator && ensureCoordinator < eventCreate &&
               eventCreate < installBatch &&
               installBatch < admissionOpen &&
                detourSpecs.find("kResolveEncounterRva") != std::string::npos &&
                detourSpecs.find("kInitSystemSceneRva") != std::string::npos &&
                detourSpecs.find("kActorPopulateRva") != std::string::npos &&
                CountSourceToken(detourSpecs, "reinterpret_cast<void*>(&") == 3u &&
                source.find("RVA_RESOLVE_ENCOUNTER") == std::string::npos &&
               source.find("RVA_INIT_SYSTEM_SCENE") == std::string::npos,
           "all five loaded signatures must gate the exact three-target Difficulty batch before creation or admission");
    Expect(install.find("CloseHandle(g_difficultyDrainedEvent)") != std::string::npos &&
               install.find("!g_difficultyDetours.mayHaveRun") != std::string::npos,
           "only a fully rolled-back pre-Apply install may release its event and trampoline outputs");
    Expect(installOwner.find("if (g_difficultyDetours.active)") != std::string::npos &&
               installOwner.find("InterlockedExchange(&g_enabled, flagGate ? 1 : 0)") !=
                   std::string::npos &&
               installOwner.find("if (g_enabled) return") == std::string::npos,
           "shared infrastructure reuse must require an active full batch while keeping F7 behavior independent");
    const std::string tick = SourceBetween(
        source, "void F7_TickMainThread()", "// ── Install / Remove");
    Expect(!tick.empty() && tick.find("F7_ForceTick") != std::string::npos &&
           tick.find("DifficultyUpdateCurrentActorsLocked") == std::string::npos &&
           tick.find("UpdateComposed") == std::string::npos &&
           tick.find("g_needApply") == std::string::npos,
           "the menu pump tick may retain Force scheduling and the bounded retry drain, "
           "but must own no direct Difficulty application");
    const std::string remove = SourceBetween(
        source, "void F7_RemoveHooks()", "#endif // FFXHOOKS_HAVE_POLYHOOK");
    Expect(!remove.empty() && remove.find("F7_RequestStop()") != std::string::npos &&
            remove.find("RetireDifficultyDetours(") != std::string::npos &&
            remove.find("DifficultyDrainFence()") != std::string::npos &&
            remove.find("g_sinTransitionPublication.Reset()") != std::string::npos &&
            remove.find("g_difficultyRuntime.Restore") == std::string::npos &&
            remove.find("g_difficultyDetours.mayHaveRun") != std::string::npos &&
            remove.find("process-lifetime") != std::string::npos,
            "reachable teardown must neutralize the exact batch while retaining runtime storage and skipping Restore");
    Expect(remove.find("RetireDifficultyDetours(") <
               remove.find("g_sinTransitionPublication.Reset()"),
           "teardown may reset S.I.N. publication only after the callback-draining retirement boundary");
    const std::string drain = SourceBetween(
        source, "static bool DifficultyDrainCallbacks(DWORD timeoutMs)",
        "int F7_DifficultyAppliedCount()");
    const size_t resetEvent = drain.find("ResetEvent(g_difficultyDrainedEvent)");
    const size_t countRecheck = resetEvent == std::string::npos ? std::string::npos : drain.find(
        "InterlockedCompareExchange(&g_difficultyCallbacks", resetEvent);
    const size_t waitEvent = drain.find("WaitForSingleObject(g_difficultyDrainedEvent");
    Expect(remove.find("!g_difficultyDetours.mayHaveRun") != std::string::npos &&
               !drain.empty() && resetEvent != std::string::npos &&
               countRecheck != std::string::npos && waitEvent != std::string::npos &&
               resetEvent < countRecheck && countRecheck < waitEvent,
           "event release must stay behind the never-applied proof and drain must reset before recheck");
    const std::string detach = SourceBetween(
        dllmain, "case DLL_PROCESS_DETACH:", "break;");
    Expect(detach.find("F7_RequestStop()") != std::string::npos &&
               detach.find("F7_RemoveHooks()") == std::string::npos,
           "DllMain detach may only close Difficulty admission; retirement belongs to normal context");
    const std::string f7Startup = SourceBetween(
        dllmain, "if (FfxHooks::F7Difficulty::ShouldInstallAtStartup",
        "ArenaPlus_RestorePendingComposeOnBoot");
    Expect(!f7Startup.empty() &&
               f7Startup.find("minHookReady, validateOnly") != std::string::npos &&
               f7Startup.find("F7_InstallHooks") != std::string::npos &&
               f7Startup.find("F7AiSwap_Install") != std::string::npos &&
               f7Startup.find("validation-only: shared battle runtime and Seymour skipped") !=
                   std::string::npos,
           "FFXHOOKS_VALIDATE_ONLY must truthfully skip shared hook creation, admission, and RAM writers");
    const std::string sharedRequest = SourceBetween(
        dllmain, "const bool sharedBattleRuntimeRequested =",
        "const FfxHooks::ResolverOwner::SharedResolverStartupPlan");
    Expect(!sharedRequest.empty() &&
               CountSourceToken(sharedRequest, "f7DifficultyStartupRequested") == 2u &&
               sharedRequest.find("f7DifficultyConfigRequested") != std::string::npos &&
               sharedRequest.find("true,  // CustomMix") == std::string::npos &&
               installOwner.find("(flagGate || arenaMixRequested) && InstallPositionReadHook()") !=
                   std::string::npos,
           "CustomMix requires an explicit F7 or Arena+ request and "
           "exact profile are both ready; an on-disk Difficulty preset independently "
           "requests shared infrastructure");

    std::string project;
    std::string buildScript;
    Expect(ReadWholeSource(RuntimeSourcePath("FfxHooksDll.vcxproj"), &project) &&
               ReadWholeSource(RuntimeSourcePath("build_hooks.ps1"), &buildScript),
           "the production project and direct build manifests must be readable");
    const char* sinSources[] = {
        "SinRamScalingCore.cpp", "SinRamConfigCore.cpp", "SinTransitionPublication.cpp",
    };
    bool manifestsComplete = true;
    for (const char* sinSource : sinSources) {
        manifestsComplete = manifestsComplete &&
            project.find(sinSource) != std::string::npos &&
            buildScript.find(sinSource) != std::string::npos;
    }
    Expect(manifestsComplete,
           "every production build path must link Scaling, Config, and Transition S.I.N. cores");
    Expect(project.find("SinRamScalingCore.h") != std::string::npos &&
               project.find("SinRamConfigCore.h") != std::string::npos &&
               project.find("SinTransitionPublication.h") != std::string::npos,
           "the Visual Studio project must expose all three typed S.I.N. headers");
    const char* customMixSources[] = {
        "CustomMixUltraCore.cpp", "CustomMixWindowsAdapter.cpp", "CustomMixRuntime.cpp",
    };
    bool customMixManifestsComplete = true;
    for (const char* customMixSource : customMixSources) {
        customMixManifestsComplete = customMixManifestsComplete &&
            project.find(customMixSource) != std::string::npos &&
            buildScript.find(customMixSource) != std::string::npos;
    }
    Expect(customMixManifestsComplete &&
               project.find("CustomMixUltraCore.h") != std::string::npos &&
               project.find("CustomMixWindowsAdapter.h") != std::string::npos &&
               project.find("CustomMixRuntime.h") != std::string::npos,
           "both production build paths must link and expose the complete CustomMix RAM path");
}

void TestDifficultyIndependentAdmission() {
    std::string header;
    std::string source;
    std::string coreHeader;
    std::string ui;
    Expect(ReadWholeSource(RuntimeSourcePath("hooks\\F7InLive.h"), &header) &&
               ReadWholeSource(RuntimeSourcePath("hooks\\F7InLive.cpp"), &source) &&
               ReadWholeSource(RuntimeSourcePath("hooks\\F7DifficultyCore.h"), &coreHeader) &&
               ReadWholeSource(RuntimeSourcePath("dllmain.cpp"), &ui),
           "F7 Difficulty admission surfaces must be readable for RT0 contracts");

    Expect(header.find("bool configured") != std::string::npos &&
               header.find("bool difficultyValid") != std::string::npos &&
               header.find("bool difficultyBehaviorEnabled") != std::string::npos &&
               header.find("bool infrastructureInstalled") != std::string::npos &&
               header.find("bool callbackAdmissionOpen") != std::string::npos &&
               header.find("bool ownedFieldsPresent") != std::string::npos &&
               header.find("AdapterGateCode infrastructureGate") != std::string::npos &&
               header.find("bool installed") == std::string::npos &&
               header.find("AdapterGateCode gate") == std::string::npos,
           "F7DifficultyRuntimeStatus must split config, behavior, infrastructure, and "
           "admission; the conflated installed/gate pair is retired");

    const std::string status = SourceBetween(
        source, "F7DifficultyRuntimeStatus F7_DifficultyStatus()",
        "F7SinRamRuntimeStatus F7_SinRamStatus()");
    Expect(status.find("status.infrastructureInstalled = g_difficultyDetours.active") !=
                   std::string::npos &&
               status.find("status.callbackAdmissionOpen") != std::string::npos &&
               status.find("status.difficultyBehaviorEnabled") != std::string::npos &&
               status.find("status.infrastructureGate = g_difficultyGate") !=
                   std::string::npos &&
               status.find("status.ownedFieldsPresent") != std::string::npos &&
               status.find("F7_IsEnabled()") == std::string::npos,
           "Difficulty status must read infra, admission, and behavior without the "
           "broad F7 master");

    Expect(source.find("DifficultyConfigEnablesBehavior") != std::string::npos &&
               source.find("F7_DifficultyRequestedFromDisk()") != std::string::npos &&
               header.find("F7_DifficultyRequestedFromDisk") != std::string::npos,
           "a dedicated predicate must answer 'does any validated preset enable Difficulty'");

    const std::string shim = SourceBetween(
        source, "static void DifficultyArmBattleGeneration()",
        "static bool DifficultyPopulateAdmitted(void*)");
    Expect(shim.find("|| !F7_IsEnabled() ||") == std::string::npos &&
               shim.find("const bool f7Master") != std::string::npos &&
               shim.find("(f7Master || sinWanted) &&") != std::string::npos &&
               source.find("DifficultyUpdateCurrentActorsLocked(configSnapshot") != std::string::npos,
           "the initializer shim must drop the broad-master early-out while S.I.N. "
           "publication requires F7 or explicitly enabled valid S.I.N.");

    const std::string resolver = SourceBetween(
        source, "ResolveEncounter_Shim(uint32_t a1, int* a2, int* a3, int* a4) {",
        "static int SharedBattleCallInitSceneOriginal");
    Expect(resolver.find("f7Master ||") != std::string::npos &&
               resolver.find("difficultyActive") != std::string::npos &&
               resolver.find("if (f7Master) {") != std::string::npos &&
               resolver.find("g_sinTransitionPublication.Stage(") == std::string::npos,
           "encounter resolution must publish the Difficulty battle field whenever "
           "Difficulty is configured, not only under the broad master; label resolution "
           "cannot claim random-encounter S.I.N. authority");

    const std::string installOwner = SourceBetween(
        source, "bool F7_InstallHooks(", "void F7_RemoveHooks()");
    Expect(installOwner.find("difficultyRequested") != std::string::npos &&
               installOwner.find("F7_DifficultyRequestedFromDisk") != std::string::npos,
           "F7_InstallHooks must plan shared infrastructure from an on-disk Difficulty "
           "preset even when the broad F7 gate is OFF");
    const std::string sharedRequest = SourceBetween(
        ui, "const bool sharedBattleRuntimeRequested =",
        "const FfxHooks::ResolverOwner::SharedResolverStartupPlan");
    Expect(sharedRequest.find("f7DifficultyConfigRequested") != std::string::npos &&
               ui.find("f7DifficultyConfigRequested = FfxHooks::F7_DifficultyRequestedFromDisk") !=
                   std::string::npos,
           "the shared battle-runtime request must OR the on-disk Difficulty predicate");

    Expect(coreHeader.find("HasOwnedOrIndeterminateFields") != std::string::npos,
           "the core runtime must expose whether any writable field remains owned");

    const std::string sinStatus = SourceBetween(
        source, "F7SinRamRuntimeStatus F7_SinRamStatus()",
        "const char* F7_SinRamStateName");
    Expect(sinStatus.find("g_difficultyDetours.active") != std::string::npos &&
               sinStatus.find("g_difficultyAccepting") != std::string::npos &&
               source.find("configSnapshot.sinRamValid && configSnapshot.sinRam.enabled") != std::string::npos,
           "S.I.N. requires validated shared infrastructure and its own explicit setting");

    // R7 D-1 diagnostics now run at the post-populate attempt. Sampling or validating in
    // the state-0x11 shim would recreate the stale-baseline defect fixed below.
    Expect(source.find("DifficultyValidateActorDiag") != std::string::npos &&
               source.find("g_difficultyRejectSpan") != std::string::npos &&
               source.find("g_difficultyRejectFormation") != std::string::npos &&
               source.find("g_difficultyRejectRead") != std::string::npos &&
               source.find("g_lastActorTableBase") != std::string::npos,
           "actor validation must keep per-cause rejection tallies and the sampled table base");
    Expect(source.find("DifficultyResetRejectDiagnostics()") != std::string::npos &&
               source.find("0x00D34460u") != std::string::npos,
           "the post-populate attempt must reset tallies and sample the 0x01134460 actor-table base");
    Expect(source.find("spanRej=%ld formRej=%ld readRej=%ld table=0x%08X") != std::string::npos,
           "the per-generation Difficulty log must print the rejection tallies and table base");
    const std::string updateBody = SourceBetween(
        source, "static RuntimeResult DifficultyUpdateCurrentActorsLocked(",
        "static void DifficultyCallbackLeave()");
    Expect(updateBody.find("DifficultyValidateActorDiag") != std::string::npos,
           "the mid-battle refresh must attribute rejections through the same diagnostic path");

    // R8-D1: the shim runs before the scene state machine fills formation IDs
    // (RT2: formRej=4 with a live table). Without a retry the whole battle keeps
    // baseline stats. A bounded, generation-tagged retry on the main-thread pump
    // re-runs the same validated update path once slots populate.
    Expect(source.find("g_difficultyRetryArmed") != std::string::npos &&
               source.find("g_difficultyRetryDeadline") != std::string::npos &&
               source.find("g_difficultyRetryGeneration") != std::string::npos &&
               source.find("kDifficultyRetryWindowMs") != std::string::npos &&
               source.find("DifficultyArmRetry()") != std::string::npos &&
               source.find("DifficultyRetryTick()") != std::string::npos,
           "a bounded generation-tagged retry must exist for post-populate re-apply");
    const std::string shimRetry = SourceBetween(
        source, "static void DifficultyArmBattleGeneration()",
        "static bool DifficultyPopulateAdmitted(void*)");
    Expect(shimRetry.find("DifficultyArmRetry()") != std::string::npos,
           "the shim must arm the post-populate retry every battle generation — "
           "even zero-rejection runs can hold pre-populate baselines");
    const size_t shimRetryArm = shimRetry.find("DifficultyArmRetry()");
    const size_t shimRuntimeUnlock =
        shimRetry.rfind("ReleaseSRWLockExclusive(&g_difficultyRuntimeLock)");
    Expect(shimRetryArm != std::string::npos &&
               shimRuntimeUnlock != std::string::npos &&
               shimRetryArm < shimRuntimeUnlock,
           "the shim must arm the retry while the new generation is protected by the runtime lock");
    Expect(shimRetry.find("DifficultyGetActorBySlot") == std::string::npos &&
               shimRetry.find("DifficultyValidateActorDiag") == std::string::npos &&
               shimRetry.find("DifficultyUpdateCurrentActorsLocked") == std::string::npos &&
               shimRetry.find("DifficultyPublishResult(initialized.runtime") == std::string::npos,
           "the state-0x11 shim must not enumerate, validate, update, or publish actor data before populate");
    const std::string tickBody = SourceBetween(
        source, "void F7_TickMainThread() {", "bool F7_InstallHooks(");
    const std::size_t retryPos = tickBody.find("DifficultyRetryTick()");
    const std::size_t masterGate = tickBody.find("if (!F7_IsEnabled()) return;");
    Expect(retryPos != std::string::npos && masterGate != std::string::npos &&
               retryPos < masterGate,
           "the retry must drain before the broad F7 master gate — Difficulty runs without it");
    const std::string retryBody = SourceBetween(
        source, "static void DifficultyRetryTick() {",
        "void F7_DifficultyApplyNow()");
    const size_t retryCallbackEnter =
        retryBody.find("InterlockedIncrement(&g_difficultyCallbacks)");
    const size_t retryArmedRead = retryBody.find("g_difficultyRetryArmed");
    Expect(retryCallbackEnter != std::string::npos &&
               retryArmedRead != std::string::npos &&
               retryCallbackEnter < retryArmedRead &&
               retryBody.find("DifficultyCallbackLeave()") != std::string::npos,
           "the retry must enter callback accounting before reading armed state or shared runtime data");
    Expect(retryBody.find("g_difficultyAccepting") != std::string::npos &&
               retryBody.find("g_difficultyDetours.active") != std::string::npos &&
               retryBody.find("g_difficultyInShim") != std::string::npos &&
               retryBody.find("expired") != std::string::npos &&
               retryBody.find("AcquireSRWLockExclusive(&g_difficultyRuntimeLock)") !=
                   std::string::npos,
           "the retry must respect teardown, shim-in-flight, the runtime lock, and log an honest expiry");
    // R8-D1 v3 correctness: the retry must NOT re-apply per tick — it waits for the
    // scene-init phase byte (0xD2A8E0) to reach its terminal value: state==0 (init
    // complete) or state>=0x13 (populate handler 0x784010 finished its 18-combatant
    // loop). Armed inside the state-0x11 handler, the byte is guaranteed non-zero at
    // arm time, so the first observed 0 is always the terminal one. Applies exactly
    // once so UpdateComposed captures post-populate baselines, not stale shim data.
    Expect(retryBody.find("IsPostPopulateBattlePhase(sceneState)") != std::string::npos &&
               source.find("0x00D2A8E0u") != std::string::npos &&
               source.find("g_difficultyOwnerThread") != std::string::npos &&
               retryBody.find("DifficultyUpdateCurrentActorsLocked") != std::string::npos &&
               retryBody.find("DifficultyPublishResult") != std::string::npos &&
               retryBody.find("state=0x%02X") != std::string::npos,
           "the retry must gate its single apply on a proven post-populate battle phase and report the observed state on expiry");
    Expect(retryBody.find("result.actorsSeen == 0") != std::string::npos &&
               retryBody.find("waiting for valid actors") != std::string::npos &&
               retryBody.find("expired result=%s actors=%zu rejected=%zu") !=
                   std::string::npos,
           "a post-seam NoActors result must remain armed until deadline and expiry must report the final result and rejections");
    Expect(retryBody.find("__try") == std::string::npos &&
               source.find("DifficultyReadSceneInitState(") != std::string::npos &&
               source.find("DifficultyReadActorTableBase()") != std::string::npos,
           "SEH reads must live in trivial helpers so the callback-accounting guard remains MSVC-compatible");
    Expect(retryBody.find("const bool sceneStateReadable = DifficultyReadSceneInitState(&sceneState)") != std::string::npos &&
               retryBody.find("sceneStateReadable &&") != std::string::npos,
           "a faulted scene-state read must never satisfy the post-populate predicate");
    const size_t retryLock = retryBody.find("AcquireSRWLockExclusive(&g_difficultyRuntimeLock)");
    const size_t retryGeneration = retryBody.find("g_difficultyRetryGeneration");
    Expect(retryLock != std::string::npos && retryLock < retryGeneration &&
               retryBody.find("now >= g_difficultyRetryDeadline") <
                   retryBody.find("DifficultyUpdateCurrentActorsLocked"),
           "retry generation and deadline must be checked under its lock before any actor update");
    Expect(retryBody.find("g_difficultyRetryLast.result = result") != std::string::npos &&
               retryBody.find("DifficultyPublishResult(g_difficultyRetryLast.result") != std::string::npos &&
               retryBody.find("expired result=%s") < retryBody.find("DifficultyUpdateCurrentActorsLocked"),
           "deadline expiry must publish the last attempted result without a late actor update");
    Expect(retryBody.find("CanServiceDifficultyRetry") < retryBody.find("AcquireSRWLockExclusive") &&
               shimRetry.find("g_difficultyOwnerThread") != std::string::npos,
           "retry must reject a foreign or recursive frame before taking the actor ownership lock");
    std::string dllmain;
    Expect(ReadWholeSource(RuntimeSourcePath("dllmain.cpp"), &dllmain), "frame producer source readable");
    Expect(dllmain.find("FfxHooks::F7_DifficultyFrameTick()") == std::string::npos &&
        source.find("return RunPopulatePostOriginal(io)")!=std::string::npos &&
        source.find("static void DifficultyPopulateService(void*) {DifficultyRetryTick();}")!=std::string::npos,
        "native population completion services Difficulty directly; Present is not a battle producer");
    const size_t acquired = retryBody.find("} runtimeScope;");
    Expect(CountSourceToken(retryBody, "CanServiceDifficultyRetry(") == 2 &&
               retryBody.find("CanServiceDifficultyRetry(", acquired) < retryBody.find("DifficultyReadSceneInitState(&sceneState)"),
           "owner-thread admission must be rechecked after taking the generation lock before game reads");
    const std::string drainBody = SourceBetween(
        source, "static bool DifficultyCloseAdmissionAndDrain(void*) {",
        "static bool DifficultyDrainAfterDisable(void*) {");
    Expect(drainBody.find("g_difficultyRetryArmed, 0") != std::string::npos,
           "closing admission must disarm the retry so teardown never leaves a live writer");
    const std::string requestStop = SourceBetween(
        source, "void F7_RequestStop() {", "void F7_RemoveHooks() {");
    Expect(requestStop.find("g_difficultyAccepting, 0") != std::string::npos &&
               requestStop.find("g_difficultyRetryArmed, 0") != std::string::npos,
           "loader-lock stop must close retry admission before normal-context drain");
}

void TestDifficultyDocumentationContracts() {
    std::string guide;
    std::string bugs;
    std::string roadmap;
    std::string readme;
    Expect(ReadWholeSource(RuntimeSourcePath("..\\..\\..\\docs\\F7_INLIVE.md"), &guide) &&
           ReadWholeSource(RuntimeSourcePath("..\\..\\..\\docs\\KNOWN_BUGS.md"), &bugs) &&
           ReadWholeSource(RuntimeSourcePath("..\\..\\..\\docs\\ROADMAP.md"), &roadmap) &&
           ReadWholeSource(RuntimeSourcePath("..\\..\\..\\README.md"), &readme),
           "F7 guide, public README, known-bugs ledger, and roadmap must be readable from RT0");
    Expect(guide.find("0x0039C130") != std::string::npos &&
           guide.find("0x00395AB0") != std::string::npos &&
           guide.find("78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED") !=
               std::string::npos,
           "the F7 guide must record the exact supported profile and both evidenced seams");
    Expect(guide.find("+0x5D0") != std::string::npos &&
           guide.find("+0x5D4") != std::string::npos &&
           guide.find("+0x5A4") != std::string::npos,
           "the F7 guide must record corrected current HP/MP and overkill widths");
    Expect(guide.find("quarantined") != std::string::npos &&
           guide.find("+0x6E4/+0x6E8") != std::string::npos &&
           guide.find("RT1") != std::string::npos && guide.find("RT2") != std::string::npos,
           "the guide must disclose quarantined scratch fields and pending live evidence");
    Expect(bugs.find("Sleep(~120 ms)") == std::string::npos &&
           bugs.find("Current HP/MP not rescaled") == std::string::npos &&
           bugs.find("per-monster (N3)") == std::string::npos,
           "the known-bugs ledger must remove stale Sleep, HP/MP, and nonexistent N3 claims");
    Expect(guide.find("live gameplay ratio") != std::string::npos &&
               guide.find("process lifetime") != std::string::npos &&
               guide.find("one-shot current-battle field") != std::string::npos &&
               guide.find("immutable validated snapshot") != std::string::npos,
           "the guide must document dynamic HP/MP ownership, retained lifecycle, field publication, and config synchronization");
    Expect(roadmap.find("Elemental weak/resist/absorb | **Offline runtime candidate") != std::string::npos &&
           roadmap.find("Auto-statuses | **Offline runtime candidate") != std::string::npos &&
           roadmap.find("Status immunities | **Offline runtime candidate") != std::string::npos &&
           roadmap.find("Per-monster presets (N3)") == std::string::npos,
           "the roadmap must identify the undeployed status/element candidate without inventing an N3 editor");
    const std::size_t f7ReadmeRow = readme.find("| F7 In-Live menu |");
    const std::size_t f8ReadmeRow = readme.find("| F8 Dashboard |");
    const std::string f7ReadmeStatus =
        (f7ReadmeRow != std::string::npos && f8ReadmeRow != std::string::npos &&
         f8ReadmeRow > f7ReadmeRow)
            ? readme.substr(f7ReadmeRow, f8ReadmeRow - f7ReadmeRow)
            : std::string();
    Expect(f7ReadmeStatus.find("Offline candidate") != std::string::npos &&
            f7ReadmeStatus.find("source/RT0/build") != std::string::npos &&
            f7ReadmeStatus.find("isolated runtime/policy RT1") != std::string::npos &&
            f7ReadmeStatus.find("live machine-callback RT1") != std::string::npos &&
            f7ReadmeStatus.find("user-run RT2 pending") != std::string::npos &&
            readme.find("Element and status") != std::string::npos &&
            readme.find("live acceptance") != std::string::npos,
            "the public README must not promote the current Difficulty hook or undeployed additions");
}

// R4: focus loss closing every F-key menu is an intentional safety contract, not a
// bug. The WndProc publishes facts only (atomics + notifications); the menu pump
// thread owns all modal teardown so teardown stays serialized with ownership.
void TestFocusLossSafetyContract() {
    std::string dllmain;
    Expect(ReadWholeSource(RuntimeSourcePath("dllmain.cpp"), &dllmain),
           "dllmain source must be readable for the focus-loss contract");

    const std::string wndProc = SourceBetween(
        dllmain,
        "static LRESULT CALLBACK InGameMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)",
        "static void InGameMenuInstallWndProc");
    Expect(!wndProc.empty() &&
               wndProc.find("case WM_ACTIVATEAPP:") != std::string::npos &&
               wndProc.find("if (!wParam)") != std::string::npos &&
               wndProc.find("case WM_KILLFOCUS:") != std::string::npos &&
               CountSourceToken(wndProc, "FfxHooks::SpeedHackNotifyForegroundLost();") == 2u &&
               CountSourceToken(wndProc, "FfxHooks::Maechen_NotifyForegroundLost();") == 2u &&
               CountSourceToken(wndProc, "InterlockedExchange(&g_f7ForegroundLost, 1)") == 2u,
           "WM_ACTIVATEAPP(false) and WM_KILLFOCUS must publish foreground loss to "
           "Speed, Maechen, and the native menu as plain facts");
    Expect(wndProc.find("F7RequestClose") == std::string::npos &&
               wndProc.find("CloseMenu") == std::string::npos &&
               wndProc.find("ShowCursor") == std::string::npos &&
               wndProc.find("g_nativeWantSpawn") == std::string::npos &&
               wndProc.find("g_f7WantOpenKind") == std::string::npos,
           "the WndProc must publish facts only: modal teardown belongs to the pump");

    const std::string presentTick = SourceBetween(
        dllmain, "static void NativeMenu_PresentTick() {", "static void F7_LeverApply");
    Expect(presentTick.find("InterlockedExchange(&g_f7ForegroundLost, 0)") !=
               std::string::npos &&
               presentTick.find("StatusCode::Queued") != std::string::npos &&
               presentTick.find("CancelReason::FocusLoss") != std::string::npos &&
               presentTick.find("F7OwnsUiPublishedForPresent()") != std::string::npos &&
               presentTick.find(
                   "F7RequestClose(FfxHooks::F7Ui::CloseSource::FocusLost)") !=
                   std::string::npos,
           "the pump must cancel queued CustomMix work and close owned UI on the "
           "consumed focus-loss fact");
    Expect(presentTick.find(
               "f7Foreground && down && !s_hk && !s_hkChordSuppressed") !=
               std::string::npos &&
               presentTick.find("s_hk = down;") != std::string::npos,
           "an F-key held through background must update the latch without producing "
           "a rising edge; only a fresh release and press can reopen");

    const struct { const char* begin; const char* end; const char* name; } closers[] = {
        {"static int __cdecl SinCurse_InputCb(int obj)",
         "static NativeMenu::Poll SinCurse_PollMenu", "S.I.N. input"},
        {"static int __cdecl ArenaPlus_InputCb(int obj)",
         "static NativeMenu::Poll ArenaPlus_PollMenu", "Arena+ input"},
        {"static void NativeMenu_TickHeld()",
         "static bool ArenaPlusNpcDelayedOpenPending", "HELD tick"},
        {"static int __cdecl F7Sub_InputCb(int obj)",
         "static void F7Diff_Draw", "F7/F8 submenu input"},
    };
    for (const auto& closer : closers) {
        const std::string body = SourceBetween(dllmain, closer.begin, closer.end);
        char message[128] = {};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "%s must request a focus-lost close when the window is not foreground",
                    closer.name);
        Expect(body.find("F7IsForegroundWindow()") != std::string::npos &&
                   body.find("FfxHooks::F7Ui::CloseSource::FocusLost") !=
                       std::string::npos,
               message);
    }

    const std::string closeTransition = SourceBetween(
        dllmain,
        "FfxHooks::F7Ui::CloseDestination destination) {",
        "static void F7_CommitValsToConfig()");
    Expect(closeTransition.find("ArenaPlus_UltraCancelForClose(source)") !=
               std::string::npos &&
               closeTransition.find("ArenaPlus_CloseMenu(g_arenaPlusMenu)") !=
                   std::string::npos &&
               closeTransition.find("SinCurse_CloseMenu()") != std::string::npos &&
               closeTransition.find("F7Sub_CloseMenu()") != std::string::npos &&
               closeTransition.find("ArenaPlusComposePick_Close()") !=
                   std::string::npos,
           "the shared close transition must drain hub, Arena+, S.I.N., F7/F8 "
           "submenu, and Compose ownership for every close source");
    Expect(closeTransition.find("effects.releaseCursor") != std::string::npos &&
               closeTransition.find("F7ReleaseCursorOwnership()") != std::string::npos &&
               closeTransition.find("effects.cancelDraft") != std::string::npos &&
               closeTransition.find("g_f8ScalarEditor.Cancel()") != std::string::npos &&
               closeTransition.find("g_arenaPlusWantOpen, 0") != std::string::npos &&
               closeTransition.find("g_sinWantOpen, 0") != std::string::npos &&
               closeTransition.find("g_f7WantOpenKind, -1") != std::string::npos &&
               closeTransition.find("NativeMenuForceGateClear()") != std::string::npos,
           "focus-close effects must release the cursor, cancel drafts, drop pending "
           "opens, and release the force gate idempotently");

    // No sticky background menus: the pump must never queue an open purely because
    // focus returned. The only spawn request comes from a fresh admitted hotkey edge.
    Expect(wndProc.find("g_nativeWantSpawn, 1") == std::string::npos &&
               presentTick.find("g_f7ForegroundLost, 0") != std::string::npos,
           "focus regain must never reopen a menu; reopen requires a fresh key edge");
}

} // namespace

void TestStatusAndElementApplyEditRestore() {
    MemorySpy spy{};
    InitializeActor(spy, 0, 7, 1000, 250, 100, 25, 200, 10);
    Put(spy, 0, 0x5DA, uint8_t{0x81});
    Put(spy, 0, 0x5DB, uint8_t{0x07});
    Put(spy, 0, 0x5DC, uint8_t{0x02});
    Put(spy, 0, 0x5DD, uint8_t{0x14});
    Put(spy, 0, 0x630, uint16_t{0x2000});
    Put(spy, 0, 0x632, uint16_t{0x8000});
    Put(spy, 0, 0x634, uint16_t{0x45FF});
    Put(spy, 0, 0x644, uint8_t{100});
    Put(spy, 0, 0x650, uint8_t{0});
    const MemoryIo io{&spy, &MemoryRead, &MemoryWrite, &CompleteAutoRefresh};
    const ActorRef actor{ActorAddress(spy, 0), 0};
    DifficultyConfig config = EnabledConfig(1000);
    config.global.elemWeak = 1;
    config.global.elemResist = 2;
    config.global.elemAbsorb = 4;
    config.global.autoStatusMask = (1u << 3) | (1u << 15) | (1u << 16) | (1u << 23);
    config.global.statusResist[3] = 80;
    config.global.statusResist[15] = 255;
    Runtime runtime{};
    runtime.BeginGeneration(1);
    RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
           Get<uint8_t>(spy, 0, 0x5DA) == 0x84 &&
           Get<uint8_t>(spy, 0, 0x5DB) == 0 &&
           Get<uint8_t>(spy, 0, 0x5DC) == 2 &&
           Get<uint8_t>(spy, 0, 0x5DD) == 0x11,
           "selected affinities replace competing native affinities only for selected elements");
    Expect(Get<uint16_t>(spy, 0, 0x630) == 0x2008 &&
           Get<uint16_t>(spy, 0, 0x632) == 0x8818 &&
           Get<uint16_t>(spy, 0, 0x634) == 0x45FF,
           "AUTO must map Poison/Shell/Protect/Haste to the two native innate words without touching extra statuses");
    Expect(Get<uint8_t>(spy, 0, 0x644) == 100 && Get<uint8_t>(spy, 0, 0x650) == 255,
           "status resistance must raise the selected byte without weakening a native resistance");
    const int writes = spy.writes;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied && spy.writes == writes,
           "unchanged status/element settings must not compound or rewrite actor fields");
    config.global.autoStatusMask = 0;
    config.global.elemAbsorb = 2;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Applied &&
           Get<uint16_t>(spy, 0, 0x630) == 0x2000 && Get<uint16_t>(spy, 0, 0x632) == 0x8000 &&
           Get<uint8_t>(spy, 0, 0x5DA) == 0x82 && Get<uint8_t>(spy, 0, 0x5DC) == 0,
           "editing AUTO removes only the selected additions; conflicting legacy element masks prefer absorb");
    config.global.enabled = false;
    result = runtime.Update(io, config, true, -1, &actor, 1);
    Expect(result.code == ResultCode::Restored &&
           Get<uint8_t>(spy, 0, 0x5DA) == 0x81 && Get<uint8_t>(spy, 0, 0x5DB) == 7 &&
           Get<uint8_t>(spy, 0, 0x5DC) == 2 && Get<uint8_t>(spy, 0, 0x5DD) == 0x14 &&
           Get<uint8_t>(spy, 0, 0x650) == 0 && ActorCanariesIntact(spy, 0),
           "OFF restores owned affinity/resistance fields and preserves actor canaries");
}

void TestEveryAutoStatusMapsToItsNativeWord() {
    for (unsigned bit = 0; bit < 25; ++bit) {
        MemorySpy spy{};
        InitializeActor(spy, 0, 7, 1000, 1000, 100, 100, 200, 10);
        Put(spy, 0, 0x630, uint16_t{0});
        Put(spy, 0, 0x632, uint16_t{0});
        Put(spy, 0, 0x634, uint16_t{0xA55A});
        const MemoryIo io{&spy, &MemoryRead, &MemoryWrite, &CompleteAutoRefresh};
        const ActorRef actor{ActorAddress(spy, 0), 0};
        DifficultyConfig config = EnabledConfig(1000);
        config.global.autoStatusMask = 1u << bit;
        Runtime runtime{};
        runtime.BeginGeneration(1);
        const RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        const uint16_t first = bit < 12 ? static_cast<uint16_t>(1u << bit) : 0;
        const uint16_t second = bit >= 12 ? static_cast<uint16_t>(1u << (bit - 12)) : 0;
        Expect(result.code == ResultCode::Applied && Get<uint16_t>(spy, 0, 0x630) == first &&
               Get<uint16_t>(spy, 0, 0x632) == second &&
               Get<uint16_t>(spy, 0, 0x634) == 0xA55A && ActorCanariesIntact(spy, 0),
               "each AUTO checkbox must reach exactly its native innate status bit");
    }
}

void TestAutoStatusAdmissionAndFaults() {
    for (int failure = 0; failure != 5; ++failure) {
        MemorySpy spy{};
        InitializeActor(spy, 0, 7, 1000, 1000, 100, 100, 200, 10);
        Put(spy, 0, 0x630, uint16_t{0});
        Put(spy, 0, 0x632, uint16_t{0});
        const ActorRef actor{ActorAddress(spy, 0), 0};
        DifficultyConfig config = EnabledConfig(1000);
        config.global.autoStatusMask = 8u | (1u << 15);
        Runtime runtime{};
        runtime.BeginGeneration(1);
        MemoryIo io{&spy, &MemoryRead, &MemoryWrite, &CompleteAutoRefresh};
        if (failure == 0) io.refreshAutoStatus = nullptr;
        if (failure == 1) spy.failAutoRefresh = true;
        if (failure == 2) spy.failReadAfterWriteAddress = actor.address + 0x630;
        if (failure == 4) spy.successfulNoopWriteAddress = actor.address + 0x630;
        RuntimeResult result = runtime.Update(io, config, true, -1, &actor, 1);
        if (failure == 0) {
            Expect(result.code == ResultCode::Unavailable && spy.writes == 0 &&
                   spy.autoRefreshCalls == 0, "missing native admission must reject AUTO before any actor writes");
            continue;
        }
        if (failure == 3) {
            Put(spy, 0, 0x632, uint16_t{0x40});
            config.global.autoStatusMask |= 0x10;
        }
        const int writes = spy.writes;
        const int refreshes = spy.autoRefreshCalls;
        spy.failReadAfterWriteAddress = 0;
        spy.failAutoRefresh = false;
        result = runtime.Update(io, config, true, -1, &actor, 1);
        Expect(result.code == (failure == 3 ? ResultCode::OwnershipLost : ResultCode::Fault) &&
               spy.writes == writes && spy.autoRefreshCalls == refreshes,
               "native failure, ambiguous innate write or foreign word must block later AUTO writes/refresh");
        result = runtime.Restore(io);
        Expect(result.code == (failure == 3 ? ResultCode::OwnershipLost : ResultCode::Fault) &&
               spy.writes == writes && spy.autoRefreshCalls == refreshes,
               "OFF must not overwrite uncertain or foreign AUTO state");
    }
}

void TestAutoStatusBodyAdmission() {
    auto remove = kAutoStatusRemoveBody;
    auto apply = kAutoStatusApplyBody;
    Expect(ValidateAutoStatusEvidence(remove.data(), remove.size(), apply.data(), apply.size()),
           "exact native status bodies must be admitted");
    Expect(!ValidateAutoStatusEvidence(nullptr, remove.size(), apply.data(), apply.size()) &&
           !ValidateAutoStatusEvidence(remove.data(), remove.size()-1, apply.data(), apply.size()) &&
           !ValidateAutoStatusEvidence(remove.data(), remove.size(), apply.data(), apply.size()-1),
           "missing or truncated native status bodies must reject admission");
    for (size_t i = 0; i < remove.size(); ++i) {
        remove[i] ^= 1;
        Expect(!ValidateAutoStatusEvidence(remove.data(), remove.size(), apply.data(), apply.size()),
               "every native remove byte must participate in admission");
        remove[i] ^= 1;
    }
    for (size_t i = 0; i < apply.size(); ++i) {
        apply[i] ^= 1;
        Expect(!ValidateAutoStatusEvidence(remove.data(), remove.size(), apply.data(), apply.size()),
               "every native apply byte must participate in admission");
        apply[i] ^= 1;
    }
}

int main() {
    TestAutoStatusAdmissionAndFaults();
    TestAutoStatusBodyAdmission();
    TestStatusAndElementApplyEditRestore();
    TestEveryAutoStatusMapsToItsNativeWord();
    TestNeutralDefaults();
    TestStrictBooleanAndMalformedInput();
    TestStatusResistanceArraysAndCanaries();
    TestRangesUnknownKeysAndInputCap();
    TestExactRoundTrip();
    TestPersistenceAllowlist();
    TestTransactionalBaselineApplyEditAndRestore();
    TestAreaReplacementAndActorReuse();
    TestRestoreRejectsReusedActorIdentity();
    TestBattleFieldPublicationIsOneShotAndConsecutive();
    TestExplicitCaptureSuppressionIsScoped();
    TestMissingBattleFieldUsesExplicitFallbackRule();
    TestDamageStateSurvivesEditAndOff();
    TestHealingStateSurvivesOff();
    TestOverhealClampsOnOff();
    TestMpSpendStateSurvivesEditAndOff();
    TestExactStatOffsetMapping();
    TestInvalidFormationOverflowAndFaults();
    TestPartialOwnershipLoss();
    TestMaximumWriteSuccessReadbackFailureFailsClosed();
    TestDynamicWriteSuccessReadbackFailureFailsClosed();
    TestInterveningGameplayCurrentRejectsStaleRetry();
    TestMaximumFailureDoesNotReuseStaleCurrentEvidence();
    TestAmbiguousCurrentReadbackAbaFailsClosed();
    TestSuccessfulWriteReturningToExpectedFailsClosed();
    TestRestoreFaultRetainsRetryableOwnership();
    TestSinCallerEvidenceConstants();
    TestProfileAndSignatureGate();
    TestValidationOnlyStartupPolicy();
    TestAllOrNothingHookTransaction();
    TestDifficultyThreeTargetCoordinatorAdapter();
    TestPostOriginalInitializerSeam();
    TestDifficultyFrameProducerWithoutMenuPump();
    TestNativePopulationCompletionProducer();
    TestNativePopulationSignature();
    TestPostOriginalInitializerComposesSinThroughDifficulty();
    TestProductionAdapterSourceContracts();
    TestDifficultyIndependentAdmission();
    TestFocusLossSafetyContract();
    TestDifficultyDocumentationContracts();

    if (g_failures != 0) {
        std::fprintf(stderr, "F7RuntimeRt0: FAIL (%d/%d failed)\n", g_failures, g_checks);
        return 1;
    }
    std::printf("F7RuntimeRt0: PASS (%d checks)\n", g_checks);
    return 0;
}
