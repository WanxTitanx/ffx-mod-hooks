#include "SinRamScalingCore.h"
#include "SinSpreadCore.h"

#include <algorithm>

namespace FfxHooks::SinRam {
namespace {

// Offline RE profile: FFX.exe PE32/I386, SHA-256
// 78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED.
// The complete encounter-token high word is the field-row key: mcfr=0x0136
// (310) and mcyt=0x0154 (340). Keeping the full word rejects masked aliases.
constexpr std::uint16_t kMacalaniaForestFieldKey = 310u;
constexpr std::uint16_t kMacalaniaOpenFieldKey = 340u;
// Random walking encounters call MsBattleEncountExe from VA 0x00871CEA.
// The former 0x381D8C observation belongs to a label/script path and misses
// random encounters entirely. Retained label observations confer no authority.
constexpr std::uint32_t kNaturalTransitionCallerRva = 0x00471CEFu;
constexpr std::uint32_t kSignedDwordMax = 0x7FFFFFFFu;

bool IsThreatLevelValid(int threatLevel) {
    return threatLevel >= 0 && threatLevel <= 2;
}

std::uint16_t ExactFieldKey(std::uint32_t encounterToken) {
    // WHY: the field identity is the complete high word. Narrowing it to a byte
    // would admit unrelated fields such as 0x0236 as aliases of mcfr (0x0136).
    return static_cast<std::uint16_t>(encounterToken >> 16u);
}

bool IsSupportedMonster(std::uint16_t fieldKey, std::uint16_t monsterId) {
    fieldKey=SinSpread::AreaForField(fieldKey);
    // Closed natural encounter pairs. Raw actor IDs carry the exact 0x1000
    // monster family tag; only that family decodes to these file/model IDs:
    // mcyt/340 x {4,12,19,37} and mcfr/310 x {3,26,33,81,87,217}.
    // Names and low-byte aliases confer no authority; every other pair fails closed.
    if (fieldKey == kMacalaniaOpenFieldKey) {
        switch (SinSpread::ModelFromNative(monsterId)) {
            case 4u:
            case 12u:
            case 19u:
            case 37u:
                return true;
            default:
                return false;
        }
    }

    if (fieldKey == kMacalaniaForestFieldKey) {
        switch (SinSpread::ModelFromNative(monsterId)) {
            case 3u:
            case 26u:
            case 33u:
            case 81u:
            case 87u:
            case 217u:
                return true;
            default:
                return false;
        }
    }

    return false;
}

bool RequestIdentityMatches(const RequestCorrelation& correlation) {
    return correlation.transitionRequestId != 0u &&
           correlation.transitionRequestId == correlation.actorRequestId;
}

bool ObservationIsFresh(const RequestCorrelation& correlation) {
    return correlation.transitionGeneration != 0u &&
           correlation.transitionGeneration == correlation.actorGeneration &&
           correlation.transitionGeneration == correlation.currentGeneration;
}

std::uint32_t ScaleSignedDword(std::uint32_t value, std::uint32_t percent) {
    // WHY: the positive legacy cast truncates the S.I.N. multiplier. Round-nearest
    // belongs only to the final current-HP ratio and must not leak into this layer.
    const std::uint64_t scaled =
        (static_cast<std::uint64_t>(value) * percent) / 100u;
    // WHY: these actor fields are consumed as signed dwords by the supported
    // executable. UINT32_MAX would become negative even though the C++ carrier is unsigned.
    return static_cast<std::uint32_t>(
        (std::min)(scaled, static_cast<std::uint64_t>(kSignedDwordMax)));
}

std::uint8_t ScaleStat(std::uint8_t value, int threatLevel) {
    const std::uint32_t percent =
        100u + static_cast<std::uint32_t>(threatLevel) * 5u;
    // WHY: the legacy percentage truncates before the separate flat +T bonus.
    const std::uint32_t scaled =
        (static_cast<std::uint32_t>(value) * percent) / 100u;
    const std::uint32_t incremented =
        scaled + static_cast<std::uint32_t>(threatLevel);
    return static_cast<std::uint8_t>((std::min)(incremented, 255u));
}

std::uint32_t ApplyHpRatioOnce(
    const HpRatioIdentity& baseline, std::uint32_t finalMaxHp) {
    if (baseline.maxHp == 0u) return 0u;

    const std::uint32_t cappedCurrent =
        (std::min)(baseline.currentHp, baseline.maxHp);
    // WHY: Difficulty contributes its new maximum without first rounding current
    // HP. Unlike the truncating S.I.N. layer, this final ratio alone rounds to
    // nearest, after S.I.N. produces the final maximum.
    const std::uint64_t rounded =
        (static_cast<std::uint64_t>(cappedCurrent) * finalMaxHp +
         baseline.maxHp / 2u) /
        baseline.maxHp;
    return static_cast<std::uint32_t>(
        (std::min)(rounded, static_cast<std::uint64_t>(finalMaxHp)));
}

}  // namespace

StructuralScalePlan BuildStructuralScalePlan(const ScaleRequest& request) {
    StructuralScalePlan plan{};
    plan.fieldKey = ExactFieldKey(request.encounter.encounterToken);

    if (!request.config.enabled) {
        plan.reason = AdmissionReason::Disabled;
        return plan;
    }
    if (!IsThreatLevelValid(request.config.threatLevel)) {
        plan.reason = AdmissionReason::InvalidThreatLevel;
        return plan;
    }
    if (request.encounter.transitionCallerRva != kNaturalTransitionCallerRva) {
        plan.reason = AdmissionReason::WrongTransitionCaller;
        return plan;
    }
    if (request.encounter.origin != EncounterOrigin::Natural) {
        plan.reason = AdmissionReason::NonNaturalOrigin;
        return plan;
    }
    if (!RequestIdentityMatches(request.correlation)) {
        plan.reason = AdmissionReason::RequestMismatch;
        return plan;
    }
    if (!ObservationIsFresh(request.correlation)) {
        plan.reason = AdmissionReason::StaleObservation;
        return plan;
    }
    if (!SinSpread::AreaForField(plan.fieldKey)) {
        plan.reason = AdmissionReason::UnsupportedField;
        return plan;
    }
    if (!IsSupportedMonster(plan.fieldKey, request.encounter.monsterId)) {
        plan.reason = AdmissionReason::UnsupportedMonster;
        return plan;
    }

    const std::uint32_t hpPercent =
        100u + static_cast<std::uint32_t>(request.config.threatLevel) * 10u;
    plan.writeback.maxHp =
        ScaleSignedDword(request.afterDifficulty.maxHp, hpPercent);
    plan.writeback.overkill =
        ScaleSignedDword(request.afterDifficulty.overkill, hpPercent);
    plan.writeback.stats = request.afterDifficulty.stats;
    for (std::uint8_t& stat : plan.writeback.stats) {
        stat = ScaleStat(stat, request.config.threatLevel);
    }

    plan.admitted = true;
    plan.reason = AdmissionReason::Admitted;
    return plan;
}

bool StructuralWritebackInDomain(const DifficultyValues& writeback) {
    // WHY: FFX.exe 78CE... consumes +0x594 and +0x5A4 as signed dwords.
    // Rejecting the sign bit here keeps impossible output outside ownership and I/O.
    return writeback.maxHp <= kSignedDwordMax &&
           writeback.overkill <= kSignedDwordMax;
}

StructuralScalePlan BuildRuntimeStructuralScalePlan(
    const RuntimeRequest& request, std::uint64_t runtimeGeneration,
    std::int32_t runtimeFieldRow, std::uint16_t formationId,
    const DifficultyValues& afterDifficulty) {
    ScaleRequest scaleRequest{};
    scaleRequest.config = request.config;
    SinSpread::AreaAssignment assignment{};
    const SinSpread::AssignedCurse* assigned=nullptr;
    if(request.config.seeded) {
        if(!SinSpread::ValidDistribution(request.config.distribution)) {
            StructuralScalePlan invalid{};invalid.reason=AdmissionReason::InvalidDistribution;return invalid;
        }
        assignment=SinSpread::BuildAssignment(ExactFieldKey(request.encounterToken),request.config.seed,
            request.areaVisit,static_cast<SinSpread::Distribution>(request.config.distribution),request.config.enabled);
        assigned=assignment.Find(SinSpread::ModelFromNative(formationId));
        scaleRequest.config.threatLevel=assigned?static_cast<int>(assigned->threat):0;
    }
    scaleRequest.encounter.encounterToken = request.encounterToken;
    scaleRequest.encounter.monsterId = formationId;
    scaleRequest.encounter.origin = request.origin;
    scaleRequest.encounter.transitionCallerRva = request.transitionCallerRva;
    scaleRequest.correlation.transitionRequestId = request.transitionRequestId;
    scaleRequest.correlation.actorRequestId = request.actorRequestId;
    scaleRequest.correlation.transitionGeneration = request.transitionGeneration;
    scaleRequest.correlation.actorGeneration = runtimeGeneration;
    scaleRequest.correlation.currentGeneration = runtimeGeneration;
    scaleRequest.afterDifficulty = afterDifficulty;

    StructuralScalePlan plan = BuildStructuralScalePlan(scaleRequest);
    if (!plan.admitted) return plan;
    if(request.config.seeded && assigned && !assigned->curse) {
        plan.admitted=false;plan.reason=AdmissionReason::Uncursed;plan.writeback={};return plan;
    }

    // WHY: the natural transition token and Runtime's consumed field ticket are
    // independent observations. Both must identify the complete u16 field row.
    if (runtimeFieldRow < 0 || runtimeFieldRow > 0xFFFF ||
        runtimeFieldRow != (request.nativeFieldRow >= 0 ? request.nativeFieldRow : plan.fieldKey)) {
        plan.admitted = false;
        plan.reason = AdmissionReason::RuntimeFieldMismatch;
        plan.writeback = {};
        return plan;
    }
    if (!StructuralWritebackInDomain(plan.writeback)) {
        plan.admitted = false;
        plan.reason = AdmissionReason::OutOfDomain;
        plan.writeback = {};
    }
    return plan;
}

ScalePlan BuildScalePlan(const ScaleRequest& request) {
    const StructuralScalePlan structural = BuildStructuralScalePlan(request);
    ScalePlan plan{};
    plan.admitted = structural.admitted;
    plan.reason = structural.reason;
    plan.fieldKey = structural.fieldKey;
    if (!structural.admitted) return plan;

    plan.writeback.maxHp = structural.writeback.maxHp;
    plan.writeback.currentHp =
        ApplyHpRatioOnce(request.preDifficultyHp, structural.writeback.maxHp);
    plan.writeback.overkill = structural.writeback.overkill;
    plan.writeback.stats = structural.writeback.stats;
    return plan;
}

}  // namespace FfxHooks::SinRam
