#pragma once

#include <cstdint>

namespace FfxHooks::MonsterAiShadow {

// These preferred-image addresses describe the supported FFX.exe evidence. A future
// ASLR-aware adapter must normalize the live return address before calling this core.
constexpr uint32_t kPreferredImageBase = 0x00400000u;
constexpr uint32_t kDispatcherPreferredVa = 0x007AC9E0u;
constexpr uint32_t kDispatcherRva = 0x003AC9E0u;
// Read-only IDA disassembly confirms each five-byte E8 call and the immediately
// following return address. Keeping both sides makes an off-by-five hook filter visible.
constexpr uint32_t kNormalPerformCallPreferredVa = 0x007A4549u;
constexpr uint32_t kNormalPerformReturnPreferredVa = 0x007A454Eu;
constexpr uint32_t kForceDispatchCallPreferredVa = 0x007A4A5Bu;
constexpr uint32_t kForceDispatchReturnPreferredVa = 0x007A4A60u;
constexpr uint32_t kDeathOverrideCallPreferredVa = 0x007A4B87u;
constexpr uint32_t kDeathOverrideReturnPreferredVa = 0x007A4B8Cu;

constexpr int32_t kFirstMonsterActorIndex = 20;
constexpr int32_t kLastMonsterActorIndex = 27;

constexpr uint16_t kCandidateMonsterRawId = 0x0156u;  // Catalog monster m342.
constexpr uint16_t kObservedCandidateCommand = 0x4100u;
constexpr uint16_t kProposedCandidateCommand = 0x4127u;
constexpr uint32_t kCandidateAiLength = 0x0A70u;
constexpr uint64_t kCandidateAiFnv1a64 = 0x09B060CEF9722A9Bull;
constexpr uint32_t kCandidateWorkerLength = 0x0164u;
constexpr uint64_t kCandidateWorkerFnv1a64 = 0x562E0807022CD551ull;

// There is deliberately no mutation-capable mode. OFF remains the zero value and the
// only enabled state can observe and report a proposal for later RT2 evidence collection.
enum class ShadowMode : uint8_t {
    Disabled = 0,
    ObserveOnly = 1,
};

struct MonsterProgramIdentity {
    uint16_t rawId = 0;
    uint32_t aiLength = 0;
    uint64_t aiFnv1a64 = 0;
    uint32_t workerLength = 0;
    uint64_t workerFnv1a64 = 0;
};

// This value-only sample mirrors the dispatcher ABI without exposing any game-memory
// pointer. That boundary makes actor-field writes impossible inside the decision core.
struct DispatchSample {
    ShadowMode mode = ShadowMode::Disabled;
    uint32_t callerReturnPreferredVa = 0;
    int32_t actorIndex = 0;
    int32_t commandStack32 = 0;
    uint32_t resolvedTargetMask = 0;
    int32_t force = 0;
    int32_t n64 = 0;
    MonsterProgramIdentity monster{};
};

enum class DecisionReason : uint8_t {
    ShadowDisabled = 0,
    UnexpectedCaller,
    ForceDispatchCaller,
    DeathOverrideCaller,
    ForceArgumentSet,
    ActorNotMonster,
    MonsterNotCandidate,
    CandidateFingerprintMismatch,
    CommandNotCandidate,
    CandidateProposalRecorded,
};

struct DispatchShadowDecision {
    DecisionReason reason = DecisionReason::ShadowDisabled;
    DispatchSample observation{};
    uint16_t observedCommand = 0;
    int32_t monsterSlot = -1;
    bool proposalAvailable = false;
    uint16_t proposedCommand = 0;
};

// The future seam is FFX_Battle_DispatchActionCommand at preferred VA 0x7AC9E0
// (RVA 0x3AC9E0), with ABI:
//   int __cdecl(int actorIndex, int commandStack32, uint32_t targetMask,
//               int force, int n64)
// Only commandStack32's low u16 is consumed by the game. This pure function records a
// possible alternative but intentionally returns no "effective" or replacement command;
// a runtime observer must forward the original dispatcher arguments unchanged.
DispatchShadowDecision EvaluateDispatch(const DispatchSample& sample) noexcept;

const char* DecisionReasonText(DecisionReason reason) noexcept;

}  // namespace FfxHooks::MonsterAiShadow
