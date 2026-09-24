#include "../hooks/SinRamScalingCore.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

int g_passed = 0;
int g_failed = 0;

void Expect(bool condition, const char* message) {
    if (condition) {
        ++g_passed;
        return;
    }
    ++g_failed;
    std::printf("FAIL: %s\n", message);
}

template <typename T, typename = void>
struct HasMaxMp : std::false_type {};

template <typename T>
struct HasMaxMp<T, std::void_t<decltype(std::declval<T&>().maxMp)>> : std::true_type {};

template <typename T, typename = void>
struct HasCurrentMp : std::false_type {};

template <typename T>
struct HasCurrentMp<T, std::void_t<decltype(std::declval<T&>().currentMp)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasCurrentHp : std::false_type {};

template <typename T>
struct HasCurrentHp<T, std::void_t<decltype(std::declval<T&>().currentHp)>>
    : std::true_type {};

static_assert(!HasMaxMp<FfxHooks::SinRam::ActorWriteback>::value,
              "the admitted writeback must have no maximum-MP surface");
static_assert(!HasCurrentMp<FfxHooks::SinRam::ActorWriteback>::value,
              "the admitted writeback must have no current-MP surface");
static_assert(!HasCurrentHp<FfxHooks::SinRam::DifficultyValues>::value,
              "Difficulty must not hand S.I.N. an already-rounded current HP");

FfxHooks::SinRam::ScaleRequest ValidRequest(
    std::uint32_t token = 0x01360000u, std::uint16_t monsterId = 3u) {
    using namespace FfxHooks::SinRam;

    ScaleRequest request{};
    request.config.enabled = true;
    request.config.threatLevel = 1;
    request.encounter.encounterToken = token;
    // Actual actor chr_id retains the native 0x1000 monster family tag.
    request.encounter.monsterId = static_cast<std::uint16_t>(0x1000u | monsterId);
    request.encounter.origin = EncounterOrigin::Natural;
    request.encounter.transitionCallerRva = 0x00471CEFu;
    request.correlation.transitionRequestId = 0x1122334455667788ull;
    request.correlation.actorRequestId = 0x1122334455667788ull;
    request.correlation.transitionGeneration = 41u;
    request.correlation.actorGeneration = 41u;
    request.correlation.currentGeneration = 41u;
    request.preDifficultyHp.maxHp = 100u;
    request.preDifficultyHp.currentHp = 21u;
    request.afterDifficulty.maxHp = 75u;
    request.afterDifficulty.overkill = 50u;
    request.afterDifficulty.stats = {{10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u}};
    return request;
}

void TestDefaultAndInvalidConfigFailClosed() {
    using namespace FfxHooks::SinRam;

    ScaleRequest request = ValidRequest();
    request.config = Config{};
    const ScalePlan disabled = BuildScalePlan(request);
    Expect(!request.config.enabled && request.config.threatLevel == 0,
           "S.I.N. RAM configuration must default to disabled threat level zero");
    Expect(!disabled.admitted && disabled.reason == AdmissionReason::Disabled,
           "the only public operation must reject the default disabled configuration");

    request.config.enabled = true;
    request.config.threatLevel = -1;
    const ScalePlan invalidLow = BuildScalePlan(request);
    request.config.threatLevel = 3;
    const ScalePlan invalidHigh = BuildScalePlan(request);
    Expect(!invalidLow.admitted &&
               invalidLow.reason == AdmissionReason::InvalidThreatLevel &&
               !invalidHigh.admitted &&
               invalidHigh.reason == AdmissionReason::InvalidThreatLevel,
           "threat levels outside zero through two must fail closed");
}

void TestExactTransitionCatalogAdmission() {
    using namespace FfxHooks::SinRam;

    struct AllowedPair {
        std::uint32_t token;
        std::uint16_t monsterId;
        std::uint16_t expectedFieldKey;
    };
    const AllowedPair allowed[] = {
        {0x01540000u, 4u, 340u},
        {0x01540015u, 12u, 340u},
        {0x0154FFFFu, 19u, 340u},
        {0x01540046u, 37u, 340u},
        {0x01360000u, 3u, 310u},
        {0x01360001u, 26u, 310u},
        {0x01360015u, 33u, 310u},
        {0x01360046u, 81u, 310u},
        {0x01360046u, 87u, 310u},
        {0x0136FFFFu, 217u, 310u},
    };
    for (const AllowedPair& item : allowed) {
        const ScalePlan plan = BuildScalePlan(ValidRequest(item.token, item.monsterId));
        Expect(plan.admitted && plan.reason == AdmissionReason::Admitted &&
                   plan.fieldKey == item.expectedFieldKey,
               "every exact field and monster pair in the small S.I.N. catalog must be admitted");
    }

    Expect(BuildScalePlan(ValidRequest(0x01360000u, 4u)).reason ==
                   AdmissionReason::UnsupportedMonster &&
               BuildScalePlan(ValidRequest(0x01540000u, 3u)).reason ==
                   AdmissionReason::UnsupportedMonster,
           "a catalog monster must remain bound to its exact field");
    Expect(BuildScalePlan(ValidRequest(0x02360000u, 3u)).reason ==
                   AdmissionReason::UnsupportedField &&
               BuildScalePlan(ValidRequest(0x02540000u, 4u)).reason ==
                   AdmissionReason::UnsupportedField,
           "full-word field aliases must not pass the mcfr or mcyt admission gate");
    Expect(BuildScalePlan(ValidRequest(0x01360000u, 0x0103u)).reason ==
                   AdmissionReason::UnsupportedMonster &&
               BuildScalePlan(ValidRequest(0x01540000u, 0x0104u)).reason ==
                   AdmissionReason::UnsupportedMonster,
           "low-byte monster aliases must not pass the exact catalog gate");
}

void TestCallerOriginAndFreshnessAdmission() {
    using namespace FfxHooks::SinRam;

    ScaleRequest request = ValidRequest();
    request.encounter.transitionCallerRva = 0x00381D8Cu;
    const ScalePlan callerBefore = BuildScalePlan(request);
    request.encounter.transitionCallerRva = 0x00381D8Du;
    const ScalePlan callerAfter = BuildScalePlan(request);
    Expect(!callerBefore.admitted &&
               callerBefore.reason == AdmissionReason::WrongTransitionCaller &&
               !callerAfter.admitted &&
               callerAfter.reason == AdmissionReason::WrongTransitionCaller,
        "only the verified random-step caller RVA 0x471CEF may admit a scaling request");

    const EncounterOrigin rejectedOrigins[] = {
        EncounterOrigin::Force,
        EncounterOrigin::Arena,
        EncounterOrigin::Ultra,
        EncounterOrigin::CustomMix,
        EncounterOrigin::Unknown,
    };
    for (EncounterOrigin origin : rejectedOrigins) {
        request = ValidRequest();
        request.encounter.origin = origin;
        const ScalePlan plan = BuildScalePlan(request);
        Expect(!plan.admitted && plan.reason == AdmissionReason::NonNaturalOrigin,
               "Force, Arena, Ultra, CustomMix, and unknown origins must fail closed");
    }

    request = ValidRequest();
    request.correlation.transitionRequestId = 0u;
    const ScalePlan zeroRequest = BuildScalePlan(request);
    request = ValidRequest();
    ++request.correlation.actorRequestId;
    const ScalePlan mismatchedRequest = BuildScalePlan(request);
    Expect(!zeroRequest.admitted &&
               zeroRequest.reason == AdmissionReason::RequestMismatch &&
               !mismatchedRequest.admitted &&
               mismatchedRequest.reason == AdmissionReason::RequestMismatch,
           "zero or mismatched request identities must reject delayed actor data");

    request = ValidRequest();
    request.correlation.transitionRequestId = 0u;
    request.correlation.actorRequestId = 0u;
    const ScalePlan bothRequestIdsZero = BuildScalePlan(request);
    Expect(!bothRequestIdsZero.admitted &&
               bothRequestIdsZero.reason == AdmissionReason::RequestMismatch,
           "matching zero request identities must not satisfy request correlation");

    request = ValidRequest();
    request.correlation.transitionGeneration = 0u;
    const ScalePlan zeroGeneration = BuildScalePlan(request);
    request = ValidRequest();
    --request.correlation.actorGeneration;
    const ScalePlan staleActor = BuildScalePlan(request);
    request = ValidRequest();
    ++request.correlation.currentGeneration;
    const ScalePlan staleTransition = BuildScalePlan(request);
    Expect(!zeroGeneration.admitted &&
               zeroGeneration.reason == AdmissionReason::StaleObservation &&
               !staleActor.admitted &&
               staleActor.reason == AdmissionReason::StaleObservation &&
               !staleTransition.admitted &&
               staleTransition.reason == AdmissionReason::StaleObservation,
           "zero or divergent transition, actor, and current generations must reject stale observations");

    request = ValidRequest();
    request.correlation.transitionGeneration = 0u;
    request.correlation.actorGeneration = 0u;
    request.correlation.currentGeneration = 0u;
    const ScalePlan allGenerationsZero = BuildScalePlan(request);
    Expect(!allGenerationsZero.admitted &&
               allGenerationsZero.reason == AdmissionReason::StaleObservation,
           "matching zero generations must not satisfy freshness correlation");
}

void TestOnePostDifficultyHpRatioCalculation() {
    using namespace FfxHooks::SinRam;

    ScaleRequest request = ValidRequest();
    request.config.threatLevel = 0;
    const ScalePlan levelZero = BuildScalePlan(request);
    Expect(levelZero.admitted && levelZero.writeback.maxHp == 75u &&
               levelZero.writeback.currentHp == 16u,
           "T0 must calculate current HP once from the pre-Difficulty ratio and post-Difficulty max");

    request.config.threatLevel = 1;
    const ScalePlan levelOne = BuildScalePlan(request);
    const std::array<std::uint8_t, 8> expectedOne =
        {{11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u}};
    Expect(levelOne.admitted && levelOne.writeback.maxHp == 82u &&
               levelOne.writeback.currentHp == 17u &&
               levelOne.writeback.overkill == 55u &&
               levelOne.writeback.stats == expectedOne,
           "T1 must truncate legacy scaling before the final current-HP ratio calculation");

    request.config.threatLevel = 2;
    const ScalePlan levelTwo = BuildScalePlan(request);
    const std::array<std::uint8_t, 8> expectedTwo =
        {{13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u}};
    Expect(levelTwo.admitted && levelTwo.writeback.maxHp == 90u &&
               levelTwo.writeback.currentHp == 19u &&
               levelTwo.writeback.overkill == 60u &&
               levelTwo.writeback.stats == expectedTwo,
           "T2 stats must truncate their percentage before adding the flat threat bonus");

    request = ValidRequest();
    request.config.threatLevel = 1;
    request.preDifficultyHp.maxHp = 100u;
    request.preDifficultyHp.currentHp = 21u;
    request.afterDifficulty.maxHp = 125u;
    const ScalePlan singleFinalRatio = BuildScalePlan(request);
    Expect(singleFinalRatio.admitted && singleFinalRatio.writeback.maxHp == 137u &&
               singleFinalRatio.writeback.currentHp == 29u,
           "base 100/21, Difficulty 125/26, and T1 must calculate only final HP 137/29, not double-rounded 137/28");
}

void TestSignedDwordAndStatSaturation() {
    using namespace FfxHooks::SinRam;

    ScaleRequest request = ValidRequest();
    request.config.threatLevel = 2;
    request.preDifficultyHp.maxHp = 0x7FFFFFFFu;
    request.preDifficultyHp.currentHp = 0x7FFFFFFFu;
    request.afterDifficulty.maxHp = 0x7FFFFFFFu;
    request.afterDifficulty.overkill = 0x7FFFFFFFu;
    request.afterDifficulty.stats = {{254u, 255u, 254u, 255u, 254u, 255u, 254u, 255u}};
    const ScalePlan saturated = BuildScalePlan(request);
    Expect(saturated.admitted && saturated.writeback.maxHp == 0x7FFFFFFFu &&
               saturated.writeback.currentHp == 0x7FFFFFFFu &&
               saturated.writeback.overkill == 0x7FFFFFFFu,
           "signed dword actor fields must saturate at 0x7FFFFFFF instead of UINT32_MAX");
    bool allStatsClamped = true;
    for (std::uint8_t stat : saturated.writeback.stats) {
        allStatsClamped = allStatsClamped && stat == 255u;
    }
    Expect(allStatsClamped,
           "all eight one-byte stats must saturate at 255 instead of wrapping");

    request = ValidRequest();
    request.config.threatLevel = 1;
    request.preDifficultyHp.maxHp = 5u;
    request.preDifficultyHp.currentHp = 5u;
    request.afterDifficulty.maxHp = 5u;
    request.afterDifficulty.overkill = 5u;
    const ScalePlan halves = BuildScalePlan(request);
    Expect(halves.admitted && halves.writeback.maxHp == 5u &&
               halves.writeback.currentHp == 5u && halves.writeback.overkill == 5u &&
               halves.writeback.stats[0] == 11u,
           "T1 positive legacy casts must truncate value five and stat ten");

    request = ValidRequest();
    request.config.threatLevel = 2;
    request.preDifficultyHp.maxHp = 13u;
    request.preDifficultyHp.currentHp = 13u;
    request.afterDifficulty.maxHp = 13u;
    request.afterDifficulty.overkill = 13u;
    const ScalePlan t2Fraction = BuildScalePlan(request);
    Expect(t2Fraction.admitted && t2Fraction.writeback.maxHp == 15u &&
               t2Fraction.writeback.currentHp == 15u &&
               t2Fraction.writeback.overkill == 15u,
           "T2 positive legacy dword casts must truncate fractional products");

    request = ValidRequest();
    request.preDifficultyHp.maxHp = 0u;
    request.preDifficultyHp.currentHp = 25u;
    const ScalePlan zeroRatio = BuildScalePlan(request);
    Expect(zeroRatio.admitted && zeroRatio.writeback.currentHp == 0u,
           "a zero baseline maximum HP must produce zero current HP without division");

    request = ValidRequest();
    request.preDifficultyHp.currentHp = 150u;
    const ScalePlan cappedRatio = BuildScalePlan(request);
    Expect(cappedRatio.admitted &&
               cappedRatio.writeback.currentHp == cappedRatio.writeback.maxHp,
           "an inconsistent baseline current HP must clamp to the final maximum");
}

}  // namespace

int main() {
    TestDefaultAndInvalidConfigFailClosed();
    TestExactTransitionCatalogAdmission();
    TestCallerOriginAndFreshnessAdmission();
    TestOnePostDifficultyHpRatioCalculation();
    TestSignedDwordAndStatSaturation();

    std::printf("S.I.N. RAM core RT0: %d/%d passed\n", g_passed, g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
