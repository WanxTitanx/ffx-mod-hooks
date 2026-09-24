#include "F8RuntimeCore.h"

#include "../shared/ffx_addresses.h"

#include <limits>

namespace FfxHooks::F8Runtime {
namespace {

// Offline PE evidence for the supported 32-bit FFX.exe snapshot; profile selection is exact.
constexpr uint16_t kSupportedMachine = 0x014C;
constexpr uint16_t kSupportedOptionalMagic = 0x010B;
constexpr uint32_t kSupportedTimestamp = 0x55D2F3CC;
constexpr uint32_t kSupportedSizeOfImage = 0x0237D000;
constexpr size_t kMinimumDosHeaderSize = 0x40;
constexpr size_t kDosPeOffsetField = 0x3C;
constexpr size_t kCoffHeaderSize = 20;
constexpr size_t kPeSignatureSize = 4;
constexpr size_t kOptionalSizeOfImageOffset = 56;
constexpr size_t kMinimumOptionalHeaderSize = kOptionalSizeOfImageOffset + sizeof(uint32_t);
constexpr uint32_t kPresentPhysicalMask = 0x03u;
constexpr uint32_t kPresentTerminalRequested = 0x04u;
constexpr uint32_t kPresentTerminalPublished = 0x08u;
constexpr std::array<uint8_t, 4> kSeymourSignature1 = {0x3Cu, 0x07u, 0x74u, 0x24u};
constexpr std::array<uint8_t, 4> kSeymourSignature2 = {0x3Cu, 0x07u, 0x74u, 0x1Eu};
constexpr std::array<uint8_t, 4> kSeymourNops = {0x90u, 0x90u, 0x90u, 0x90u};
constexpr RewardHookSpec kApRewardSpec = {
    RewardKind::Ap,
    RVA_FFX_AP_MULTIPLIER_SIGNATURE,
    RVA_FFX_AP_MULTIPLIER_SITE,
    RVA_FFX_AP_MULTIPLIER_IMMEDIATE,
    RVA_FFX_AP_MULTIPLIER_RESUME,
    0xFCu,
    {0x74u, 0x06u, 0x6Bu, 0xC0u, 0x64u, 0x89u, 0x45u, 0xFCu},
};
constexpr RewardHookSpec kGilRewardSpec = {
    RewardKind::Gil,
    RVA_FFX_GIL_MULTIPLIER_SIGNATURE,
    RVA_FFX_GIL_MULTIPLIER_SITE,
    RVA_FFX_GIL_MULTIPLIER_IMMEDIATE,
    RVA_FFX_GIL_MULTIPLIER_RESUME,
    0xF8u,
    {0x74u, 0x06u, 0x6Bu, 0xC0u, 0x64u, 0x89u, 0x45u, 0xF8u},
};

bool EncodeRel32(uintptr_t nextInstruction, uintptr_t target, int32_t* displacementOut) {
    if (!displacementOut) return false;
    if (target >= nextInstruction) {
        const uintptr_t distance = target - nextInstruction;
        if (distance > static_cast<uintptr_t>((std::numeric_limits<int32_t>::max)())) return false;
        *displacementOut = static_cast<int32_t>(distance);
        return true;
    }
    const uintptr_t distance = nextInstruction - target;
    constexpr uint64_t kNegativeLimit = static_cast<uint64_t>((std::numeric_limits<int32_t>::max)()) + 1u;
    if (static_cast<uint64_t>(distance) > kNegativeLimit) return false;
    *displacementOut = distance == kNegativeLimit
        ? (std::numeric_limits<int32_t>::min)()
        : -static_cast<int32_t>(distance);
    return true;
}

void WriteU32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

bool HasBytes(size_t offset, size_t length, size_t available) {
    return offset <= available && length <= available - offset;
}

uint16_t ReadU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t ReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void ClearOwnership(OwnedByte* owned) {
    owned->address = 0;
    owned->original = 0;
    owned->desired = 0;
    owned->priorDesired = 0;
    owned->captured = false;
    owned->hasPriorDesired = false;
    owned->state = OwnershipState::Unowned;
}

void ClearPriorDesired(OwnedByte* owned) {
    owned->priorDesired = 0;
    owned->hasPriorDesired = false;
}

bool IsOwnedCandidate(const OwnedByte& owned, uint8_t value) {
    return value == owned.desired ||
           (owned.hasPriorDesired && value == owned.priorDesired);
}

bool IsEligible(uint8_t inParty) {
    // The recovered seven-slot contract uses 0x10 as an explicit non-party sentinel.
    return inParty != 0 && inParty != 0x10;
}

PresentHookPhysicalState PresentPhysicalState(uint32_t word) {
    return static_cast<PresentHookPhysicalState>(word & kPresentPhysicalMask);
}

bool ValidPatchIo(const PatchIo& io) {
    return io.context && io.read && io.beginCodeWrite && io.writeIfEqual && io.flush &&
           io.endCodeWrite;
}

bool CheckedAddAddress(uintptr_t base, uintptr_t offset, uintptr_t* resultOut) {
    if (!resultOut || base > (std::numeric_limits<uintptr_t>::max)() - offset) {
        return false;
    }
    *resultOut = base + offset;
    return true;
}

bool ValidSeymourLayout(const SeymourBundleState& state) {
    if (state.sites[0].address == 0 || state.sites[1].address == 0 ||
        state.party.address == 0 || state.party.mask != FFX_PARTY_SEYMOUR_MASK) {
        return false;
    }
    constexpr uintptr_t kSite1Rva =
        static_cast<uintptr_t>(RVA_FFX_SEYMOUR_PATCH_SITE1);
    constexpr uintptr_t kSite2Rva =
        static_cast<uintptr_t>(RVA_FFX_SEYMOUR_PATCH_SITE2);
    constexpr uintptr_t kPartyRva =
        static_cast<uintptr_t>(RVA_FFX_PARTY_SEYMOUR_IN_PARTY);
    constexpr uintptr_t kPageRva =
        static_cast<uintptr_t>(RVA_FFX_SEYMOUR_PATCH_PAGE);
    if (state.sites[0].address < kSite1Rva) return false;
    const uintptr_t imageBase = state.sites[0].address - kSite1Rva;
    if ((imageBase & static_cast<uintptr_t>(0xFFFu)) != 0) return false;

    uintptr_t expectedSite1 = 0;
    uintptr_t expectedSite2 = 0;
    uintptr_t expectedParty = 0;
    uintptr_t expectedPage = 0;
    if (!CheckedAddAddress(imageBase, kSite1Rva, &expectedSite1) ||
        !CheckedAddAddress(imageBase, kSite2Rva, &expectedSite2) ||
        !CheckedAddAddress(imageBase, kPartyRva, &expectedParty) ||
        !CheckedAddAddress(imageBase, kPageRva, &expectedPage)) {
        return false;
    }
    constexpr uintptr_t kPageMask = ~static_cast<uintptr_t>(0xFFFu);
    constexpr uintptr_t kSpanLastOffset =
        static_cast<uintptr_t>(FFX_SEYMOUR_PATCH_SPAN_LENGTH - 1u);
    if (state.sites[0].address >
        (std::numeric_limits<uintptr_t>::max)() - kSpanLastOffset) {
        return false;
    }
    const uintptr_t spanEnd = state.sites[0].address + kSpanLastOffset;
    return state.sites[0].address == expectedSite1 &&
           state.sites[1].address == expectedSite2 &&
           state.party.address == expectedParty &&
           (state.sites[0].address & kPageMask) == expectedPage &&
           (state.sites[1].address & kPageMask) == expectedPage &&
           (state.party.address < state.sites[0].address || state.party.address > spanEnd);
}

bool BytesEqual(const std::array<uint8_t, 4>& left,
                const std::array<uint8_t, 4>& right) {
    return left == right;
}

bool ReadPatchSite(const PatchIo& io, uintptr_t address,
                   std::array<uint8_t, 4>* bytesOut) {
    return bytesOut && io.read(io.context, address, bytesOut->data(), bytesOut->size());
}

MutationEffect ConservativeMutationEffect(const MutationReport& report) {
    if (report.effect == MutationEffect::NotTouched) return MutationEffect::NotTouched;
    // Only the internally observed bytes decide success. A missing callback readback claim
    // downgrades Verified to MayHaveChanged; an optimistic claim never bypasses our read.
    return report.effect == MutationEffect::Verified && report.readbackVerified
        ? MutationEffect::Verified
        : MutationEffect::MayHaveChanged;
}

bool EndSeymourProtection(const PatchIo& io, SeymourBundleState* state) {
    if (!state || !state->protection.active) return true;
    const PatchProtectionToken retained = state->protection;
    if (retained.address != state->sites[0].address ||
        retained.length != FFX_SEYMOUR_PATCH_SPAN_LENGTH) {
        return false;
    }
    PatchProtectionToken endedToken = retained;
    const bool ended = io.endCodeWrite(io.context, &endedToken);
    if (!ended || endedToken.active || endedToken.address != retained.address ||
        endedToken.length != retained.length ||
        endedToken.originalProtection != retained.originalProtection) {
        state->protection = retained;
        return false;
    }
    state->protection = endedToken;
    return true;
}

enum class ResourceObservation : uint8_t { Unreadable = 0, Original, Candidate, Third };

ResourceObservation ObservePatchSite(const PatchIo& io, const PatchSiteState& site) {
    std::array<uint8_t, 4> current{};
    if (!ReadPatchSite(io, site.address, &current)) return ResourceObservation::Unreadable;
    if (BytesEqual(current, site.original)) return ResourceObservation::Original;
    if (BytesEqual(current, kSeymourNops)) return ResourceObservation::Candidate;
    return ResourceObservation::Third;
}

ResourceObservation ObserveParty(const PatchIo& io, const MaskedByteState& party,
                                 uint8_t* currentOut) {
    uint8_t current = 0;
    if (!io.read(io.context, party.address, &current, 1)) {
        return ResourceObservation::Unreadable;
    }
    if (currentOut) *currentOut = current;
    const uint8_t controlled = static_cast<uint8_t>(current & party.mask);
    if (controlled == party.originalMaskedBits) return ResourceObservation::Original;
    if (controlled == party.mask) return ResourceObservation::Candidate;
    return ResourceObservation::Third;
}

bool HasConflictWitness(const SeymourBundleState& state) {
    return state.sites[0].conflictWitness || state.sites[1].conflictWitness ||
           state.party.conflictWitness;
}

bool HasRestoreProvenance(ResourceRestorePhase phase) {
    return phase != ResourceRestorePhase::None;
}

bool PatchSiteRelevant(const PatchSiteState& site) {
    return site.possiblyOwned || site.conflictWitness ||
           HasRestoreProvenance(site.restorePhase);
}

bool PartyRelevant(const MaskedByteState& party) {
    return party.possiblyOwned || party.conflictWitness ||
           HasRestoreProvenance(party.restorePhase);
}

void ConfirmPatchSiteOriginal(PatchSiteState* site) {
    if (!site) return;
    site->restorePhase = ResourceRestorePhase::OriginalConfirmed;
    site->conflictWitness = false;
}

void ConfirmPartyOriginal(MaskedByteState* party) {
    if (!party) return;
    party->restorePhase = ResourceRestorePhase::OriginalConfirmed;
    party->conflictWitness = false;
}

void RecordPotentialRestoreMutation(
    MutationEffect effect, ResourceRestorePhase* phase) {
    if (phase && effect != MutationEffect::NotTouched) {
        *phase = ResourceRestorePhase::MutationReadbackUnknown;
    }
}

bool HasSeymourObligation(const SeymourBundleState& state) {
    return PatchSiteRelevant(state.sites[0]) || PatchSiteRelevant(state.sites[1]) ||
           PartyRelevant(state.party) || state.protection.active;
}

bool ValidActiveProtection(const SeymourBundleState& state) {
    return state.protection.active &&
           state.protection.address == state.sites[0].address &&
           state.protection.length == FFX_SEYMOUR_PATCH_SPAN_LENGTH;
}

void ClearSeymourOwnership(SeymourBundleState* state) {
    if (!state) return;
    for (PatchSiteState& site : state->sites) {
        site.possiblyOwned = false;
        site.conflictWitness = false;
        site.restorePhase = ResourceRestorePhase::None;
    }
    state->party.possiblyOwned = false;
    state->party.conflictWitness = false;
    state->party.restorePhase = ResourceRestorePhase::None;
    state->protection = {};
}

BundleResult FinishFailedSeymourApply(
    const PatchIo& io, SeymourBundleState* state, bool conflict) {
    if (!state) return BundleResult::PreflightFailed;
    state->state = conflict ? BundleState::Conflict : BundleState::RestorePending;
    const BundleResult rollback = RestoreSeymourBundle(io, state);
    if (rollback == BundleResult::Restored || rollback == BundleResult::AlreadyInState) {
        return BundleResult::ApplyFailedRolledBack;
    }
    if (rollback == BundleResult::RestorePending) return BundleResult::RestorePending;
    return rollback == BundleResult::Conflict
        ? BundleResult::Conflict
        : BundleResult::RestorePending;
}

} // namespace

const RewardHookSpec& RewardHookSpecFor(RewardKind kind) {
    return kind == RewardKind::Gil ? kGilRewardSpec : kApRewardSpec;
}

bool ValidateRewardSignature(const RewardHookSpec& spec, const uint8_t* bytes, size_t length) {
    if (!bytes || length != spec.signature.size()) return false;
    for (size_t index = 0; index < spec.signature.size(); ++index) {
        if (bytes[index] != spec.signature[index]) return false;
    }
    // These relations prove the decoded JZ, signed-imm8 IMUL, EBP store, and exact resume shape.
    return spec.siteRva == spec.signatureRva + 2u &&
           spec.immediateRva == spec.signatureRva + 4u &&
           spec.resumeRva == spec.siteRva + kRewardPatchSize &&
           spec.signature[0] == 0x74u && spec.signature[1] == 0x06u &&
           spec.signature[2] == 0x6Bu && spec.signature[3] == 0xC0u &&
           spec.signature[4] == 0x64u && spec.signature[5] == 0x89u &&
           spec.signature[6] == 0x45u && spec.signature[7] == spec.storeDisplacement;
}

bool EncodeRewardStub(const RewardHookSpec& spec, uintptr_t stubAddress,
                      uintptr_t scalarAddress, uintptr_t resumeAddress,
                      std::array<uint8_t, kRewardStubSize>* out) {
    if (!out || !stubAddress || !scalarAddress || !resumeAddress ||
        scalarAddress > (std::numeric_limits<uint32_t>::max)() ||
        stubAddress > (std::numeric_limits<uintptr_t>::max)() - kRewardStubSize) {
        return false;
    }
    int32_t resumeDisplacement = 0;
    if (!EncodeRel32(stubAddress + kRewardStubSize, resumeAddress, &resumeDisplacement)) {
        return false;
    }

    *out = {0x0Fu, 0xAFu, 0x05u, 0u, 0u, 0u, 0u,
            0x89u, 0x45u, spec.storeDisplacement,
            0xE9u, 0u, 0u, 0u, 0u};
    WriteU32(out->data() + 3, static_cast<uint32_t>(scalarAddress));
    WriteU32(out->data() + 11, static_cast<uint32_t>(resumeDisplacement));
    return true;
}

bool EncodeRewardSiteJump(uintptr_t siteAddress, uintptr_t stubAddress,
                          std::array<uint8_t, kRewardPatchSize>* out) {
    if (!out || !siteAddress || !stubAddress ||
        siteAddress > (std::numeric_limits<uintptr_t>::max)() - 5u) {
        return false;
    }
    int32_t displacement = 0;
    if (!EncodeRel32(siteAddress + 5u, stubAddress, &displacement)) return false;
    *out = {0xE9u, 0u, 0u, 0u, 0u, 0x90u};
    WriteU32(out->data() + 1, static_cast<uint32_t>(displacement));
    return true;
}

bool IsRewardMultiplierInRange(int value) {
    return value >= 1 && value <= 100;
}

bool IsRewardWorstCaseSafe(RewardKind kind, int value) {
    if (!IsRewardMultiplierInRange(value)) return false;
    constexpr int64_t kRewardInputMax = 65535;
    constexpr int64_t kAccumulatorCap = 999999999;
    const int64_t downstreamFactor = kind == RewardKind::Ap ? 3 : 2;
    const int64_t worst = kRewardInputMax * value * downstreamFactor + kAccumulatorCap;
    return worst <= (std::numeric_limits<int32_t>::max)();
}

bool PrepareRewardHookState(RewardKind kind, uintptr_t moduleBase,
                            uintptr_t scalarAddress, RewardHookState* state) {
    if (!state || !moduleBase || !scalarAddress || (scalarAddress & 3u) != 0u ||
        scalarAddress > (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }
    const RewardHookSpec& spec = RewardHookSpecFor(kind);
    if (ValidateImageRange(spec.signatureRva, spec.signature.size(), kSupportedSizeOfImage) !=
            ProfileResult::Supported ||
        ValidateImageRange(spec.siteRva, kRewardPatchSize, kSupportedSizeOfImage) !=
            ProfileResult::Supported ||
        ValidateImageRange(spec.resumeRva, 0, kSupportedSizeOfImage) !=
            ProfileResult::Supported) {
        return false;
    }

    RewardHookState prepared{};
    prepared.kind = kind;
    prepared.moduleBase = moduleBase;
    prepared.scalarAddress = scalarAddress;
    if (!CheckedAddAddress(moduleBase, spec.signatureRva, &prepared.signatureAddress) ||
        !CheckedAddAddress(moduleBase, spec.siteRva, &prepared.siteAddress) ||
        !CheckedAddAddress(moduleBase, spec.resumeRva, &prepared.resumeAddress)) {
        return false;
    }
    for (size_t index = 0; index < kRewardPatchSize; ++index) {
        prepared.original[index] = spec.signature[index + 2u];
    }
    *state = prepared;
    return true;
}

RewardHookResult InstallRewardHook(const RewardHookIo& io, bool battleActive,
                                   RewardHookState* state) {
    if (!state || !io.context || !io.read || !io.allocateWritable ||
        !io.writeWritable || !io.protectExecuteRead || !io.confirmBattleInactive ||
        !io.beginCodeWrite ||
        !io.writeIfEqual || !io.flush || !io.endCodeWrite || !io.freeAllocation ||
        !state->moduleBase || !state->signatureAddress || !state->siteAddress ||
        !state->resumeAddress || !state->scalarAddress) {
        return RewardHookResult::Failed;
    }
    if (battleActive) return RewardHookResult::DeferredBattleActive;
    if (state->state == RewardHookOwnership::Installed) {
        return RewardHookResult::AlreadyInstalled;
    }
    if (state->state == RewardHookOwnership::Conflict) return RewardHookResult::Conflict;
    if (state->state == RewardHookOwnership::RestorePending ||
        state->state == RewardHookOwnership::StubCleanupPending) {
        return RewardHookResult::RestorePending;
    }
    if (state->stubOwned &&
        (state->state != RewardHookOwnership::StubReady || !state->stubVerifiedRx)) {
        // An allocation retained after a failed encode/emission/W^X stage is never a candidate
        // for publication. Only normal-context cleanup may release it before a fresh allocation.
        state->stubVerifiedRx = false;
        state->state = RewardHookOwnership::StubCleanupPending;
        return RewardHookResult::RestorePending;
    }

    const RewardHookSpec& spec = RewardHookSpecFor(state->kind);
    std::array<uint8_t, kRewardSignatureSize> observedSignature{};
    if (!io.read(io.context, state->signatureAddress,
                 observedSignature.data(), observedSignature.size())) {
        return RewardHookResult::Failed;
    }
    if (!ValidateRewardSignature(spec, observedSignature.data(), observedSignature.size())) {
        bool originalSite = true;
        for (size_t index = 0; index < kRewardPatchSize; ++index) {
            originalSite = originalSite && observedSignature[index + 2u] == state->original[index];
        }
        if (!originalSite) {
            state->state = RewardHookOwnership::Conflict;
            return RewardHookResult::Conflict;
        }
        return RewardHookResult::SignatureMismatch;
    }

    if (!state->stubOwned) {
        uintptr_t allocation = 0;
        if (!io.allocateWritable(io.context, kRewardStubSize, &allocation) || !allocation) {
            return RewardHookResult::Failed;
        }
        state->stubAddress = allocation;
        state->stubOwned = true;
        state->stubVerifiedRx = false;
        // WHY: allocation ownership becomes cleanup-only until every byte and W^X obligation is
        // proven. If VirtualFree also fails, retries must see this sticky state and never jump to
        // malformed or still-writable memory.
        state->state = RewardHookOwnership::StubCleanupPending;

        const auto cleanupFailedStub = [&io, state]() {
            state->stubVerifiedRx = false;
            state->siteMayPointToStub = false;
            state->state = RewardHookOwnership::StubCleanupPending;
            if (io.freeAllocation(io.context, state->stubAddress, kRewardStubSize)) {
                state->stubAddress = 0;
                state->stubOwned = false;
                state->stub.fill(0);
                state->jump.fill(0);
                state->state = RewardHookOwnership::Empty;
            }
            return RewardHookResult::Failed;
        };

        if (!EncodeRewardStub(spec, state->stubAddress, state->scalarAddress,
                              state->resumeAddress, &state->stub) ||
            !EncodeRewardSiteJump(state->siteAddress, state->stubAddress, &state->jump)) {
            return cleanupFailedStub();
        }

        const MutationReport emitted = io.writeWritable(
            io.context, state->stubAddress, state->stub.data(), state->stub.size());
        std::array<uint8_t, kRewardStubSize> stubReadback{};
        if (emitted.effect == MutationEffect::NotTouched ||
            !io.read(io.context, state->stubAddress, stubReadback.data(), stubReadback.size()) ||
            stubReadback != state->stub) return cleanupFailedStub();
        if (!io.protectExecuteRead(io.context, state->stubAddress, state->stub.size())) {
            return cleanupFailedStub();
        }
        if (!io.flush(io.context, state->stubAddress, state->stub.size())) {
            return cleanupFailedStub();
        }
        stubReadback.fill(0);
        if (!io.read(io.context, state->stubAddress, stubReadback.data(), stubReadback.size()) ||
            stubReadback != state->stub) return cleanupFailedStub();

        // StubReady is the sole publishable allocation state and is assigned only after exact
        // encode, emission/readback, RX transition, cache flush, and the final RX readback.
        state->stubVerifiedRx = true;
        state->state = RewardHookOwnership::StubReady;
    }

    // Code publication happens once, on admitted Present work, only after the complete NX->RX
    // stub transition and battle-inactive proof. Later UI edits touch aligned data only.
    if (!state->stubVerifiedRx || state->state != RewardHookOwnership::StubReady) {
        state->stubVerifiedRx = false;
        state->state = RewardHookOwnership::StubCleanupPending;
        return RewardHookResult::RestorePending;
    }
    if (!io.confirmBattleInactive(io.context)) {
        return RewardHookResult::DeferredBattleActive;
    }
    if (!io.beginCodeWrite(io.context, state->siteAddress, kRewardPatchSize,
                           &state->protection)) {
        if (state->protection.active) {
            state->state = RewardHookOwnership::RestorePending;
            return RewardHookResult::RestorePending;
        }
        return RewardHookResult::Failed;
    }
    const MutationReport patched = io.writeIfEqual(
        io.context, state->siteAddress, state->original.data(), state->jump.data(),
        kRewardPatchSize);
    if (patched.effect != MutationEffect::NotTouched) state->siteMayPointToStub = true;
    const bool flushed = io.flush(io.context, state->siteAddress, kRewardPatchSize);
    const bool protectionRestored = io.endCodeWrite(io.context, &state->protection);
    std::array<uint8_t, kRewardPatchSize> siteReadback{};
    const bool readbackOk = io.read(
        io.context, state->siteAddress, siteReadback.data(), siteReadback.size());
    if (readbackOk && siteReadback == state->jump) {
        state->siteMayPointToStub = true;
        if (patched.effect == MutationEffect::NotTouched) {
            // An exact pre-existing jump was not produced by this owner. Never adopt or restore it.
            state->state = RewardHookOwnership::Conflict;
            return RewardHookResult::Conflict;
        }
        if (patched.effect != MutationEffect::NotTouched && flushed && protectionRestored) {
            state->state = RewardHookOwnership::Installed;
            return RewardHookResult::Installed;
        }
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    if (readbackOk && siteReadback == state->original) {
        if (patched.effect == MutationEffect::NotTouched && protectionRestored) {
            state->siteMayPointToStub = false;
            state->state = RewardHookOwnership::StubReady;
            return RewardHookResult::Failed;
        }
        // A potentially-mutating callback may have published the jump transiently and an earlier
        // flush may therefore describe that jump rather than the original bytes now observed.
        // Keep the stub alive until normal-context recovery flushes and re-reads the original span.
        if (patched.effect != MutationEffect::NotTouched) state->siteMayPointToStub = true;
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    state->state = readbackOk ? RewardHookOwnership::Conflict
                              : RewardHookOwnership::RestorePending;
    return readbackOk ? RewardHookResult::Conflict : RewardHookResult::RestorePending;
}

RewardHookResult RemoveRewardHook(const RewardHookIo& io, bool battleActive,
                                  RewardHookState* state) {
    if (!state || !io.context || !io.read || !io.beginCodeWrite ||
        !io.confirmBattleInactive || !io.writeIfEqual || !io.flush ||
        !io.endCodeWrite || !io.freeAllocation) {
        return RewardHookResult::Failed;
    }
    if (battleActive) return RewardHookResult::DeferredBattleActive;
    if (!state->stubOwned) {
        return state->state == RewardHookOwnership::Empty
            ? RewardHookResult::AlreadyRemoved : RewardHookResult::Failed;
    }

    if (state->state == RewardHookOwnership::StubCleanupPending &&
        !state->stubVerifiedRx && !state->siteMayPointToStub &&
        !state->protection.active) {
        // No code site was ever allowed to target this failed prepublication allocation.
        if (!io.freeAllocation(io.context, state->stubAddress, kRewardStubSize)) {
            return RewardHookResult::RestorePending;
        }
        *state = {};
        return RewardHookResult::Removed;
    }

    if (state->protection.active && !io.endCodeWrite(io.context, &state->protection)) {
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }

    std::array<uint8_t, kRewardPatchSize> observed{};
    if (!io.read(io.context, state->siteAddress, observed.data(), observed.size())) {
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    if (observed == state->original) {
        if (state->siteMayPointToStub) {
            // Exact bytes alone do not discharge an earlier failed cache flush: another core may
            // still execute its cached jump. Re-confirm the safe point, flush the already-RX site
            // without opening an RWX window, then re-read original bytes before freeing the target.
            if (!io.confirmBattleInactive(io.context)) {
                state->state = RewardHookOwnership::RestorePending;
                return RewardHookResult::DeferredBattleActive;
            }
            const bool flushed = io.flush(io.context, state->siteAddress, kRewardPatchSize);
            std::array<uint8_t, kRewardPatchSize> finalOriginal{};
            const bool finalRead = io.read(
                io.context, state->siteAddress, finalOriginal.data(), finalOriginal.size());
            if (!flushed || !finalRead || finalOriginal != state->original) {
                state->state = RewardHookOwnership::RestorePending;
                return RewardHookResult::RestorePending;
            }
            state->siteMayPointToStub = false;
        }
        // After cache provenance is proven, a failed free is only an allocation retry. The sticky
        // flag remains false so that retry re-reads original bytes without a redundant flush.
        if (!io.freeAllocation(io.context, state->stubAddress, kRewardStubSize)) {
            state->state = RewardHookOwnership::RestorePending;
            return RewardHookResult::RestorePending;
        }
        *state = {};
        return RewardHookResult::Removed;
    }
    if (observed != state->jump) {
        state->siteMayPointToStub = true;
        state->state = RewardHookOwnership::Conflict;
        return RewardHookResult::Conflict;
    }
    state->siteMayPointToStub = true;

    if (!io.confirmBattleInactive(io.context)) {
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::DeferredBattleActive;
    }
    if (!io.beginCodeWrite(io.context, state->siteAddress, kRewardPatchSize,
                           &state->protection)) {
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    const MutationReport restored = io.writeIfEqual(
        io.context, state->siteAddress, state->jump.data(), state->original.data(),
        kRewardPatchSize);
    const bool flushed = io.flush(io.context, state->siteAddress, kRewardPatchSize);
    const bool protectionRestored = io.endCodeWrite(io.context, &state->protection);
    std::array<uint8_t, kRewardPatchSize> readback{};
    const bool readbackOk = io.read(
        io.context, state->siteAddress, readback.data(), readback.size());
    if (!readbackOk || !flushed || !protectionRestored) {
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    if (readback == state->jump) {
        state->siteMayPointToStub = true;
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    if (readback != state->original) {
        state->siteMayPointToStub = true;
        state->state = RewardHookOwnership::Conflict;
        return RewardHookResult::Conflict;
    }
    (void)restored;
    state->siteMayPointToStub = false;
    if (!io.freeAllocation(io.context, state->stubAddress, kRewardStubSize)) {
        state->state = RewardHookOwnership::RestorePending;
        return RewardHookResult::RestorePending;
    }
    *state = {};
    return RewardHookResult::Removed;
}

bool CanRunRewardHookTransaction(
    const RewardHookState* states, size_t count, size_t candidateIndex) {
    if (!states || candidateIndex >= count || !states[candidateIndex].siteAddress) return false;
    constexpr uintptr_t kCodePageMask = ~static_cast<uintptr_t>(0xFFFu);
    const uintptr_t candidatePage = states[candidateIndex].siteAddress & kCodePageMask;
    for (size_t index = 0; index < count; ++index) {
        if (index == candidateIndex || !states[index].protection.active) continue;
        // AP and Gil currently share RVA page 0x00399000. The prior protection returned by the
        // platform adapter is page state, so one unresolved token makes the peer transaction
        // ambiguous even though the six-byte sites otherwise retain independent ownership.
        if ((states[index].siteAddress & kCodePageMask) == candidatePage) return false;
    }
    return true;
}

bool IsRewardPriorProtectionAdmitted(bool executable, bool writable) {
    return executable && !writable;
}

RewardBindingResult RewardBindingResultFromHook(RewardHookResult result) {
    switch (result) {
        case RewardHookResult::DeferredBattleActive:
        case RewardHookResult::RestorePending:
        case RewardHookResult::Removed:
        case RewardHookResult::AlreadyRemoved:
            return RewardBindingResult::HookDeferred;
        case RewardHookResult::Conflict:
            return RewardBindingResult::Conflict;
        default:
            return RewardBindingResult::HookUnavailable;
    }
}

ProfileResult ParseExecutableIdentity(
    const uint8_t* image, size_t mappedLength, ExecutableIdentity* identityOut) {
    if (identityOut) *identityOut = {};
    if (!image || !HasBytes(0, kDosPeOffsetField + sizeof(uint32_t), mappedLength) ||
        image[0] != 'M' || image[1] != 'Z') {
        return ProfileResult::BadDos;
    }

    const uint32_t peOffset32 = ReadU32(image + kDosPeOffsetField);
    const size_t peOffset = static_cast<size_t>(peOffset32);
    const size_t fixedPeSize = kPeSignatureSize + kCoffHeaderSize;
    if (peOffset < kMinimumDosHeaderSize ||
        !HasBytes(peOffset, fixedPeSize, mappedLength)) {
        return ProfileResult::BadPe;
    }
    if (image[peOffset] != 'P' || image[peOffset + 1] != 'E' ||
        image[peOffset + 2] != 0 || image[peOffset + 3] != 0) {
        return ProfileResult::BadPe;
    }

    const uint8_t* coff = image + peOffset + kPeSignatureSize;
    const uint16_t optionalSize = ReadU16(coff + 16);
    const size_t optionalOffset = peOffset + fixedPeSize;
    if (optionalSize < kMinimumOptionalHeaderSize ||
        !HasBytes(optionalOffset, optionalSize, mappedLength)) {
        return ProfileResult::BadOptionalHeader;
    }

    const uint8_t* optional = image + optionalOffset;
    const ExecutableIdentity identity = {
        ReadU16(coff),
        ReadU16(optional),
        ReadU32(coff + 4),
        ReadU32(optional + kOptionalSizeOfImageOffset),
    };
    if (identityOut) *identityOut = identity;

    if (identity.optionalMagic != kSupportedOptionalMagic) {
        return ProfileResult::BadOptionalHeader;
    }
    if (identity.machine != kSupportedMachine) return ProfileResult::WrongMachine;
    if (identity.timestamp != kSupportedTimestamp) return ProfileResult::WrongTimestamp;
    if (identity.sizeOfImage != kSupportedSizeOfImage) return ProfileResult::WrongImageSize;
    return ProfileResult::Supported;
}

bool IsSupportedExecutable(const ExecutableIdentity& identity) {
    return identity.machine == kSupportedMachine &&
           identity.optionalMagic == kSupportedOptionalMagic &&
           identity.timestamp == kSupportedTimestamp &&
           identity.sizeOfImage == kSupportedSizeOfImage;
}

ProfileResult ValidateImageRange(uint32_t rva, size_t length, uint32_t sizeOfImage) {
    if (length > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return ProfileResult::RangeOverflow;
    }
    const uint64_t end = static_cast<uint64_t>(rva) + static_cast<uint64_t>(length);
    if (end > (std::numeric_limits<uint32_t>::max)()) {
        return ProfileResult::RangeOverflow;
    }
    if (rva > sizeOfImage || end > sizeOfImage) return ProfileResult::OutOfImage;
    return ProfileResult::Supported;
}

ByteTransition UpdateOwnedByte(
    const ByteIo& io, uintptr_t address, uint8_t desired, bool enabled, OwnedByte* owned) {
    if (!owned) return ByteTransition::ReadFailed;
    if (!enabled) return RestoreOwnedByte(io, owned);
    if (!io.read || !io.write) return ByteTransition::ReadFailed;

    if (owned->captured && owned->address != address) {
        // Reject the new address without poisoning the independently restorable old owner.
        return ByteTransition::Conflict;
    }
    // Conflict is sticky for active updates. Only RestoreOwnedByte may clear it after reading
    // the captured original; a candidate value is no longer proof that the byte is ours.
    if (owned->state == OwnershipState::Conflict) return ByteTransition::Conflict;

    uint8_t current = 0;
    if (!io.read(io.context, address, &current)) return ByteTransition::ReadFailed;

    const bool wasCaptured = owned->captured;
    if (!wasCaptured) {
        if (current == desired) return ByteTransition::NoChange;
        owned->address = address;
        owned->original = current;
        owned->desired = desired;
        ClearPriorDesired(owned);
        owned->captured = true;
        owned->state = OwnershipState::RestorePending;
    } else {
        if (owned->state == OwnershipState::RestorePending) {
            if (current == owned->original) {
                // Original readback resolves every pending candidate before another write.
                ClearPriorDesired(owned);
                owned->state = OwnershipState::Owned;
            } else if (IsOwnedCandidate(*owned, current)) {
                // Collapse a possibly-mutated transition to the one candidate now observed.
                owned->desired = current;
                ClearPriorDesired(owned);
                owned->state = OwnershipState::Owned;
            } else {
                owned->state = OwnershipState::Conflict;
                return ByteTransition::Conflict;
            }
        } else if (current != owned->desired && current != owned->original) {
            // A third value belongs to the game or another mod; never reassert over it.
            owned->state = OwnershipState::Conflict;
            return ByteTransition::Conflict;
        }
        if (current == owned->original && desired == owned->original) {
            ClearOwnership(owned);
            return ByteTransition::Restored;
        }
        if (current == desired) {
            owned->desired = desired;
            ClearPriorDesired(owned);
            owned->state = OwnershipState::Owned;
            return ByteTransition::NoChange;
        }

        // When changing an owned value, both the old and requested values remain possible
        // until readback resolves the write. There is never more than one prior candidate.
        if (current != owned->original) {
            owned->priorDesired = current;
            owned->hasPriorDesired = true;
        } else {
            ClearPriorDesired(owned);
        }
        owned->desired = desired;
        owned->state = OwnershipState::RestorePending;
    }

    const WriteResult write = io.write(io.context, address, desired);
    if (write.effect == WriteEffect::NotTouched) {
        if (current == owned->original) {
            ClearOwnership(owned);
        } else {
            owned->desired = current;
            ClearPriorDesired(owned);
            owned->state = OwnershipState::Owned;
        }
        return ByteTransition::WriteFailed;
    }

    uint8_t readback = 0;
    if (!io.read(io.context, address, &readback)) {
        owned->state = OwnershipState::RestorePending;
        return ByteTransition::ReadbackFailed;
    }
    if (readback == owned->original) {
        const bool requestedOriginal = desired == owned->original;
        ClearOwnership(owned);
        return requestedOriginal ? ByteTransition::Restored : ByteTransition::WriteFailed;
    }
    if (readback == desired) {
        ClearPriorDesired(owned);
        owned->state = OwnershipState::Owned;
        return wasCaptured ? ByteTransition::Reasserted : ByteTransition::Applied;
    }
    if (owned->hasPriorDesired && readback == owned->priorDesired) {
        owned->desired = owned->priorDesired;
        ClearPriorDesired(owned);
        owned->state = OwnershipState::Owned;
        return ByteTransition::WriteFailed;
    }
    owned->state = OwnershipState::Conflict;
    return ByteTransition::Conflict;
}

ByteTransition RestoreOwnedByte(const ByteIo& io, OwnedByte* owned) {
    if (!owned || !owned->captured) return ByteTransition::NoChange;
    if (!io.read || !io.write) {
        if (owned->state == OwnershipState::Conflict) return ByteTransition::Conflict;
        owned->state = OwnershipState::RestorePending;
        return ByteTransition::RestorePending;
    }

    uint8_t current = 0;
    if (!io.read(io.context, owned->address, &current)) {
        if (owned->state == OwnershipState::Conflict) return ByteTransition::Conflict;
        owned->state = OwnershipState::RestorePending;
        return ByteTransition::RestorePending;
    }
    if (current == owned->original) {
        ClearOwnership(owned);
        return ByteTransition::Restored;
    }
    if (owned->state == OwnershipState::Conflict) return ByteTransition::Conflict;
    if (!IsOwnedCandidate(*owned, current)) {
        owned->state = OwnershipState::Conflict;
        return ByteTransition::Conflict;
    }

    owned->state = OwnershipState::RestorePending;
    const WriteResult write = io.write(io.context, owned->address, owned->original);
    if (write.effect == WriteEffect::NotTouched) return ByteTransition::WriteFailed;

    uint8_t readback = 0;
    if (!io.read(io.context, owned->address, &readback)) {
        return ByteTransition::ReadbackFailed;
    }
    if (readback == owned->original) {
        ClearOwnership(owned);
        return ByteTransition::Restored;
    }
    if (IsOwnedCandidate(*owned, readback)) return ByteTransition::RestorePending;
    owned->state = OwnershipState::Conflict;
    return ByteTransition::Conflict;
}

RewardBindingResult UpdateRewardBinding(
    const RewardScalarIo& scalarIo, const ByteIo& gateIo, uintptr_t gateAddress,
    bool hookInstalled, bool configurationValid, int multiplier, bool enabled,
    OwnedByte* gateOwner, int32_t* appliedScalarOut) {
    if (appliedScalarOut) *appliedScalarOut = 0;
    if (!gateOwner || !gateAddress || !gateIo.read || !gateIo.write) {
        return RewardBindingResult::GateFailed;
    }

    auto classifyGate = [](ByteTransition transition) {
        return transition == ByteTransition::Conflict
            ? RewardBindingResult::Conflict
            : transition == ByteTransition::ReadFailed ||
                      transition == ByteTransition::WriteFailed ||
                      transition == ByteTransition::ReadbackFailed ||
                      transition == ByteTransition::RestorePending
                ? RewardBindingResult::GateFailed
                : RewardBindingResult::Disabled;
    };
    const auto disarmAfterFailure = [&gateIo, gateOwner, classifyGate](
                                         RewardBindingResult primaryFailure) {
        if (!gateOwner->captured) return primaryFailure;
        // Once data publication has started, every failure must immediately attempt to make the
        // already-owned execution gate inert. We keep pending/conflict ownership when OFF cannot
        // be proven; silently leaving an old ON gate would execute with ambiguous scalar data.
        const RewardBindingResult restore = classifyGate(RestoreOwnedByte(gateIo, gateOwner));
        if (restore == RewardBindingResult::Conflict) return RewardBindingResult::Conflict;
        if (restore == RewardBindingResult::GateFailed) return RewardBindingResult::GateFailed;
        return primaryFailure;
    };

    if (!configurationValid || !IsRewardMultiplierInRange(multiplier)) {
        // Invalid text is a fail-closed condition: restore a byte we own, but never adopt or
        // normalize an externally-ON byte and never publish scalar data. Even though there is no
        // scalar write on this branch, an unowned ON gate would execute the already-installed stub
        // with stale data, so it must remain an explicit ownership conflict rather than look OFF.
        if (!gateOwner->captured) {
            uint8_t unownedGate = 0;
            if (!gateIo.read(gateIo.context, gateAddress, &unownedGate)) {
                return RewardBindingResult::GateFailed;
            }
            if (unownedGate != 0) return RewardBindingResult::Conflict;
        }
        const RewardBindingResult restore = classifyGate(RestoreOwnedByte(gateIo, gateOwner));
        if (restore == RewardBindingResult::Conflict ||
            restore == RewardBindingResult::GateFailed) {
            return restore;
        }
        return RewardBindingResult::InvalidConfiguration;
    }

    if (!hookInstalled) {
        if (enabled) return RewardBindingResult::HookUnavailable;
        return classifyGate(RestoreOwnedByte(gateIo, gateOwner));
    }

    uint8_t gateBefore = 0;
    if (!gateIo.read(gateIo.context, gateAddress, &gateBefore)) {
        return RewardBindingResult::GateFailed;
    }
    if (!gateOwner->captured && gateBefore != 0) {
        // An already-ON byte was not created by this owner. Treat it as an external mod conflict.
        return RewardBindingResult::Conflict;
    }

    if (!enabled) {
        const RewardBindingResult result = classifyGate(RestoreOwnedByte(gateIo, gateOwner));
        uint8_t gateAfter = 0;
        if (!gateIo.read(gateIo.context, gateAddress, &gateAfter)) {
            return RewardBindingResult::GateFailed;
        }
        if (gateAfter != 0) return RewardBindingResult::Conflict;
        return result == RewardBindingResult::Conflict ? result : RewardBindingResult::Disabled;
    }

    if (!scalarIo.context || !scalarIo.exchange || !scalarIo.read ||
        !scalarIo.exchange(scalarIo.context, static_cast<int32_t>(multiplier))) {
        return disarmAfterFailure(RewardBindingResult::ScalarWriteFailed);
    }
    int32_t scalarReadback = 0;
    if (!scalarIo.read(scalarIo.context, &scalarReadback) || scalarReadback != multiplier) {
        return disarmAfterFailure(RewardBindingResult::ScalarReadbackFailed);
    }

    // Exact data readback is the publication barrier. The debug gate cannot turn on before it.
    const ByteTransition transition = UpdateOwnedByte(
        gateIo, gateAddress, 1u, true, gateOwner);
    if (transition == ByteTransition::NoChange && !gateOwner->captured) {
        // The pre-scalar read observed OFF, but the ownership read observed an external ON. A
        // desired byte with no captured original is never ours, even though its value is 1.
        return RewardBindingResult::Conflict;
    }
    if (transition == ByteTransition::Conflict) {
        return disarmAfterFailure(RewardBindingResult::Conflict);
    }
    if (transition == ByteTransition::ReadFailed || transition == ByteTransition::WriteFailed ||
        transition == ByteTransition::ReadbackFailed || transition == ByteTransition::RestorePending) {
        return disarmAfterFailure(RewardBindingResult::GateFailed);
    }
    uint8_t gateReadback = 0;
    if (!gateIo.read(gateIo.context, gateAddress, &gateReadback) || gateReadback != 1u) {
        return disarmAfterFailure(RewardBindingResult::GateFailed);
    }
    if (appliedScalarOut) *appliedScalarOut = scalarReadback;
    return RewardBindingResult::Applied;
}

BundleResult ApplySeymourBundle(const PatchIo& io, SeymourBundleState* state) {
    if (!state || !ValidPatchIo(io) || !ValidSeymourLayout(*state)) {
        return BundleResult::PreflightFailed;
    }
    if (state->state == BundleState::Active) {
        const ResourceObservation site1 = ObservePatchSite(io, state->sites[0]);
        const ResourceObservation site2 = ObservePatchSite(io, state->sites[1]);
        uint8_t partyValue = 0;
        const ResourceObservation party = ObserveParty(io, state->party, &partyValue);
        const bool partyActive =
            static_cast<uint8_t>(partyValue & state->party.mask) == state->party.mask;
        if (site1 == ResourceObservation::Unreadable ||
            site2 == ResourceObservation::Unreadable ||
            party == ResourceObservation::Unreadable) {
            state->state = BundleState::RestorePending;
            return BundleResult::RestorePending;
        }
        if (site1 == ResourceObservation::Third) {
            state->sites[0].conflictWitness = true;
        }
        if (site2 == ResourceObservation::Third) {
            state->sites[1].conflictWitness = true;
        }
        if (!partyActive) {
            // Even an unexpected Original value while Active is external drift. Recording the
            // witness prevents a later Candidate from being mistaken for our write.
            state->party.conflictWitness = true;
        }
        if (HasConflictWitness(*state)) {
            state->state = BundleState::Conflict;
            return BundleResult::Conflict;
        }
        if (site1 != ResourceObservation::Candidate ||
            site2 != ResourceObservation::Candidate ||
            !partyActive) {
            state->state = BundleState::RestorePending;
            return BundleResult::RestorePending;
        }
        return BundleResult::AlreadyInState;
    }
    if (state->state != BundleState::Inactive || HasSeymourObligation(*state)) {
        if (state->state != BundleState::Conflict) {
            state->state = BundleState::RestorePending;
        }
        const BundleResult cleanup = RestoreSeymourBundle(io, state);
        if (cleanup == BundleResult::Restored || cleanup == BundleResult::AlreadyInState) {
            return BundleResult::ApplyFailedRolledBack;
        }
        return cleanup;
    }

    std::array<uint8_t, 4> site1{};
    std::array<uint8_t, 4> site2{};
    uint8_t party = 0;
    const bool readSite1 = ReadPatchSite(io, state->sites[0].address, &site1);
    const bool readSite2 = ReadPatchSite(io, state->sites[1].address, &site2);
    const bool readParty = io.read(io.context, state->party.address, &party, 1);
    if (!readSite1 || !readSite2 || !readParty) return BundleResult::PreflightFailed;
    if (!BytesEqual(site1, kSeymourSignature1) ||
        !BytesEqual(site2, kSeymourSignature2)) {
        return BundleResult::SignatureMismatch;
    }

    state->sites[0].original = site1;
    state->sites[1].original = site2;
    state->sites[0].conflictWitness = false;
    state->sites[1].conflictWitness = false;
    state->sites[0].restorePhase = ResourceRestorePhase::None;
    state->sites[1].restorePhase = ResourceRestorePhase::None;
    state->party.originalMaskedBits = static_cast<uint8_t>(party & state->party.mask);
    state->party.conflictWitness = false;
    state->party.restorePhase = ResourceRestorePhase::None;
    state->state = BundleState::Acquiring;
    PatchProtectionToken begunProtection{};
    const bool began = io.beginCodeWrite(
        io.context, state->sites[0].address, FFX_SEYMOUR_PATCH_SPAN_LENGTH,
        &begunProtection);
    if (begunProtection.active) state->protection = begunProtection;
    if (!began || !ValidActiveProtection(*state)) {
        return FinishFailedSeymourApply(io, state, false);
    }

    for (PatchSiteState& patchSite : state->sites) {
        const MutationReport report = io.writeIfEqual(
            io.context, patchSite.address, patchSite.original.data(),
            kSeymourNops.data(), kSeymourNops.size());
        const MutationEffect effect = ConservativeMutationEffect(report);
        if (effect != MutationEffect::NotTouched) patchSite.possiblyOwned = true;
        const ResourceObservation readback = ObservePatchSite(io, patchSite);
        if (readback == ResourceObservation::Candidate) {
            if (effect == MutationEffect::NotTouched) {
                // A desired value that appeared despite a failed compare belongs to someone else.
                patchSite.conflictWitness = true;
                return FinishFailedSeymourApply(io, state, true);
            }
            continue;
        }
        if (readback == ResourceObservation::Original) {
            return FinishFailedSeymourApply(io, state, false);
        }
        if (readback == ResourceObservation::Third) {
            patchSite.conflictWitness = true;
        }
        return FinishFailedSeymourApply(
            io, state, readback == ResourceObservation::Third);
    }
    if (!io.flush(io.context, state->sites[0].address,
                  FFX_SEYMOUR_PATCH_SPAN_LENGTH)) {
        return FinishFailedSeymourApply(io, state, false);
    }
    if (!EndSeymourProtection(io, state)) {
        return FinishFailedSeymourApply(io, state, false);
    }

    uint8_t currentParty = 0;
    if (!io.read(io.context, state->party.address, &currentParty, 1)) {
        return FinishFailedSeymourApply(io, state, false);
    }
    const uint8_t controlledParty =
        static_cast<uint8_t>(currentParty & state->party.mask);
    if (controlledParty == state->party.mask) {
        if (state->party.originalMaskedBits != state->party.mask) {
            state->party.conflictWitness = true;
            return FinishFailedSeymourApply(io, state, true);
        }
        state->state = BundleState::Active;
        return BundleResult::Applied;
    }
    if (controlledParty != state->party.originalMaskedBits) {
        // A controlled-bit change between preflight and the final party write is not ours.
        // Record it only as a conflict witness; no callback has authorized ownership.
        state->party.conflictWitness = true;
        return FinishFailedSeymourApply(io, state, true);
    }
    const uint8_t desiredParty = static_cast<uint8_t>(
        (currentParty & static_cast<uint8_t>(~state->party.mask)) | state->party.mask);
    const MutationReport partyWrite = io.writeIfEqual(
        io.context, state->party.address, &currentParty, &desiredParty, 1);
    const MutationEffect partyEffect = ConservativeMutationEffect(partyWrite);
    if (partyEffect != MutationEffect::NotTouched) state->party.possiblyOwned = true;
    uint8_t partyReadback = 0;
    if (!io.read(io.context, state->party.address, &partyReadback, 1)) {
        return FinishFailedSeymourApply(io, state, false);
    }
    const uint8_t controlledReadback =
        static_cast<uint8_t>(partyReadback & state->party.mask);
    if (controlledReadback == state->party.mask) {
        if (partyEffect == MutationEffect::NotTouched) {
            state->party.conflictWitness = true;
            return FinishFailedSeymourApply(io, state, true);
        }
        state->state = BundleState::Active;
        return BundleResult::Applied;
    }
    if (controlledReadback == state->party.originalMaskedBits) {
        return FinishFailedSeymourApply(io, state, false);
    }
    state->party.conflictWitness = true;
    return FinishFailedSeymourApply(io, state, true);
}

BundleResult RestoreSeymourBundle(const PatchIo& io, SeymourBundleState* state) {
    if (!state) return BundleResult::PreflightFailed;
    const bool hasObligation = HasSeymourObligation(*state);
    if (state->state == BundleState::Inactive && !hasObligation) {
        return BundleResult::AlreadyInState;
    }
    if (!ValidPatchIo(io) || !ValidSeymourLayout(*state)) {
        const bool cleanupRequired = hasObligation || state->state != BundleState::Inactive;
        state->state = cleanupRequired ? BundleState::RestorePending : BundleState::Inactive;
        return cleanupRequired ? BundleResult::RestorePending : BundleResult::PreflightFailed;
    }

    const bool partyRelevantAtEntry = PartyRelevant(state->party);
    const bool codeOwnedAtEntry =
        state->sites[0].possiblyOwned || state->sites[1].possiblyOwned ||
        HasRestoreProvenance(state->sites[0].restorePhase) ||
        HasRestoreProvenance(state->sites[1].restorePhase);
    uint8_t observedParty = 0;
    const ResourceObservation partyObservation = partyRelevantAtEntry
        ? ObserveParty(io, state->party, &observedParty)
        : ResourceObservation::Original;
    std::array<ResourceObservation, 2> codeObservations = {
        ResourceObservation::Original, ResourceObservation::Original};
    for (size_t reverse = state->sites.size(); reverse != 0; --reverse) {
        const size_t index = reverse - 1;
        if (PatchSiteRelevant(state->sites[index])) {
            codeObservations[index] = ObservePatchSite(io, state->sites[index]);
        }
    }

    bool pending = false;
    bool partyVerifiedOriginal = !partyRelevantAtEntry;

    if (partyRelevantAtEntry) {
        if (partyObservation == ResourceObservation::Original) {
            partyVerifiedOriginal = true;
            ConfirmPartyOriginal(&state->party);
        } else if (partyObservation == ResourceObservation::Third) {
            state->party.conflictWitness = true;
        } else if (partyObservation == ResourceObservation::Candidate &&
                   (state->party.conflictWitness ||
                    HasRestoreProvenance(state->party.restorePhase))) {
            // A prior restore observation or ambiguous mutation permanently revokes Candidate
            // write authority until this exact resource is observed Original again.
            state->party.conflictWitness = true;
        } else if (partyObservation == ResourceObservation::Candidate &&
                   state->party.possiblyOwned) {
            const uint8_t original = static_cast<uint8_t>(
                (observedParty & static_cast<uint8_t>(~state->party.mask)) |
                state->party.originalMaskedBits);
            const MutationReport report = io.writeIfEqual(
                io.context, state->party.address, &observedParty, &original, 1);
            const MutationEffect effect = ConservativeMutationEffect(report);
            RecordPotentialRestoreMutation(effect, &state->party.restorePhase);
            uint8_t readback = 0;
            const ResourceObservation restored = ObserveParty(io, state->party, &readback);
            if (restored == ResourceObservation::Original) {
                partyVerifiedOriginal = true;
                ConfirmPartyOriginal(&state->party);
            } else if (restored == ResourceObservation::Third) {
                state->party.conflictWitness = true;
            } else if (restored == ResourceObservation::Candidate) {
                // Candidate after this conditional restore is never evidence that our restore
                // retained authority, regardless of the callback's mutation classification.
                state->party.conflictWitness = true;
            } else {
                pending = true;
            }
        } else {
            pending = true;
        }
    }

    std::array<bool, 2> verifiedOriginal = {
        !PatchSiteRelevant(state->sites[0]),
        !PatchSiteRelevant(state->sites[1])};
    std::array<bool, 2> needsWrite = {false, false};
    for (size_t index = 0; index < state->sites.size(); ++index) {
        PatchSiteState& site = state->sites[index];
        if (!PatchSiteRelevant(site)) continue;
        if (codeObservations[index] == ResourceObservation::Original) {
            verifiedOriginal[index] = true;
            ConfirmPatchSiteOriginal(&site);
        } else if (codeObservations[index] == ResourceObservation::Third) {
            site.conflictWitness = true;
        } else if (codeObservations[index] == ResourceObservation::Candidate &&
                   (site.conflictWitness || HasRestoreProvenance(site.restorePhase))) {
            // Persisted restore provenance makes a later Candidate external, even when global
            // flush or protection restoration kept the bundle pending.
            site.conflictWitness = true;
        } else if (codeObservations[index] == ResourceObservation::Candidate &&
                   site.possiblyOwned) {
            needsWrite[index] = true;
        } else {
            pending = true;
        }
    }

    const bool needsCodeWrite = needsWrite[0] || needsWrite[1];
    bool protectionReady = ValidActiveProtection(*state);
    if (needsCodeWrite && !state->protection.active) {
        PatchProtectionToken begunProtection{};
        const bool began = io.beginCodeWrite(
            io.context, state->sites[0].address, FFX_SEYMOUR_PATCH_SPAN_LENGTH,
            &begunProtection);
        if (begunProtection.active) state->protection = begunProtection;
        protectionReady = began && ValidActiveProtection(*state);
        if (!protectionReady) pending = true;
    } else if (needsCodeWrite && !protectionReady) {
        pending = true;
    }

    if (protectionReady) {
        for (size_t reverse = state->sites.size(); reverse != 0; --reverse) {
            const size_t index = reverse - 1;
            if (!needsWrite[index]) continue;
            PatchSiteState& patchSite = state->sites[index];
            const MutationReport report = io.writeIfEqual(
                io.context, patchSite.address, kSeymourNops.data(), patchSite.original.data(),
                patchSite.original.size());
            const MutationEffect effect = ConservativeMutationEffect(report);
            RecordPotentialRestoreMutation(effect, &patchSite.restorePhase);
            const ResourceObservation readback = ObservePatchSite(io, patchSite);
            if (readback == ResourceObservation::Original) {
                verifiedOriginal[index] = true;
                ConfirmPatchSiteOriginal(&patchSite);
            } else if (readback == ResourceObservation::Third) {
                patchSite.conflictWitness = true;
            } else if (readback == ResourceObservation::Candidate) {
                // All callback effects are conservative here: Candidate may have been restored
                // by another writer after our write but before this independent readback.
                patchSite.conflictWitness = true;
            } else {
                pending = true;
            }
        }
    }

    bool flushConfirmed = true;
    if (codeOwnedAtEntry) {
        flushConfirmed = io.flush(
            io.context, state->sites[0].address, FFX_SEYMOUR_PATCH_SPAN_LENGTH);
        if (!flushConfirmed) pending = true;
    }
    const bool protectionConfirmed = EndSeymourProtection(io, state);
    if (!protectionConfirmed) pending = true;
    const bool conflict = HasConflictWitness(*state);
    bool allCodeVerifiedOriginal = true;
    for (size_t index = 0; index < state->sites.size(); ++index) {
        if (PatchSiteRelevant(state->sites[index]) && !verifiedOriginal[index]) {
            allCodeVerifiedOriginal = false;
        }
    }
    const bool bundleRestorationConfirmed = partyVerifiedOriginal &&
        allCodeVerifiedOriginal && flushConfirmed && protectionConfirmed &&
        !pending && !conflict;
    if (bundleRestorationConfirmed) {
        ClearSeymourOwnership(state);
    }

    const bool remainsOwned = HasSeymourObligation(*state);
    if (pending) {
        state->state = BundleState::RestorePending;
        return BundleResult::RestorePending;
    }
    if (conflict) {
        state->state = BundleState::Conflict;
        return BundleResult::Conflict;
    }
    if (remainsOwned) {
        state->state = BundleState::RestorePending;
        return BundleResult::RestorePending;
    }
    state->state = BundleState::Inactive;
    return BundleResult::Restored;
}

bool ArmTickGate(TickGate* gate) {
    if (!gate || gate->armed) return false;
    gate->lastTickMs = 0;
    gate->armed = true;
    gate->inTick = false;
    gate->hasTicked = false;
    return true;
}

bool TryBeginTick(TickGate* gate, uint32_t nowMs, uint32_t intervalMs) {
    if (!gate || !gate->armed || gate->inTick) return false;
    if (gate->hasTicked && static_cast<uint32_t>(nowMs - gate->lastTickMs) < intervalMs) {
        return false;
    }
    gate->lastTickMs = nowMs;
    gate->hasTicked = true;
    gate->inTick = true;
    return true;
}

void EndTick(TickGate* gate) {
    if (gate) gate->inTick = false;
}

bool DisarmTickGate(TickGate* gate) {
    if (!gate || !gate->armed) return false;
    gate->lastTickMs = 0;
    gate->armed = false;
    gate->inTick = false;
    gate->hasTicked = false;
    return true;
}

bool TryBeginPresentHookInstall(AtomicPresentHookArbiter* arbiter) {
    if (!arbiter) return false;
    uint32_t current = arbiter->word.load(std::memory_order_acquire);
    while (PresentPhysicalState(current) == PresentHookPhysicalState::Idle) {
        const uint32_t desired =
            (current & ~kPresentPhysicalMask) |
            static_cast<uint32_t>(PresentHookPhysicalState::Installing);
        if (arbiter->word.compare_exchange_weak(
                current, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

PresentHookResult CompletePresentHookInstall(
    AtomicPresentHookArbiter* arbiter, bool installed) {
    if (!arbiter) return PresentHookResult::None;
    uint32_t current = arbiter->word.load(std::memory_order_acquire);
    while (PresentPhysicalState(current) == PresentHookPhysicalState::Installing) {
        const bool terminalRequested = (current & kPresentTerminalRequested) != 0;
        const bool terminalPublished = (current & kPresentTerminalPublished) != 0;
        PresentHookResult result = PresentHookResult::None;
        uint32_t desired = current & kPresentTerminalPublished;
        if (installed) {
            desired |= static_cast<uint32_t>(PresentHookPhysicalState::Ready);
            // A claimed terminal callback may still be pending outside this arbiter. Keep the
            // shared physical hook Ready without transiently resurrecting the UnX lifecycle.
            result = terminalPublished ? PresentHookResult::None : PresentHookResult::Ready;
        } else {
            desired |= static_cast<uint32_t>(PresentHookPhysicalState::Idle);
            if (terminalRequested && !terminalPublished) {
                desired |= kPresentTerminalPublished;
                result = PresentHookResult::PublishTerminal;
            }
        }
        if (arbiter->word.compare_exchange_weak(
                current, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return result;
        }
    }
    return PresentHookResult::None;
}

PresentHookResult RequestPresentHookTerminal(AtomicPresentHookArbiter* arbiter) {
    if (!arbiter) return PresentHookResult::None;
    uint32_t current = arbiter->word.load(std::memory_order_acquire);
    while (true) {
        if ((current & kPresentTerminalPublished) != 0 ||
            PresentPhysicalState(current) == PresentHookPhysicalState::Ready) {
            return PresentHookResult::None;
        }
        const PresentHookPhysicalState physical = PresentPhysicalState(current);
        uint32_t desired = current;
        PresentHookResult result = PresentHookResult::None;
        if (physical == PresentHookPhysicalState::Idle) {
            desired = (current & ~kPresentTerminalRequested) | kPresentTerminalPublished;
            result = PresentHookResult::PublishTerminal;
        } else if (physical == PresentHookPhysicalState::Installing) {
            desired = current | kPresentTerminalRequested;
        } else {
            return PresentHookResult::None;
        }
        if (arbiter->word.compare_exchange_weak(
                current, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return result;
        }
    }
}

PresentHookPhysicalState ReadPresentHookPhysicalState(
    const AtomicPresentHookArbiter* arbiter) {
    if (!arbiter) return PresentHookPhysicalState::Idle;
    return PresentPhysicalState(arbiter->word.load(std::memory_order_acquire));
}

bool ResetPresentHookPhysicalState(AtomicPresentHookArbiter* arbiter) {
    if (!arbiter) return false;
    uint32_t current = arbiter->word.load(std::memory_order_acquire);
    while (PresentPhysicalState(current) != PresentHookPhysicalState::Installing) {
        const uint32_t desired =
            (current & ~kPresentPhysicalMask) |
            static_cast<uint32_t>(PresentHookPhysicalState::Idle);
        if (arbiter->word.compare_exchange_weak(
                current, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

bool StartLifecycle(AtomicLifecycle* lifecycle) {
    if (!lifecycle) return false;
    uint32_t expected = static_cast<uint32_t>(RuntimeLifecycle::Stopped);
    return lifecycle->state.compare_exchange_strong(
        expected, static_cast<uint32_t>(RuntimeLifecycle::Running),
        std::memory_order_acq_rel, std::memory_order_acquire);
}

bool TryEnterFrame(AtomicLifecycle* lifecycle) {
    if (!lifecycle || lifecycle->state.load(std::memory_order_acquire) !=
                          static_cast<uint32_t>(RuntimeLifecycle::Running)) {
        return false;
    }
    lifecycle->frameInFlight.fetch_add(1, std::memory_order_acq_rel);
#ifdef FFXHOOKS_TESTING
    if (lifecycle->afterProvisionalIncrementForTests) {
        lifecycle->afterProvisionalIncrementForTests(lifecycle->testContext);
    }
#endif
    // Admission linearizes at this second state observation. A concurrent stop makes the
    // provisional increment roll back before the caller can touch cadence state.
    if (lifecycle->state.load(std::memory_order_acquire) ==
        static_cast<uint32_t>(RuntimeLifecycle::Running)) {
        return true;
    }
    lifecycle->frameInFlight.fetch_sub(1, std::memory_order_acq_rel);
    return false;
}

void LeaveFrame(AtomicLifecycle* lifecycle) {
    if (!lifecycle) return;
    uint32_t current = lifecycle->frameInFlight.load(std::memory_order_acquire);
    while (current != 0 && !lifecycle->frameInFlight.compare_exchange_weak(
                               current, current - 1, std::memory_order_acq_rel,
                               std::memory_order_acquire)) {
    }
}

void RequestLifecycleStop(AtomicLifecycle* lifecycle) {
    if (!lifecycle) return;
    lifecycle->state.store(
        static_cast<uint32_t>(RuntimeLifecycle::Stopping), std::memory_order_release);
}

void PublishProducerState(AtomicLifecycle* lifecycle, ProducerState state) {
    if (!lifecycle || state == ProducerState::Unknown) return;
    if (state == ProducerState::TerminalFailure) {
        // Terminal failure wins every race with Ready and is never reset by StartLifecycle.
        lifecycle->producer.store(
            static_cast<uint32_t>(ProducerState::TerminalFailure), std::memory_order_release);
        return;
    }
    uint32_t expected = static_cast<uint32_t>(ProducerState::Unknown);
    lifecycle->producer.compare_exchange_strong(
        expected, static_cast<uint32_t>(ProducerState::Ready),
        std::memory_order_acq_rel, std::memory_order_acquire);
}

ProducerState ReadProducerState(const AtomicLifecycle* lifecycle) {
    if (!lifecycle) return ProducerState::Unknown;
    return static_cast<ProducerState>(lifecycle->producer.load(std::memory_order_acquire));
}

bool HasSeededBattleParticipant(
    const std::array<uint8_t, kApSlotCount>& inParty,
    const std::array<uint8_t, kApSlotCount>& participation) {
    for (size_t index = 0; index < kApSlotCount; ++index) {
        if (IsEligible(inParty[index]) && participation[index] == 1) return true;
    }
    return false;
}

std::array<ApSlotUpdate, kApSlotCount> ComputeApUpdates(
    const std::array<uint8_t, kApSlotCount>& inParty,
    const std::array<uint8_t, kApSlotCount>& participation) {
    std::array<ApSlotUpdate, kApSlotCount> updates{};
    for (size_t index = 0; index < kApSlotCount; ++index) {
        const bool eligible = IsEligible(inParty[index]);
        updates[index].participation = eligible
            ? static_cast<uint8_t>(participation[index] == 1 ? 1 : 2)
            : static_cast<uint8_t>(0);
        updates[index].earn = static_cast<uint8_t>(eligible ? 1 : 0);
    }
    return updates;
}

} // namespace FfxHooks::F8Runtime
