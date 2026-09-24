#pragma once

#include <array>
#include <cstdint>

namespace FfxHooks::SinRam {

// Production wiring is RAM-only and composed inside F7Difficulty's sole actor writer. The legacy
// SinCurse compatibility API is a separate inert adapter and grants no fallback writer authority.

struct Config {
    bool enabled = false;
    int threatLevel = 0;
    // Legacy files retain their scalar for import/offline compatibility. The
    // player-facing seeded mode derives threat from the curated assignment.
    bool seeded = false;
    unsigned distribution = 0;
    std::uint32_t seed = 1;
};

enum class EncounterOrigin : std::uint8_t {
    Unknown,
    Natural,
    Force,
    Arena,
    Ultra,
    CustomMix,
};

struct EncounterEvidence {
    // The token's complete high word is the exact field key. monsterId is the actor record's raw
    // u16 formation word; neither value is a mask, display name, species alias, or low-byte key.
    std::uint32_t encounterToken = 0;
    std::uint16_t monsterId = 0xFFFFu;
    EncounterOrigin origin = EncounterOrigin::Unknown;
    std::uint32_t transitionCallerRva = 0;
};

// Both the request identity and the generation must agree. The identity binds
// the transition to its actor initialization; the generation proves neither
// observation was retained across a later battle transition.
struct RequestCorrelation {
    std::uint64_t transitionRequestId = 0;
    std::uint64_t actorRequestId = 0;
    std::uint64_t transitionGeneration = 0;
    std::uint64_t actorGeneration = 0;
    std::uint64_t currentGeneration = 0;
};

// Runtime passes only immutable admission evidence. Actor identity and the
// after-Difficulty structural values are supplied by the sole memory owner at
// the call site, so this request cannot become a second actor writer.
struct RuntimeRequest {
    Config config{};
    bool scriptManaged = false;
    std::uint8_t scriptActorMask = 0xFFu;
    std::uint32_t areaVisit = 0;
    std::uint32_t encounterToken = 0;
    EncounterOrigin origin = EncounterOrigin::Unknown;
    // The random-step argument identifies the field; its native lookup result
    // is a separate index used by Difficulty. Production captures both together.
    std::int32_t nativeFieldRow = -1;
    std::uint32_t transitionCallerRva = 0;
    std::uint64_t transitionRequestId = 0;
    std::uint64_t actorRequestId = 0;
    std::uint64_t transitionGeneration = 0;
};

struct HpRatioIdentity {
    std::uint32_t maxHp = 0;
    std::uint32_t currentHp = 0;
};

// Difficulty supplies only fields that can be composed before the one final HP
// ratio calculation. Current HP is deliberately absent so it cannot be rounded twice.
struct DifficultyValues {
    std::uint32_t maxHp = 0;
    std::uint32_t overkill = 0;
    std::array<std::uint8_t, 8> stats{};
};

struct ScaleRequest {
    Config config{};
    EncounterEvidence encounter{};
    RequestCorrelation correlation{};
    HpRatioIdentity preDifficultyHp{};
    DifficultyValues afterDifficulty{};
};

enum class AdmissionReason : std::uint8_t {
    Admitted,
    Disabled,
    InvalidThreatLevel,
    WrongTransitionCaller,
    NonNaturalOrigin,
    RequestMismatch,
    StaleObservation,
    UnsupportedField,
    UnsupportedMonster,
    RuntimeFieldMismatch,
    OutOfDomain,
    Uncursed,
    InvalidDistribution,
};

// MP and every non-stat actor field are absent by construction. A runtime adapter
// that consumes this type has no S.I.N.-provided MP value it could write back.
struct ActorWriteback {
    std::uint32_t maxHp = 0;
    std::uint32_t currentHp = 0;
    std::uint32_t overkill = 0;
    std::array<std::uint8_t, 8> stats{};
};

// This narrower plan deliberately omits current HP. The shared Difficulty
// runtime consumes it before its one compare-safe maximum/current transaction,
// so S.I.N. cannot introduce a second ratio calculation or an MP writer.
struct StructuralScalePlan {
    bool admitted = false;
    AdmissionReason reason = AdmissionReason::Disabled;
    std::uint16_t fieldKey = 0;
    DifficultyValues writeback{};
};

struct ScalePlan {
    bool admitted = false;
    AdmissionReason reason = AdmissionReason::Disabled;
    std::uint16_t fieldKey = 0;
    ActorWriteback writeback{};
};

// Runtime composition uses one indivisible admission/scaling gate and leaves the
// final current-HP ratio to the sole F7Difficulty memory owner.
StructuralScalePlan BuildStructuralScalePlan(const ScaleRequest& request);

// This is the closed Runtime boundary: Runtime supplies its captured formation,
// generation, field row, and after-Difficulty values. The request cannot replace
// any of them, and the returned structural dwords remain signed-domain safe.
StructuralScalePlan BuildRuntimeStructuralScalePlan(
    const RuntimeRequest& request, std::uint64_t runtimeGeneration,
    std::int32_t runtimeFieldRow, std::uint16_t formationId,
    const DifficultyValues& afterDifficulty);

bool StructuralWritebackInDomain(const DifficultyValues& writeback);

// Offline consumers that need a complete value-only plan reuse the same gate and
// add exactly one final current-HP ratio after structural composition.
ScalePlan BuildScalePlan(const ScaleRequest& request);

}  // namespace FfxHooks::SinRam
