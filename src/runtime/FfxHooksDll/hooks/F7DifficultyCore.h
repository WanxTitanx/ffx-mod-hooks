#pragma once

#include "MinHookBatchCoordinator.h"
#include "SinRamScalingCore.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::F7Difficulty {

inline constexpr size_t kStatusCount = 25;
inline constexpr size_t kMaxAreaRules = 16;
inline constexpr size_t kMaxJsonBytes = 16384;

// FFX.exe 78CE... uses eight 0xF90-byte runtime actor records. Only offsets whose
// widths have executable xref evidence are exposed to the writer below.
inline constexpr size_t kActorSlots = 8;
inline constexpr size_t kActorSpan = 0xF90;
inline constexpr size_t kFormationIdOffset = 0x00E;
inline constexpr size_t kMaxHpOffset = 0x594;
inline constexpr size_t kMaxMpOffset = 0x598;
inline constexpr size_t kOverkillOffset = 0x5A4;
inline constexpr size_t kStatCount = 8;
inline constexpr std::array<size_t, kStatCount> kStatOffsets = {
    0x5A8, 0x5A9, 0x5AA, 0x5AB, 0x5AC, 0x5AD, 0x5AE, 0x5AF,
};
inline constexpr size_t kCurrentHpOffset = 0x5D0;
inline constexpr size_t kCurrentMpOffset = 0x5D4;

// Exact 78CE... byte/word accesses and native status refresh are documented in
// docs/reverse/DIFFICULTY_STATUS_ELEMENTS_2026-09-19.md.
inline constexpr size_t kElementAbsorbOffset = 0x5DA;
inline constexpr size_t kElementIgnoreOffset = 0x5DB;
inline constexpr size_t kElementResistOffset = 0x5DC;
inline constexpr size_t kElementWeakOffset = 0x5DD;
inline constexpr size_t kInnateAutoFirstOffset = 0x630;
inline constexpr size_t kInnateAutoDurationOffset = 0x632;
inline constexpr size_t kStatusResistanceOffset = 0x641;
inline constexpr size_t kUnsupportedCurrentHpScratchOffset = 0x6E4;
inline constexpr size_t kUnsupportedCurrentMpScratchOffset = 0x6E8;

inline constexpr uint16_t kSupportedMachine = 0x014Cu;
inline constexpr uint32_t kSupportedTimestamp = 0x55D2F3CCu;
inline constexpr uint32_t kSupportedSizeOfImage = 0x0237D000u;
inline constexpr uint32_t kSupportedImageBase = 0x00400000u;
inline constexpr uintptr_t kResolveEncounterRva = 0x003828B0u;
inline constexpr uintptr_t kInitSystemSceneRva = 0x00383ED0u;
inline constexpr uintptr_t kActorInitializerRva = 0x0039C130u;
inline constexpr uintptr_t kActorPopulateRva = 0x00384010u;
inline constexpr uintptr_t kActorAccessorRva = 0x00395AB0u;
// Read-only IDA evidence for FFX.exe 78CE...: ResolveEncounter has exactly
// these four direct label-lookup callers. Walking random encounters instead use
// MsBattleEncountExe; SinNaturalCore owns that independently verified origin.
inline constexpr std::array<uintptr_t, 4> kResolveEncounterCallsiteRvas = {{
    0x00381D87u, 0x00382A0Cu, 0x00382C0Eu, 0x00382C6Eu,
}};
inline constexpr uintptr_t kNaturalResolveReturnRva = 0x00381D8Cu;
// ActorInitializer has exactly one direct call inside the validated InitScene.
inline constexpr uintptr_t kActorInitializerCallsiteRva = 0x00383FB1u;
inline constexpr std::array<uint8_t, 32> kSupportedSha256 = {{
    0x78, 0xCE, 0x34, 0x39, 0x7D, 0xA5, 0xE6, 0xF4,
    0x9B, 0x72, 0xC2, 0xAE, 0xBA, 0xDE, 0xDA, 0xF4,
    0xCD, 0x3F, 0x67, 0x20, 0xE1, 0x94, 0x9D, 0x46,
    0xA1, 0xB8, 0xED, 0x67, 0xD3, 0xDB, 0x5C, 0xED,
}};
// KEY: these instruction-aligned prefixes are direct loaded-memory evidence for all four
// Difficulty targets in the exact 78CE... profile. Validation reconstructs all five HIGHLOW
// operands at the actual loaded base: three InitScene, one initializer, and one accessor.
inline constexpr std::array<uint8_t, 21> kResolveEncounterPrefix = {{
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08, 0x53,
    0x0F, 0xB7, 0xD8, 0x56, 0xC1, 0xF8, 0x10, 0x25,
    0xFF, 0xFF, 0x00, 0x00, 0x57,
}};
inline constexpr std::array<uint8_t, 24> kInitSystemScenePreferredPrefix = {{
    0x8B, 0x0D, 0xA8, 0xA9, 0x12, 0x01, 0x56, 0x8B,
    0x41, 0x04, 0x0F, 0xBE, 0x35, 0xD9, 0xC9, 0x12,
    0x01, 0x03, 0xC1, 0xA3, 0xAC, 0xA9, 0x12, 0x01,
}};
inline constexpr std::array<uint8_t, 15> kActorInitializerPrefix = {{
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56, 0x57, 0x6A,
    0x60, 0x68, 0x00, 0x60, 0x13, 0x01, 0xE8,
}};
inline constexpr std::array<uint8_t, 21> kActorAccessorSignature = {{
    0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x45, 0x08,
    0x69, 0xC0, 0x90, 0x0F, 0x00, 0x00,
    0x03, 0x05, 0x60, 0x44, 0x13, 0x01, 0x5D, 0xC3,
}};

// Full resumable actor-population body, VA 0x00784010, exact 78CE... executable.
inline constexpr std::array<uint8_t,89> kActorPopulatePreferredBody = {{
    0x53,0x0F,0xBE,0x1D,0x29,0xA9,0x12,0x01,0xC6,0x05,0xE0,0xA8,0x12,0x01,0x12,0x83,
    0xFB,0x12,0x7D,0x2D,0x53,0xE8,0x06,0x00,0x01,0x00,0x83,0xC4,0x04,0x83,0xFB,0x12,
    0x7D,0x19,0x80,0xB8,0xC8,0x0D,0x00,0x00,0x00,0x74,0x10,0x6A,0x00,0x50,0x53,0xE8,
    0x8C,0x01,0x00,0x00,0x83,0xC4,0x0C,0x85,0xC0,0x74,0x11,0x43,0x83,0xFB,0x12,0x7C,
    0xD3,0xC6,0x05,0xE0,0xA8,0x12,0x01,0x13,0x33,0xC0,0x5B,0xC3,0xFE,0xC3,0x88,0x1D,
    0x29,0xA9,0x12,0x01,0x83,0xC8,0xFF,0x5B,0xC3,
}};

// Position-independent cdecl(actor*) native status remove/apply bodies. They
// preserve temporary effects through the game-owned backup fields. No detours.
inline constexpr uintptr_t kAutoStatusRemoveRva = 0x0039B1B0u;
inline constexpr std::array<uint8_t, 239> kAutoStatusRemoveBody = {{
    0x55,0x8B,0xEC,0x83,0xEC,0x08,0x53,0x56,0x57,0x8B,0x7D,0x08,0x33,0xF6,0x0F,0xB7,
    0x87,0x2A,0x06,0x00,0x00,0x0F,0xB7,0x9F,0x06,0x06,0x00,0x00,0x89,0x45,0xFC,0x0F,
    0xB7,0x87,0x18,0x06,0x00,0x00,0x8B,0x7D,0xFC,0x89,0x45,0xF8,0x8D,0x56,0x01,0x90,
    0x8B,0xC7,0x66,0x8B,0xCE,0x66,0xD3,0xE8,0xA8,0x01,0x74,0x18,0x8B,0xCE,0x66,0xD3,
    0xE2,0x8B,0xC2,0x23,0x55,0xF8,0xF7,0xD0,0x23,0xC3,0x0B,0xC2,0x0F,0xB7,0xD8,0xBA,
    0x01,0x00,0x00,0x00,0x46,0x83,0xFE,0x0C,0x7C,0xD6,0x8B,0x7D,0x08,0x33,0xC0,0x0F,
    0xB7,0xB7,0x2C,0x06,0x00,0x00,0x66,0x89,0x9F,0x06,0x06,0x00,0x00,0x8D,0x49,0x00,
    0x8B,0xD6,0x66,0x8B,0xC8,0x66,0xD3,0xEA,0xF6,0xC2,0x01,0x74,0x0E,0x8A,0x8C,0x38,
    0x1A,0x06,0x00,0x00,0x88,0x8C,0x38,0x08,0x06,0x00,0x00,0x40,0x83,0xF8,0x0D,0x7C,
    0xDF,0x0F,0xB7,0x87,0x2E,0x06,0x00,0x00,0x0F,0xB7,0x9F,0x16,0x06,0x00,0x00,0x89,
    0x45,0xF8,0x0F,0xB7,0x87,0x28,0x06,0x00,0x00,0x8B,0x7D,0xF8,0x89,0x45,0xFC,0x33,
    0xF6,0x8B,0xC7,0x66,0x8B,0xCE,0x66,0xD3,0xE8,0xA8,0x01,0x74,0x1B,0xB8,0x01,0x00,
    0x00,0x00,0x0F,0xB7,0xD0,0x8B,0xCE,0x66,0xD3,0xE2,0x8B,0xC2,0x23,0x55,0xFC,0xF7,
    0xD0,0x23,0xC3,0x0B,0xC2,0x0F,0xB7,0xD8,0x46,0x83,0xFE,0x10,0x7C,0xD3,0x8B,0x7D,
    0x08,0x66,0x89,0x9F,0x16,0x06,0x00,0x00,0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3,
}};
inline constexpr uintptr_t kAutoStatusApplyRva = 0x0039B2A0u;
inline constexpr std::array<uint8_t, 470> kAutoStatusApplyBody = {{
    0x55,0x8B,0xEC,0x83,0xEC,0x10,0x8B,0x45,0x08,0x53,0x0F,0xB6,0x88,0x12,0x06,0x00,
    0x00,0x0F,0xB6,0x90,0x3E,0x06,0x00,0x00,0x0F,0xB7,0x98,0x34,0x06,0x00,0x00,0x56,
    0x0F,0xB7,0xB0,0x30,0x06,0x00,0x00,0x57,0x0F,0xB7,0xB8,0x32,0x06,0x00,0x00,0x89,
    0x4D,0xF0,0x0F,0xB6,0x88,0x13,0x06,0x00,0x00,0x89,0x4D,0xF4,0x66,0x89,0xB0,0x2A,
    0x06,0x00,0x00,0x66,0x89,0xB8,0x2C,0x06,0x00,0x00,0x66,0x89,0x98,0x2E,0x06,0x00,
    0x00,0x83,0xFA,0x01,0x74,0x05,0x83,0xFA,0x02,0x75,0x33,0x0F,0xB7,0x88,0x36,0x06,
    0x00,0x00,0x66,0x0B,0xCE,0x66,0x89,0x88,0x2A,0x06,0x00,0x00,0x0F,0xB7,0x88,0x38,
    0x06,0x00,0x00,0x66,0x0B,0xCF,0x66,0x89,0x88,0x2C,0x06,0x00,0x00,0x0F,0xB7,0x88,
    0x3A,0x06,0x00,0x00,0x66,0x0B,0xCB,0x66,0x89,0x88,0x2E,0x06,0x00,0x00,0x0F,0xB7,
    0x88,0x18,0x06,0x00,0x00,0x0F,0xB7,0x98,0x06,0x06,0x00,0x00,0x88,0x90,0x3F,0x06,
    0x00,0x00,0x0F,0xB7,0x90,0x2A,0x06,0x00,0x00,0x33,0xFF,0x89,0x55,0xF8,0x89,0x4D,
    0xFC,0x8D,0x77,0x01,0x8B,0xC1,0x66,0x8B,0xCF,0x66,0xD3,0xEA,0xF6,0xC2,0x01,0x74,
    0x1B,0x8B,0xCF,0x66,0xD3,0xE6,0x8B,0xD6,0xF7,0xD2,0x8B,0xCE,0x23,0xD0,0x23,0xCB,
    0x0B,0xD1,0x0B,0xDE,0x0F,0xB7,0xC2,0xBE,0x01,0x00,0x00,0x00,0x8B,0x55,0xF8,0x47,
    0x83,0xFF,0x0C,0x7C,0xD1,0x89,0x45,0xFC,0x8B,0x45,0x08,0x8B,0x4D,0xFC,0x0F,0xB7,
    0xB8,0x2C,0x06,0x00,0x00,0x66,0x89,0x98,0x06,0x06,0x00,0x00,0x66,0x89,0x88,0x18,
    0x06,0x00,0x00,0x33,0xF6,0x8B,0xD7,0x66,0x8B,0xCE,0x66,0xD3,0xEA,0xF6,0xC2,0x01,
    0x74,0x16,0x8A,0x8C,0x06,0x08,0x06,0x00,0x00,0x88,0x8C,0x06,0x1A,0x06,0x00,0x00,
    0xC6,0x84,0x06,0x08,0x06,0x00,0x00,0xFF,0x46,0x83,0xFE,0x0D,0x7C,0xD7,0x83,0x7D,
    0xF4,0x00,0x75,0x19,0x80,0xB8,0x13,0x06,0x00,0x00,0x00,0x74,0x10,0x80,0xB8,0x14,
    0x06,0x00,0x00,0xFF,0x73,0x07,0xC6,0x80,0x14,0x06,0x00,0x00,0x00,0x0F,0xB7,0x90,
    0x2E,0x06,0x00,0x00,0x0F,0xB7,0x88,0x28,0x06,0x00,0x00,0x0F,0xB7,0x98,0x16,0x06,
    0x00,0x00,0x89,0x55,0xF4,0x89,0x4D,0xFC,0x33,0xFF,0x8B,0xC1,0x8D,0x64,0x24,0x00,
    0x66,0x8B,0xCF,0x66,0xD3,0xEA,0xF6,0xC2,0x01,0x74,0x1E,0xB9,0x01,0x00,0x00,0x00,
    0x0F,0xB7,0xF1,0x8B,0xCF,0x66,0xD3,0xE6,0x8B,0xD6,0xF7,0xD2,0x23,0xD0,0x8B,0xCE,
    0x23,0xCB,0x0B,0xD1,0x0F,0xB7,0xC2,0x0B,0xDE,0x8B,0x55,0xF4,0x47,0x83,0xFF,0x10,
    0x7C,0xCE,0x89,0x45,0xFC,0x8B,0x45,0x08,0x8B,0x4D,0xFC,0x5F,0x66,0x89,0x98,0x16,
    0x06,0x00,0x00,0x66,0x89,0x88,0x28,0x06,0x00,0x00,0x80,0xB8,0x12,0x06,0x00,0x00,
    0x00,0x5E,0x5B,0x74,0x0D,0x83,0x7D,0xF0,0x00,0x75,0x07,0xC6,0x80,0xD2,0x06,0x00,
    0x00,0x00,0x8B,0xE5,0x5D,0xC3,
}};
bool ValidateAutoStatusEvidence(const uint8_t* removeBytes, size_t removeLength,
                                const uint8_t* applyBytes, size_t applyLength);

// Jarvis-HOOK 2026-09-19, exact 78CE... executable: state 1 is active battle
// (write at VA 0x00783693), 0x13..0x16 follow population, zero is teardown
// (0x007816F1/0x00782744). An arbitrary high phase such as 0x22 is not ready.
inline constexpr bool IsPostPopulateBattlePhase(uint8_t phase) noexcept {
    return phase == 1 || (phase >= 0x13 && phase <= 0x16);
}
inline constexpr bool CanServiceDifficultyRetry(
    uint32_t ownerThread, uint32_t currentThread, bool insideInitializer) noexcept {
    return ownerThread != 0 && ownerThread == currentThread && !insideInitializer;
}

struct Preset {
    bool enabled = false;
    int32_t hpMul = 1000;
    int32_t mpMul = 1000;
    int32_t strMul = 1000;
    int32_t defMul = 1000;
    int32_t magMul = 1000;
    int32_t mdfMul = 1000;
    int32_t agiMul = 1000;
    int32_t accMul = 1000;
    int32_t evaMul = 1000;
    int32_t lckMul = 1000;
    int32_t overkillMul = 1000;

    // AUTO adds native innate effects. Selected affinities replace competing
    // affinities for those elements only; resistance is a minimum, not a nerf.
    uint32_t autoStatusMask = 0;
    uint8_t elemWeak = 0;
    uint8_t elemResist = 0;
    uint8_t elemAbsorb = 0;
    std::array<uint8_t, kStatusCount> statusResist{};
};

struct AreaRule {
    bool enabled = false;
    int32_t fieldRow = -1;
    Preset preset{};
};

struct DifficultyConfig {
    Preset global{};
    bool byArea = false;
    std::array<AreaRule, kMaxAreaRules> areas{};
    size_t areaCount = 0;
};

Preset MakeNeutralPreset();
DifficultyConfig MakeNeutralConfig();

enum class ConfigCode : uint8_t {
    Ok = 0,
    InvalidArgument,
    TooLarge,
    Malformed,
    DuplicateKey,
    OutOfRange,
    TooManyArrayItems,
    TooManyAreaRules,
    OutputTooSmall,
};

struct ConfigResult {
    ConfigCode code = ConfigCode::InvalidArgument;
    size_t offset = 0;
    bool clamped = false;
};

ConfigResult ParseConfig(const char* json, size_t length, DifficultyConfig* output);
ConfigResult SerializeConfig(
    const DifficultyConfig& config, char* output, size_t capacity, size_t* lengthOut);

struct PersistenceIo {
    void* context = nullptr;
    bool (*read)(void*, const char*, char*, size_t, size_t*) = nullptr;
    // Production supplies an atomic same-directory implementation. The portable core owns the
    // path allowlist and byte cap, never a secondary temp/battle/save/executable path.
    bool (*writeAtomic)(void*, const char*, const char*, size_t) = nullptr;
};

enum class PersistenceCode : uint8_t {
    Ok = 0,
    InvalidArgument,
    PathRejected,
    TooLarge,
    IoError,
};

bool IsAllowedPersistencePath(const char* path);
PersistenceCode ReadDocument(
    const PersistenceIo& io, const char* path, char* output, size_t capacity, size_t* lengthOut);
PersistenceCode WriteDocument(
    const PersistenceIo& io, const char* path, const char* data, size_t length);

struct MemoryIo {
    void* context = nullptr;
    bool (*read)(void*, uintptr_t, void*, size_t) = nullptr;
    bool (*write)(void*, uintptr_t, const void*, size_t) = nullptr;
    // Reconcile changed innate masks through the validated native remove/apply
    // pair. False is an ambiguous native failure: never retry that actor generation.
    bool (*refreshAutoStatus)(void*, uintptr_t) = nullptr;
};

struct ActorRef {
    uintptr_t address = 0;
    uint8_t slot = 0;
};

enum class BattleFieldSource : uint8_t {
    Missing = 0,
    Natural,
    Force,
    Arena,
    Ultra,
    CustomMix,
};

struct BattleFieldTicket {
    int32_t fieldRow = -1;
    BattleFieldSource source = BattleFieldSource::Missing;
    uint32_t request = 0;
};

using BattleFieldRequest = uint32_t;

// A launch request must still be current when it commits. This prevents a late failed/cancelled
// explicit route from overwriting the Natural route that superseded it.
class BattleFieldPublication {
public:
    BattleFieldRequest BeginRequest();
    bool Commit(
        BattleFieldRequest request, int32_t fieldRow, BattleFieldSource source);
    void Cancel(BattleFieldRequest request);
    void Publish(int32_t fieldRow, BattleFieldSource source);
    BattleFieldTicket Consume();

private:
    std::atomic<uint32_t> nextRequest_{0};
    std::atomic<uint64_t> pending_{0};
};

// Explicit routes suppress only synchronous ResolveEncounter capture inside their lexical
// launch scope. A depth counter makes nested fallback routes safe without leaking into the next
// unrelated Natural encounter.
class EncounterCaptureSuppression {
public:
    void Begin();
    void End();
    bool Active() const;

private:
    std::atomic<uint32_t> depth_{0};
};

enum class ResultCode : uint8_t {
    Unavailable = 0,
    NoActors,
    Applied,
    Restored,
    OwnershipLost,
    InvalidConfig,
    Fault,
};

struct RuntimeResult {
    ResultCode code = ResultCode::Unavailable;
    size_t actorsSeen = 0;
    size_t fieldsWritten = 0;
    size_t fieldsRestored = 0;
    size_t ownershipLost = 0;
    size_t faults = 0;
    size_t autoStatusRefreshed = 0;
};

class Runtime {
public:
    void BeginGeneration(uint64_t generation);
    RuntimeResult Update(
        const MemoryIo& io, const DifficultyConfig& config, bool configValid,
        int32_t fieldRow, const ActorRef* actors, size_t actorCount);
    RuntimeResult UpdateComposed(
        const MemoryIo& io, const DifficultyConfig& config, bool configValid,
        int32_t fieldRow, const SinRam::RuntimeRequest& request,
        const ActorRef* actors, size_t actorCount);
    RuntimeResult Restore(const MemoryIo& io);
    // Read-only: true while any captured field is still Owned or Indeterminate, so the UI
    // can distinguish "Off preset with fields to restore" from "nothing ever written".
    bool HasOwnedOrIndeterminateFields() const;

private:
    static constexpr size_t kRuntimeFieldCount = 44;

    enum class Ownership : uint8_t { Unowned = 0, Owned, Indeterminate, Lost };

    struct DynamicPairTransaction {
        bool pending = false;
        bool currentWritePending = false;
        uint32_t sourceMaximum = 0;
        uint32_t sourceCurrent = 0;
        uint32_t targetMaximum = 0;
        uint32_t currentObservedBeforeWrite = 0;
        uint32_t currentWriteTarget = 0;
    };

    struct ActorSnapshot {
        bool captured = false;
        uintptr_t address = 0;
        uint16_t formation = 0xFFFFu;
        std::array<uint32_t, kRuntimeFieldCount> baseline{};
        std::array<uint32_t, kRuntimeFieldCount> lastApplied{};
        std::array<uint32_t, kRuntimeFieldCount> pendingTarget{};
        std::array<Ownership, kRuntimeFieldCount> ownership{};
        std::array<DynamicPairTransaction, 2> dynamicTransactions{};
        bool autoRefreshPending = false;
        bool autoRefreshFailed = false;
    };

    bool AdmitAutoStatus(const MemoryIo& io, ActorSnapshot& snapshot,
                         const std::array<uint32_t, kRuntimeFieldCount>& desired,
                         RuntimeResult& result);
    void RefreshAutoStatus(const MemoryIo& io, ActorSnapshot& snapshot, RuntimeResult& result);

    uint64_t generation_ = 0;
    bool generationStarted_ = false;
    std::array<ActorSnapshot, kActorSlots> snapshots_{};
};

struct ExecutableIdentity {
    uint16_t machine = 0;
    uint32_t timestamp = 0;
    uint32_t sizeOfImage = 0;
    uint32_t imageBase = 0;
    std::array<uint8_t, 32> sha256{};
};

struct AdapterEvidence {
    const ExecutableIdentity* identity = nullptr;
    uintptr_t loadedImageBase = 0;
    const uint8_t* resolverBytes = nullptr;
    size_t resolverLength = 0;
    const uint8_t* initSceneBytes = nullptr;
    size_t initSceneLength = 0;
    const uint8_t* initializerBytes = nullptr;
    size_t initializerLength = 0;
    const uint8_t* accessorBytes = nullptr;
    size_t accessorLength = 0;
};

enum class AdapterGateCode : uint8_t {
    Supported = 0,
    InvalidArgument,
    WrongMachine,
    WrongTimestamp,
    WrongImageSize,
    WrongImageBase,
    WrongSha256,
    ResolverSignatureMismatch,
    InitSceneSignatureMismatch,
    InitializerSignatureMismatch,
    AccessorSignatureMismatch,
    HookCreateFailed,
    HookEnableFailed,
    HookRollbackFailed,
    CoordinatorNotReady,
    CoordinatorBusy,
    CoordinatorPoisoned,
    RetainedInert,
    Installed,
    PopulateSignatureMismatch,
    AutoStatusSignatureMismatch,
};

AdapterGateCode ValidateAdapterEvidence(const AdapterEvidence& evidence);
bool ShouldInstallAtStartup(bool minHookReady, bool validateOnly);

// InitScene returns 8/10 only on its actor-initializing battle branch.
inline constexpr bool IsInitializedBattleSceneReturn(int result) noexcept {return result==8 || result==10;}
struct PopulateIo {
    void* context=nullptr;
    int (*callOriginal)(void*)=nullptr;
    bool (*admitted)(void*)=nullptr;
    bool (*readPhase)(void*,uint8_t*)=nullptr;
    void (*serviceRetry)(void*)=nullptr;
};
inline int RunPopulatePostOriginal(const PopulateIo& io) {
    if(!io.callOriginal)return 0;
    const int result=io.callOriginal(io.context);
    if(result!=0 || !io.admitted || !io.readPhase || !io.serviceRetry || !io.admitted(io.context))return result;
    uint8_t phase=0;
    if(io.readPhase(io.context,&phase) && phase==0x13 && io.admitted(io.context))io.serviceRetry(io.context);
    return result;
}
bool ValidatePopulateEvidence(const uint8_t* bytes,size_t length,uintptr_t loadedBase);

struct HookIo {
    void* context = nullptr;
    bool (*create)(void*, uintptr_t, void*, void**) = nullptr;
    bool (*enable)(void*, uintptr_t) = nullptr;
    bool (*disable)(void*, uintptr_t) = nullptr;
    bool (*remove)(void*, uintptr_t) = nullptr;
};

struct InstallResult {
    AdapterGateCode code = AdapterGateCode::InvalidArgument;
    bool installed = false;
};

class HookTransaction {
public:
    InstallResult Install(
        const HookIo& io, const AdapterEvidence& evidence, uintptr_t target,
        void* detour, void** originalOut);
    bool Remove(const HookIo& io);
    bool Installed() const { return installed_.load(std::memory_order_acquire); }
    bool HasHookState() const { return created_; }
    bool EverReachable() const { return everReachable_; }
    // A machine-prologue entrant can exist before the detour's first C++ counter increment.
    // Only a target that was never made reachable can permit teardown-time Restore.
    bool TeardownRestoreSafe() const { return !everReachable_; }

private:
    uintptr_t target_ = 0;
    bool created_ = false;
    std::atomic<bool> installed_{false};
    bool everReachable_ = false;
    bool quiesced_ = false;
};

// ResolveEncounter, InitSystemScene, and the actor initializer form one ownership unit. Keeping
// this transaction in the portable core lets RT0 prove the only safe removal boundary: targets
// may be removed only before the process-global coordinator makes its first Apply attempt.
inline constexpr size_t kDifficultyDetourCount = 3u;

struct DifficultyDetourIo {
    void* context = nullptr;
    bool (*create)(void*, uintptr_t, void*, void**) = nullptr;
    bool (*remove)(void*, uintptr_t) = nullptr;
};

struct DifficultyDetourSpec {
    uintptr_t target = 0u;
    void* detour = nullptr;
    void** originalOut = nullptr;
};

struct DifficultyDetourOwner {
    std::array<uintptr_t, kDifficultyDetourCount> targets{};
    std::array<bool, kDifficultyDetourCount> created{};
    bool applyAttempted = false;
    bool mayHaveRun = false;
    bool active = false;
    bool retainedInert = false;
    bool coordinatorPoisoned = false;
};

enum class DifficultyDetourCode : uint8_t {
    Installed = 0,
    Removed,
    RetainedInert,
    InvalidArgument,
    CoordinatorNotReady,
    HookCreateFailed,
    HookRollbackFailed,
    EnableFailedRetained,
    CoordinatorBusy,
    CoordinatorPoisoned,
    TeardownRetryRequired,
};

struct DifficultyDetourResult {
    DifficultyDetourCode code = DifficultyDetourCode::InvalidArgument;
    MinHookBatch::BatchReport batch{};
};

DifficultyDetourResult InstallDifficultyDetours(
    const DifficultyDetourIo& hookIo,
    MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo,
    MinHookBatch::NeutralizationFence fence,
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount>& specs,
    DifficultyDetourOwner* owner);

DifficultyDetourResult RetireDifficultyDetours(
    const DifficultyDetourIo& hookIo,
    MinHookBatch::Coordinator* coordinator,
    const MinHookBatch::BatchIo& batchIo,
    MinHookBatch::NeutralizationFence fence,
    const std::array<DifficultyDetourSpec, kDifficultyDetourCount>& specs,
    DifficultyDetourOwner* owner);

struct InitializerIo {
    void* context = nullptr;
    int (*callOriginal)(void*) = nullptr;
    uintptr_t (*getActorBySlot)(void*, uint8_t) = nullptr;
    bool (*validateActor)(void*, uintptr_t, size_t) = nullptr;
    MemoryIo memory{};
};

struct InitializerResult {
    bool originalCalled = false;
    int originalReturn = 0;
    size_t pointersRejected = 0;
    RuntimeResult runtime{};
};

InitializerResult RunInitializerPostOriginal(
    Runtime& runtime, uint64_t generation, const DifficultyConfig& config,
    bool configValid, int32_t fieldRow, const InitializerIo& io);
InitializerResult RunInitializerPostOriginal(
    Runtime& runtime, uint64_t generation, const DifficultyConfig& config,
    bool configValid, int32_t fieldRow, const SinRam::RuntimeRequest& sinRequest,
    const InitializerIo& io);

} // namespace FfxHooks::F7Difficulty
