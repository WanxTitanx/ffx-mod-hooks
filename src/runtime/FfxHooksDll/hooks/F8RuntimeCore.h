#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::F8Runtime {

struct ExecutableIdentity {
    uint16_t machine;
    uint16_t optionalMagic;
    uint32_t timestamp;
    uint32_t sizeOfImage;
};

enum class ProfileResult : uint8_t {
    Supported = 0,
    BadDos,
    BadPe,
    BadOptionalHeader,
    WrongMachine,
    WrongTimestamp,
    WrongImageSize,
    RangeOverflow,
    OutOfImage,
};

ProfileResult ParseExecutableIdentity(
    const uint8_t* image, size_t mappedLength, ExecutableIdentity* identityOut);
bool IsSupportedExecutable(const ExecutableIdentity& identity);
ProfileResult ValidateImageRange(uint32_t rva, size_t length, uint32_t sizeOfImage);

enum class RewardKind : uint8_t { Ap = 0, Gil };
inline constexpr size_t kRewardSignatureSize = 8;
inline constexpr size_t kRewardPatchSize = 6;
// 0F AF 05 abs32 (7) + original 89 45 disp8 store (3) + E9 rel32 (5).
// Any shorter form would clobber another register or depend on an unproved near allocation.
inline constexpr size_t kRewardStubSize = 15;

struct RewardHookSpec {
    RewardKind kind;
    uint32_t signatureRva;
    uint32_t siteRva;
    uint32_t immediateRva;
    uint32_t resumeRva;
    uint8_t storeDisplacement;
    std::array<uint8_t, kRewardSignatureSize> signature;
};

const RewardHookSpec& RewardHookSpecFor(RewardKind kind);
bool ValidateRewardSignature(const RewardHookSpec&, const uint8_t* bytes, size_t length);
bool EncodeRewardStub(
    const RewardHookSpec&,
    uintptr_t stubAddress,
    uintptr_t scalarAddress,
    uintptr_t resumeAddress,
    std::array<uint8_t, kRewardStubSize>* out);
bool EncodeRewardSiteJump(
    uintptr_t siteAddress,
    uintptr_t stubAddress,
    std::array<uint8_t, kRewardPatchSize>* out);
bool IsRewardMultiplierInRange(int value);
bool IsRewardWorstCaseSafe(RewardKind kind, int value);

enum class WriteEffect : uint8_t { NotTouched = 0, Verified, MayHaveChanged };
struct WriteResult { WriteEffect effect; };
struct ByteIo {
    void* context;
    bool (*read)(void*, uintptr_t, uint8_t*);
    WriteResult (*write)(void*, uintptr_t, uint8_t);
};

enum class OwnershipState : uint8_t { Unowned = 0, Owned, RestorePending, Conflict };
struct OwnedByte {
    uintptr_t address = 0;
    uint8_t original = 0;
    uint8_t desired = 0;
    uint8_t priorDesired = 0;
    bool captured = false;
    bool hasPriorDesired = false;
    OwnershipState state = OwnershipState::Unowned;
};

enum class ByteTransition : uint8_t {
    NoChange = 0,
    Applied,
    Reasserted,
    Restored,
    ReadFailed,
    WriteFailed,
    ReadbackFailed,
    RestorePending,
    Conflict,
};

ByteTransition UpdateOwnedByte(
    const ByteIo&, uintptr_t address, uint8_t desired, bool enabled, OwnedByte*);
ByteTransition RestoreOwnedByte(const ByteIo&, OwnedByte*);

struct RewardScalarIo {
    void* context = nullptr;
    bool (*exchange)(void*, int32_t) = nullptr;
    bool (*read)(void*, int32_t*) = nullptr;
};

enum class RewardBindingResult : uint8_t {
    Applied = 0,
    Disabled,
    InvalidConfiguration,
    HookDeferred,
    HookUnavailable,
    ScalarWriteFailed,
    ScalarReadbackFailed,
    GateFailed,
    Conflict,
};

RewardBindingResult UpdateRewardBinding(
    const RewardScalarIo& scalarIo,
    const ByteIo& gateIo,
    uintptr_t gateAddress,
    bool hookInstalled,
    bool configurationValid,
    int multiplier,
    bool enabled,
    OwnedByte* gateOwner,
    int32_t* appliedScalarOut);

enum class BundleState : uint8_t {
    Inactive = 0,
    Acquiring,
    Active,
    RestorePending,
    Conflict,
};
enum class MutationEffect : uint8_t { NotTouched = 0, Verified, MayHaveChanged };
struct MutationReport {
    MutationEffect effect = MutationEffect::NotTouched;
    // Advisory only: the portable transaction always performs its own readback. A callback
    // claim can classify ambiguity but can never release ownership or skip that read.
    bool readbackVerified = false;
};
// Restore provenance survives per-resource readback: a later candidate cannot regain write
// authority until the entire bundle has met its rollback obligations and releases ownership.
enum class ResourceRestorePhase : uint8_t {
    None = 0,
    MutationReadbackUnknown,
    OriginalConfirmed,
};
struct PatchSiteState {
    uintptr_t address = 0;
    std::array<uint8_t, 4> original{};
    bool possiblyOwned = false;
    bool conflictWitness = false;
    ResourceRestorePhase restorePhase = ResourceRestorePhase::None;
};
struct MaskedByteState {
    uintptr_t address = 0;
    uint8_t mask = 0;
    uint8_t originalMaskedBits = 0;
    bool possiblyOwned = false;
    bool conflictWitness = false;
    ResourceRestorePhase restorePhase = ResourceRestorePhase::None;
};
struct PatchProtectionToken {
    uintptr_t address = 0;
    size_t length = 0;
    uint32_t originalProtection = 0;
    bool active = false;
};
struct PatchIo {
    void* context = nullptr;
    bool (*read)(void*, uintptr_t, uint8_t*, size_t) = nullptr;
    bool (*beginCodeWrite)(void*, uintptr_t, size_t, PatchProtectionToken*) = nullptr;
    // A conforming adapter atomically, or under equivalent serialization, compares the full
    // expected byte sequence and writes desired only on equality. A read followed by an
    // unconditional write is not a conforming implementation of this callback.
    MutationReport (*writeIfEqual)(
        void*, uintptr_t, const uint8_t*, const uint8_t*, size_t) = nullptr;
    bool (*flush)(void*, uintptr_t, size_t) = nullptr;
    bool (*endCodeWrite)(void*, PatchProtectionToken*) = nullptr;
};

enum class RewardHookOwnership : uint8_t {
    Empty = 0,
    StubCleanupPending,
    StubReady,
    Installed,
    RestorePending,
    Conflict,
};

enum class RewardHookResult : uint8_t {
    Installed = 0,
    AlreadyInstalled,
    Removed,
    AlreadyRemoved,
    DeferredBattleActive,
    SignatureMismatch,
    Failed,
    RestorePending,
    Conflict,
};

RewardBindingResult RewardBindingResultFromHook(RewardHookResult result);

struct RewardHookState {
    RewardKind kind = RewardKind::Ap;
    uintptr_t moduleBase = 0;
    uintptr_t signatureAddress = 0;
    uintptr_t siteAddress = 0;
    uintptr_t resumeAddress = 0;
    uintptr_t scalarAddress = 0;
    uintptr_t stubAddress = 0;
    std::array<uint8_t, kRewardPatchSize> original{};
    std::array<uint8_t, kRewardPatchSize> jump{};
    std::array<uint8_t, kRewardStubSize> stub{};
    PatchProtectionToken protection{};
    RewardHookOwnership state = RewardHookOwnership::Empty;
    bool stubOwned = false;
    // StubReady is publishable only while this sticky invariant is true. A retained allocation
    // from any failed prepublication stage is cleanup-only and can never regain this bit.
    bool stubVerifiedRx = false;
    bool siteMayPointToStub = false;
};

struct RewardHookIo {
    void* context = nullptr;
    bool (*read)(void*, uintptr_t, uint8_t*, size_t) = nullptr;
    // Allocation is RW/NX. The only permitted transition after emission is RX/RO.
    bool (*allocateWritable)(void*, size_t, uintptr_t*) = nullptr;
    MutationReport (*writeWritable)(void*, uintptr_t, const uint8_t*, size_t) = nullptr;
    bool (*protectExecuteRead)(void*, uintptr_t, size_t) = nullptr;
    // The outer Present sample admits slow preparation; this immediate sample closes the race
    // between stub work and each executable-site mutation.
    bool (*confirmBattleInactive)(void*) = nullptr;
    bool (*beginCodeWrite)(void*, uintptr_t, size_t, PatchProtectionToken*) = nullptr;
    MutationReport (*writeIfEqual)(
        void*, uintptr_t, const uint8_t*, const uint8_t*, size_t) = nullptr;
    bool (*flush)(void*, uintptr_t, size_t) = nullptr;
    bool (*endCodeWrite)(void*, PatchProtectionToken*) = nullptr;
    bool (*freeAllocation)(void*, uintptr_t, size_t) = nullptr;
};

bool PrepareRewardHookState(
    RewardKind kind, uintptr_t moduleBase, uintptr_t scalarAddress, RewardHookState* state);
RewardHookResult InstallRewardHook(
    const RewardHookIo&, bool battleActive, RewardHookState* state);
// This is a normal-context primitive only. DllMain/process detach must never call it.
RewardHookResult RemoveRewardHook(
    const RewardHookIo&, bool battleActive, RewardHookState* state);
bool CanRunRewardHookTransaction(
    const RewardHookState* states, size_t count, size_t candidateIndex);
bool IsRewardPriorProtectionAdmitted(bool executable, bool writable);
struct SeymourBundleState {
    // Ownership is released as one bundle only after party/code readbacks, the combined code
    // flush, and original protection restoration are all confirmed.
    BundleState state = BundleState::Inactive;
    std::array<PatchSiteState, 2> sites{};
    MaskedByteState party{};
    PatchProtectionToken protection{};
};
enum class BundleResult : uint8_t {
    Applied = 0,
    Restored,
    AlreadyInState,
    PreflightFailed,
    SignatureMismatch,
    ApplyFailedRolledBack,
    RestorePending,
    Conflict,
};

// This portable Seymour transaction is RT0-only and has no production invoker. Promotion needs
// separately authorized RT2 safe-point proof covering both recovered execution-site xrefs.
BundleResult ApplySeymourBundle(const PatchIo&, SeymourBundleState*);
BundleResult RestoreSeymourBundle(const PatchIo&, SeymourBundleState*);

struct TickGate {
    uint32_t lastTickMs = 0;
    bool armed = false;
    bool inTick = false;
    bool hasTicked = false;
};
bool ArmTickGate(TickGate*);
bool TryBeginTick(TickGate*, uint32_t nowMs, uint32_t intervalMs);
void EndTick(TickGate*);
bool DisarmTickGate(TickGate*);

enum class RuntimeLifecycle : uint32_t { Stopped = 0, Running, Stopping };
// TerminalFailure is sticky; Ready may publish only while producer state remains Unknown.
enum class ProducerState : uint32_t { Unknown = 0, Ready, TerminalFailure };

enum class PresentHookPhysicalState : uint32_t { Idle = 0, Installing, Ready };
enum class PresentHookResult : uint8_t { None = 0, Ready, PublishTerminal };
struct AtomicPresentHookArbiter {
    // Physical Present installation is intentionally separate from the once-only UnX
    // terminal latch so a later Aurora/F7 hook may still install after UnX stops.
    std::atomic<uint32_t> word{static_cast<uint32_t>(PresentHookPhysicalState::Idle)};
};

struct AtomicLifecycle {
    std::atomic<uint32_t> state{static_cast<uint32_t>(RuntimeLifecycle::Stopped)};
    std::atomic<uint32_t> frameInFlight{0};
    std::atomic<uint32_t> producer{static_cast<uint32_t>(ProducerState::Unknown)};
#ifdef FFXHOOKS_TESTING
    void* testContext = nullptr;
    void (*afterProvisionalIncrementForTests)(void*) = nullptr;
#endif
};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
bool TryBeginPresentHookInstall(AtomicPresentHookArbiter*);
PresentHookResult CompletePresentHookInstall(AtomicPresentHookArbiter*, bool installed);
PresentHookResult RequestPresentHookTerminal(AtomicPresentHookArbiter*);
PresentHookPhysicalState ReadPresentHookPhysicalState(const AtomicPresentHookArbiter*);
bool ResetPresentHookPhysicalState(AtomicPresentHookArbiter*);
bool StartLifecycle(AtomicLifecycle*);
bool TryEnterFrame(AtomicLifecycle*);
// Each successful admission is one token whose caller must balance it with exactly one leave;
// the aggregate counter cannot identify or reject a cross-frame double leave.
void LeaveFrame(AtomicLifecycle*);
void RequestLifecycleStop(AtomicLifecycle*);
void PublishProducerState(AtomicLifecycle*, ProducerState);
ProducerState ReadProducerState(const AtomicLifecycle*);

// AP arrays cover battle slots 0..6 only. Seymour is party slot 7 and is deliberately excluded.
inline constexpr size_t kApSlotCount = 7;
struct ApSlotUpdate { uint8_t participation; uint8_t earn; };
bool HasSeededBattleParticipant(
    const std::array<uint8_t, kApSlotCount>& inParty,
    const std::array<uint8_t, kApSlotCount>& participation);
std::array<ApSlotUpdate, kApSlotCount> ComputeApUpdates(
    const std::array<uint8_t, kApSlotCount>& inParty,
    const std::array<uint8_t, kApSlotCount>& participation);

} // namespace FfxHooks::F8Runtime
