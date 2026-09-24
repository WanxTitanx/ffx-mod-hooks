#pragma once

#include "MinHookBatchCoordinator.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::SeymourBattle {

inline constexpr uint8_t kSeymourSlot = 7u;
inline constexpr uint8_t kAcceptedBattleDiscriminator = 7u;
inline constexpr size_t kStateListSize = 3u;
inline constexpr size_t kAbilityListSize = 17u;
inline constexpr size_t kCombinedListSize = kStateListSize + kAbilityListSize;
inline constexpr uintptr_t kBattleExitSyncReturnRva = 0x00390F07u;

// Admission is exact equality against SharedBattleRuntime::kBattleStateInitSceneReturnRva and
// kBattleExitSyncReturnRva; no nearby caller, generic InitScene invocation, or inferred battle
// state is allowed to enter roster behavior.
bool IsAdmittedEntryReturnRva(uintptr_t returnRva) noexcept;
bool IsAdmittedExitReturnRva(uintptr_t returnRva) noexcept;

struct RosterImage {
    uint8_t party = 0;
    std::array<uint8_t, kStateListSize> state{};
    std::array<uint8_t, kAbilityListSize> ability{};
};

enum class PreflightResult : uint8_t {
    Ready = 0,
    WrongDiscriminator,
    WrongPartyBaseline,
    SeymourAlreadyPresent,
    NoFreeAbilitySlot,
};

PreflightResult ValidatePreflight(const RosterImage&, uint8_t battleDiscriminator);
bool IsExactInitialApply(const RosterImage& baseline, const RosterImage& candidate);
bool IsConservativeOwnedImage(const RosterImage& baseline, const RosterImage& candidate);
bool IsConservativeExitOwnedImage(const RosterImage& baseline, const RosterImage& candidate);
bool IsConservativeRestoredImage(const RosterImage& baseline, const RosterImage& candidate);
uint32_t HashRoster(const RosterImage&);

struct Command {
    uint32_t generation = 0;
    bool requested = false;
    bool stopping = false;
};

struct AtomicCommandMailbox {
    std::atomic<uint32_t> word{1u << 2u};
};

Command ReadCommand(const AtomicCommandMailbox*);
bool PublishRequested(AtomicCommandMailbox*, bool requested);
void RequestStop(AtomicCommandMailbox*);

// Counting is distinct from exclusive behavior admission. Every detour callback takes a counted
// ticket as its first C++ action, while only one game-thread transaction may own roster behavior.
struct AtomicAdmission {
    std::atomic<uint32_t> accepting{0u};
    std::atomic<uint32_t> callbacks{0u};
    std::atomic<uint32_t> behaviorBusy{0u};
};

struct AdmissionTicket {
    bool counted = false;
    bool behaviorAdmitted = false;
};

void OpenAdmission(AtomicAdmission*);
void CloseAdmission(AtomicAdmission*);
AdmissionTicket EnterCallback(AtomicAdmission*);
void LeaveCallback(AtomicAdmission*, AdmissionTicket);
uint32_t ActiveCallbacks(const AtomicAdmission*);

enum class State : uint8_t {
    Off = 0,
    PendingBattle,
    AppliedBattleRoster,
    RestorePending,
    Unavailable,
    RejectedPreflight,
};

enum class ServiceOutcome : uint8_t {
    NoChange = 0,
    DeferredBattle,
    Applied,
    Restored,
    RejectedPreflight,
    ApplyFailedRolledBack,
    RestorePending,
    CommandSuperseded,
    CommandSupersededRolledBack,
    InvalidInput,
};

struct ServiceIo {
    void* context = nullptr;
    size_t stateLength = 0;
    size_t abilityLength = 0;
    bool (*read)(void*, RosterImage*) = nullptr;
    // Official AssignToFormation is int __cdecl(uint8_t slot, int active). Its return value is
    // not ownership evidence; exact 1+3+17-byte readback owns truth, so adapters expose only
    // whether the exact ABI call completed without a structured exception.
    bool (*assign)(void*, uint8_t slot, int active) = nullptr;
    bool (*commandCurrent)(void*, const Command*) = nullptr;
};

struct EntryOriginalIo {
    void* context = nullptr;
    int (*call)(void*) = nullptr;
};

struct ExitOriginalIo {
    void* context = nullptr;
    void (*call)(void*) = nullptr;
};

struct Ownership {
    State state = State::Off;
    uint32_t activeGeneration = 0;
    RosterImage baseline{};
    bool hasBaseline = false;
    bool battleRosterOwned = false;
    bool persistentMayContainSeymour = false;
};

enum class CallbackKind : uint8_t { None = 0, Entry, Exit };

struct Telemetry {
    State state = State::Off;
    ServiceOutcome outcome = ServiceOutcome::NoChange;
    uint32_t generation = 0;
    uint32_t threadId = 0;
    CallbackKind callback = CallbackKind::None;
    uint32_t returnAddress = 0;
    uint32_t battleDiscriminator = 0;
    uint32_t beforeHash = 0;
    uint32_t applyHash = 0;
    uint32_t restoreHash = 0;
};

struct AtomicTelemetryMailbox {
    std::atomic<uint32_t> epoch{0u};
    std::atomic<uint32_t> state{0u};
    std::atomic<uint32_t> outcome{0u};
    std::atomic<uint32_t> generation{0u};
    std::atomic<uint32_t> threadId{0u};
    std::atomic<uint32_t> callback{0u};
    std::atomic<uint32_t> returnAddress{0u};
    std::atomic<uint32_t> battleDiscriminator{0u};
    std::atomic<uint32_t> beforeHash{0u};
    std::atomic<uint32_t> applyHash{0u};
    std::atomic<uint32_t> restoreHash{0u};
};

void PublishTelemetry(AtomicTelemetryMailbox*, const Telemetry&);
bool ReadTelemetry(const AtomicTelemetryMailbox*, Telemetry*);

struct EntryServiceResult {
    ServiceOutcome outcome = ServiceOutcome::InvalidInput;
    int originalResult = 0;
};

EntryServiceResult ServiceEntry(
    const Command&, uint8_t battleDiscriminator, const ServiceIo&, const EntryOriginalIo&,
    Ownership*, Telemetry*);
ServiceOutcome ServiceExit(
    const Command&, const ServiceIo&, const ExitOriginalIo&, Ownership*, Telemetry*);

// The Present producer can publish before the delayed hook worker starts. Terminal is absorbing:
// a later Ready edge may never revive a producer whose owning Present path has already failed.
enum class ProducerPublication : uint32_t { Unknown = 0, Ready, Terminal };
enum class ProducerStartupDisposition : uint8_t {
    Pending = 0,
    Available,
    ProducerUnavailable,
};

ProducerPublication PublishProducerState(
    std::atomic<uint32_t>*, bool ready, bool terminalFailure);
ProducerStartupDisposition ClassifyProducerAtInstall(ProducerPublication);

// Normal-context teardown is an ordered ownership transaction: clear/restore exact roster
// ownership first, retire Seymour's exit target second, and unregister the borrowed F7 composer
// last. Each completed phase is retained so Busy/retry outcomes cannot discard ownership or
// repeat a proved lifecycle operation. Composer-conflict rollback arms this same machine with
// the exit owner only and remains RestorePending until exact retirement succeeds. Applied
// trampolines remain process-lifetime storage.
enum class TeardownPhase : uint8_t {
    Inactive = 0,
    ExitRetirement,
    ComposerUnregister,
    Complete,
};

enum class TeardownResult : uint8_t {
    InvalidArgument = 0,
    Inactive,
    OwnershipPending,
    ExitRetryRequired,
    ComposerRetryRequired,
    Complete,
};

struct TeardownMachine {
    TeardownPhase phase = TeardownPhase::Inactive;
    bool exitOwned = false;
    bool composerOwned = false;
};

struct TeardownIo {
    void* context = nullptr;
    bool (*retireExit)(void*) = nullptr;
    bool (*unregisterComposer)(void*) = nullptr;
};

bool ArmTeardown(TeardownMachine*, bool exitOwned, bool composerOwned);
TeardownResult AdvanceTeardown(TeardownMachine*, bool ownershipClear, const TeardownIo&);

struct ExitDetourIo {
    void* context = nullptr;
    bool (*create)(void*, uintptr_t target, void* detour, void** originalOut) = nullptr;
    // The process-lifetime trampoline must be release-published before QueueEnable/Apply can
    // expose the detour to another thread.
    bool (*publishOriginal)(void*, void* original) = nullptr;
};

struct ExitDetourOwner {
    uintptr_t target = 0u;
    void* original = nullptr;
    bool created = false;
    bool applyAttempted = false;
    bool mayHaveRun = false;
    bool active = false;
    bool retainedInert = false;
    bool coordinatorPoisoned = false;
};

enum class ExitDetourResult : uint8_t {
    Installed = 0,
    Removed,
    RetainedInert,
    InvalidArgument,
    AlreadyOwned,
    CreateFailed,
    PublicationFailed,
    RollbackFailed,
    EnableFailedRetained,
    CoordinatorNotReady,
    CoordinatorBusy,
    CoordinatorPoisoned,
    TeardownRetryRequired,
};

ExitDetourResult InstallExitDetour(
    const ExitDetourIo&, MinHookBatch::Coordinator*, const MinHookBatch::BatchIo&,
    MinHookBatch::NeutralizationFence, uintptr_t target, void* detour,
    ExitDetourOwner*);
ExitDetourResult RetireExitDetour(
    const ExitDetourIo&, MinHookBatch::Coordinator*, const MinHookBatch::BatchIo&,
    MinHookBatch::NeutralizationFence, ExitDetourOwner*);

static_assert(std::atomic<uint32_t>::is_always_lock_free);

} // namespace FfxHooks::SeymourBattle
