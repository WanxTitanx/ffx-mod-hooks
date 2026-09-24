#include "../hooks/MonsterAiDispatchShadow.h"

#include <cstdio>
#include <cstring>

namespace {

using FfxHooks::MonsterAiShadow::DecisionReason;
using FfxHooks::MonsterAiShadow::DispatchSample;
using FfxHooks::MonsterAiShadow::DispatchShadowDecision;
using FfxHooks::MonsterAiShadow::ShadowMode;

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

DispatchSample MakeRecognizedCandidate() {
    DispatchSample sample{};
    sample.mode = ShadowMode::ObserveOnly;
    sample.callerReturnPreferredVa = 0x007A454Eu;
    sample.actorIndex = 20;
    sample.commandStack32 = static_cast<int32_t>(0xBEEF4100u);
    sample.resolvedTargetMask = 0xA5A55AA5u;
    sample.force = 0;
    sample.n64 = 73;
    sample.monster.rawId = 0x0156u;
    sample.monster.aiLength = 0x0A70u;
    sample.monster.aiFnv1a64 = 0x09B060CEF9722A9Bull;
    sample.monster.workerLength = 0x0164u;
    sample.monster.workerFnv1a64 = 0x562E0807022CD551ull;
    return sample;
}

void TestDispatcherAddressEvidence() {
    using namespace FfxHooks::MonsterAiShadow;
    Expect(kPreferredImageBase == 0x00400000u &&
               kDispatcherPreferredVa == 0x007AC9E0u &&
               kDispatcherRva == 0x003AC9E0u &&
               kPreferredImageBase + kDispatcherRva == kDispatcherPreferredVa,
           "the dispatcher preferred VA and RVA must retain the supported image-base relation");
    Expect(kNormalPerformCallPreferredVa == 0x007A4549u &&
               kNormalPerformReturnPreferredVa == 0x007A454Eu &&
               kNormalPerformReturnPreferredVa - kNormalPerformCallPreferredVa == 5u,
           "the normal-perform E8 call and following return address must remain an exact pair");
    Expect(kForceDispatchCallPreferredVa == 0x007A4A5Bu &&
               kForceDispatchReturnPreferredVa == 0x007A4A60u &&
               kForceDispatchReturnPreferredVa - kForceDispatchCallPreferredVa == 5u,
           "the force-dispatch E8 call and following return address must remain an exact pair");
    Expect(kDeathOverrideCallPreferredVa == 0x007A4B87u &&
               kDeathOverrideReturnPreferredVa == 0x007A4B8Cu &&
               kDeathOverrideReturnPreferredVa - kDeathOverrideCallPreferredVa == 5u,
           "the death-override E8 call and following return address must remain an exact pair");
}

void ExpectRejected(const DispatchShadowDecision& decision,
                    DecisionReason reason,
                    const char* message) {
    Expect(decision.reason == reason && !decision.proposalAvailable &&
               decision.proposedCommand == 0,
           message);
}

void TestDefaultAndInvalidModesFailClosed() {
    DispatchSample disabled = MakeRecognizedCandidate();
    disabled.mode = ShadowMode::Disabled;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(disabled),
                   DecisionReason::ShadowDisabled,
                   "the default disabled shadow mode must never emit a proposal");

    DispatchSample invalid = MakeRecognizedCandidate();
    invalid.mode = static_cast<ShadowMode>(0xFFu);
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(invalid),
                   DecisionReason::ShadowDisabled,
                   "an unknown shadow mode must fail closed as disabled");
}

void TestRecognizedNormalDispatchProducesProposalOnly() {
    const DispatchSample sample = MakeRecognizedCandidate();
    const DispatchShadowDecision decision =
        FfxHooks::MonsterAiShadow::EvaluateDispatch(sample);

    Expect(decision.reason == DecisionReason::CandidateProposalRecorded &&
               decision.proposalAvailable && decision.proposedCommand == 0x4127u,
           "the exact m342 normal-perform observation must record the sole proposal");
    Expect(decision.observedCommand == 0x4100u &&
               static_cast<uint32_t>(decision.observation.commandStack32) == 0xBEEF4100u,
           "the dispatcher consumes only the command's low u16 while telemetry keeps the full stack value");
    Expect(decision.observation.resolvedTargetMask == 0xA5A55AA5u &&
               decision.observation.actorIndex == 20 && decision.monsterSlot == 0,
           "a proposal must preserve the already-resolved target mask and identify monster slot zero");
    Expect(decision.observation.force == 0 && decision.observation.n64 == 73 &&
               decision.observation.monster.rawId == 0x0156u,
           "proposal telemetry must retain the observed ABI arguments and monster identity");

    DispatchSample finalSlot = MakeRecognizedCandidate();
    finalSlot.actorIndex = 27;
    finalSlot.resolvedTargetMask = 0xFFFFFFFFu;
    const DispatchShadowDecision finalDecision =
        FfxHooks::MonsterAiShadow::EvaluateDispatch(finalSlot);
    Expect(finalDecision.proposalAvailable && finalDecision.monsterSlot == 7 &&
               finalDecision.observation.resolvedTargetMask == 0xFFFFFFFFu,
           "actor 27 must remain the final admitted monster slot without altering its target mask");

    DispatchSample emptyMask = MakeRecognizedCandidate();
    emptyMask.resolvedTargetMask = 0;
    const DispatchShadowDecision emptyDecision =
        FfxHooks::MonsterAiShadow::EvaluateDispatch(emptyMask);
    Expect(emptyDecision.proposalAvailable && emptyDecision.observation.resolvedTargetMask == 0,
           "an empty resolved target mask must be observed verbatim rather than synthesized");
}

void TestOnlyNormalPerformReturnIsAdmitted() {
    DispatchSample sample = MakeRecognizedCandidate();
    sample.callerReturnPreferredVa = 0x007A4A60u;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::ForceDispatchCaller,
                   "the force-dispatch return address must be classified and rejected");

    sample = MakeRecognizedCandidate();
    sample.callerReturnPreferredVa = 0x007A4B8Cu;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::DeathOverrideCaller,
                   "the death-override return address must be classified and rejected");

    sample = MakeRecognizedCandidate();
    sample.callerReturnPreferredVa = 0x007A4549u;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::UnexpectedCaller,
                   "the normal call instruction address is not the accepted post-call return address");

    sample = MakeRecognizedCandidate();
    sample.callerReturnPreferredVa = 0x007A4A5Bu;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::UnexpectedCaller,
                   "the force call instruction address must not alias its classified post-call return address");

    sample = MakeRecognizedCandidate();
    sample.callerReturnPreferredVa = 0x007A4B87u;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::UnexpectedCaller,
                   "the death-override call instruction address must not alias its classified post-call return address");

    sample = MakeRecognizedCandidate();
    sample.callerReturnPreferredVa = 0x007A454Fu;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::UnexpectedCaller,
                   "a neighboring return address must not alias the exact normal-perform caller");
}

void TestForceArgumentAndActorBoundsFailClosed() {
    DispatchSample sample = MakeRecognizedCandidate();
    sample.force = 1;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::ForceArgumentSet,
                   "a forced dispatch argument must be rejected even at the normal return address");

    sample = MakeRecognizedCandidate();
    sample.force = -1;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::ForceArgumentSet,
                   "a negative nonzero force argument must fail closed like every forced dispatch");

    sample = MakeRecognizedCandidate();
    sample.actorIndex = 19;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::ActorNotMonster,
                   "actor 19 must remain outside the monster slot range");

    sample = MakeRecognizedCandidate();
    sample.actorIndex = 28;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::ActorNotMonster,
                   "actor 28 must remain outside the monster slot range");
}

void TestEveryMonsterActorMapsToItsExactSlot() {
    for (int32_t actor = 20; actor <= 27; ++actor) {
        DispatchSample sample = MakeRecognizedCandidate();
        sample.actorIndex = actor;
        const DispatchShadowDecision decision =
            FfxHooks::MonsterAiShadow::EvaluateDispatch(sample);
        Expect(decision.proposalAvailable && decision.monsterSlot == actor - 20,
               "every admitted monster actor must map monotonically to slots zero through seven");
    }
}

void TestCandidateIdentityIsExact() {
    DispatchSample sample = MakeRecognizedCandidate();
    sample.monster.rawId = 0x0155u;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::MonsterNotCandidate,
                   "a different raw monster id must not inherit the m342 proposal");

    sample = MakeRecognizedCandidate();
    sample.monster.aiLength = 0x0A71u;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::CandidateFingerprintMismatch,
                   "a different AI payload length must reject the candidate");

    sample = MakeRecognizedCandidate();
    sample.monster.aiFnv1a64 ^= 1ull;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::CandidateFingerprintMismatch,
                   "a different AI payload fingerprint must reject the candidate");

    sample = MakeRecognizedCandidate();
    sample.monster.workerLength = 0x0165u;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::CandidateFingerprintMismatch,
                   "a different worker payload length must reject the candidate");

    sample = MakeRecognizedCandidate();
    sample.monster.workerFnv1a64 ^= 1ull;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::CandidateFingerprintMismatch,
                   "a different worker payload fingerprint must reject the candidate");
}

void TestCommandPolicyUsesOnlyTheLowWord() {
    DispatchSample sample = MakeRecognizedCandidate();
    sample.commandStack32 = static_cast<int32_t>(0x12344100u);
    const DispatchShadowDecision highBits =
        FfxHooks::MonsterAiShadow::EvaluateDispatch(sample);
    Expect(highBits.proposalAvailable && highBits.observedCommand == 0x4100u,
           "unconsumed high command bits must not change the low-u16 dispatcher decision");

    sample = MakeRecognizedCandidate();
    sample.commandStack32 = 0x4127;
    ExpectRejected(FfxHooks::MonsterAiShadow::EvaluateDispatch(sample),
                   DecisionReason::CommandNotCandidate,
                   "an already different low-u16 action must not receive the 0x4100 proposal");
}

void TestEveryDecisionReasonHasTelemetryText() {
    const DecisionReason reasons[] = {
        DecisionReason::ShadowDisabled,
        DecisionReason::UnexpectedCaller,
        DecisionReason::ForceDispatchCaller,
        DecisionReason::DeathOverrideCaller,
        DecisionReason::ForceArgumentSet,
        DecisionReason::ActorNotMonster,
        DecisionReason::MonsterNotCandidate,
        DecisionReason::CandidateFingerprintMismatch,
        DecisionReason::CommandNotCandidate,
        DecisionReason::CandidateProposalRecorded,
    };
    for (DecisionReason reason : reasons) {
        const char* text = FfxHooks::MonsterAiShadow::DecisionReasonText(reason);
        Expect(text != nullptr && text[0] != '\0',
               "every reachable decision reason must have a non-empty telemetry label");
    }
    Expect(std::strcmp(FfxHooks::MonsterAiShadow::DecisionReasonText(
                           static_cast<DecisionReason>(0xFFu)),
                       "unknown") == 0,
           "an unknown decision reason must remain safely printable");
}

}  // namespace

int main() {
    TestDispatcherAddressEvidence();
    TestDefaultAndInvalidModesFailClosed();
    TestRecognizedNormalDispatchProducesProposalOnly();
    TestOnlyNormalPerformReturnIsAdmitted();
    TestForceArgumentAndActorBoundsFailClosed();
    TestEveryMonsterActorMapsToItsExactSlot();
    TestCandidateIdentityIsExact();
    TestCommandPolicyUsesOnlyTheLowWord();
    TestEveryDecisionReasonHasTelemetryText();
    std::printf("Monster AI dispatch shadow RT0: %d/%d passed\n",
                g_passed,
                g_passed + g_failed);
    return g_failed == 0 ? 0 : 1;
}
