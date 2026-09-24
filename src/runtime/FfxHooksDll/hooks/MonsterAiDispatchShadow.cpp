#include "MonsterAiDispatchShadow.h"

namespace FfxHooks::MonsterAiShadow {
namespace {

DispatchShadowDecision BaseDecision(const DispatchSample& sample) noexcept {
    DispatchShadowDecision decision{};
    decision.observation = sample;
    decision.observedCommand =
        static_cast<uint16_t>(static_cast<uint32_t>(sample.commandStack32) & 0xFFFFu);
    if (sample.actorIndex >= kFirstMonsterActorIndex &&
        sample.actorIndex <= kLastMonsterActorIndex) {
        decision.monsterSlot = sample.actorIndex - kFirstMonsterActorIndex;
    }
    return decision;
}

bool HasExactCandidateFingerprint(const MonsterProgramIdentity& monster) noexcept {
    // The available SHA-1 note is only a prefix, so it is not safe as an equality gate.
    // The two exact length/FNV pairs keep this candidate version-specific and fail closed.
    return monster.aiLength == kCandidateAiLength &&
           monster.aiFnv1a64 == kCandidateAiFnv1a64 &&
           monster.workerLength == kCandidateWorkerLength &&
           monster.workerFnv1a64 == kCandidateWorkerFnv1a64;
}

}  // namespace

DispatchShadowDecision EvaluateDispatch(const DispatchSample& sample) noexcept {
    DispatchShadowDecision decision = BaseDecision(sample);

    if (sample.mode != ShadowMode::ObserveOnly) {
        decision.reason = DecisionReason::ShadowDisabled;
        return decision;
    }
    if (sample.callerReturnPreferredVa == kForceDispatchReturnPreferredVa) {
        decision.reason = DecisionReason::ForceDispatchCaller;
        return decision;
    }
    if (sample.callerReturnPreferredVa == kDeathOverrideReturnPreferredVa) {
        decision.reason = DecisionReason::DeathOverrideCaller;
        return decision;
    }
    if (sample.callerReturnPreferredVa != kNormalPerformReturnPreferredVa) {
        decision.reason = DecisionReason::UnexpectedCaller;
        return decision;
    }
    if (sample.force != 0) {
        // A nonzero force argument is treated as forced even if a future caller reaches
        // the normal return site; conservative rejection prevents caller aliasing.
        decision.reason = DecisionReason::ForceArgumentSet;
        return decision;
    }
    if (decision.monsterSlot < 0) {
        decision.reason = DecisionReason::ActorNotMonster;
        return decision;
    }
    if (sample.monster.rawId != kCandidateMonsterRawId) {
        decision.reason = DecisionReason::MonsterNotCandidate;
        return decision;
    }
    if (!HasExactCandidateFingerprint(sample.monster)) {
        decision.reason = DecisionReason::CandidateFingerprintMismatch;
        return decision;
    }
    if (decision.observedCommand != kObservedCandidateCommand) {
        decision.reason = DecisionReason::CommandNotCandidate;
        return decision;
    }

    // This is telemetry only. Keeping the proposal separate from the observation prevents
    // the decision layer from silently rewriting commandStack32 or the resolved target mask.
    decision.reason = DecisionReason::CandidateProposalRecorded;
    decision.proposalAvailable = true;
    decision.proposedCommand = kProposedCandidateCommand;
    return decision;
}

const char* DecisionReasonText(DecisionReason reason) noexcept {
    switch (reason) {
        case DecisionReason::ShadowDisabled:
            return "shadow_disabled";
        case DecisionReason::UnexpectedCaller:
            return "unexpected_caller";
        case DecisionReason::ForceDispatchCaller:
            return "force_dispatch_caller";
        case DecisionReason::DeathOverrideCaller:
            return "death_override_caller";
        case DecisionReason::ForceArgumentSet:
            return "force_argument_set";
        case DecisionReason::ActorNotMonster:
            return "actor_not_monster";
        case DecisionReason::MonsterNotCandidate:
            return "monster_not_candidate";
        case DecisionReason::CandidateFingerprintMismatch:
            return "candidate_fingerprint_mismatch";
        case DecisionReason::CommandNotCandidate:
            return "command_not_candidate";
        case DecisionReason::CandidateProposalRecorded:
            return "candidate_proposal_recorded";
    }
    return "unknown";
}

}  // namespace FfxHooks::MonsterAiShadow
