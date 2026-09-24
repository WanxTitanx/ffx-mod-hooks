#pragma once

#include "CustomMixUltraCore.h"
#include "CustomMixWindowsAdapter.h"

#include <cstdint>

namespace FfxHooks::CustomMixUltra::Runtime {

constexpr std::uint64_t kRequestTtlMs = 30000u;

enum class StatusCode : std::uint8_t {
    Empty = 0,
    Full,
    Ready,
    Queued,
    Consumed,
    Failed,
    RestoreConflict,
    Expired,
    Unavailable,
};

const char* StatusName(StatusCode code) noexcept;

struct StatusSnapshot {
    StatusCode code = StatusCode::Unavailable;
    std::uint64_t generation = 0u;
    TransactionResult transactionResult = TransactionResult::NotArmed;
};

struct SelectionEditResult {
    bool accepted = false;
    SelectionInput selection{};
    SelectionOutcome preview{};
    StatusCode status = StatusCode::Empty;
};

StatusCode ClassifySelection(const SelectionInput& selection) noexcept;
SelectionEditResult TryAddChoice(
    const SelectionInput& selection, MonsterChoice choice) noexcept;
SelectionEditResult RemoveLastChoice(const SelectionInput& selection) noexcept;
SelectionEditResult ClearSelection() noexcept;

enum class CancelReason : std::uint8_t {
    Back = 0,
    Cancel,
    Close,
    FocusLoss,
    QueueFailure,
    ArmFailure,
    Timeout,
    ValidateOnly,
    Stop,
    NewGeneration,
    Unavailable,
};

struct PendingClaim {
    bool claimed = false;
    PendingRequest request{};
    Observation observation{};
};

// The production wrapper supplies a short SRW lock around this value-only state.
// No method calls vanilla, probes memory, waits, allocates, or performs I/O.
class RequestState {
public:
    void Start(bool exactProfileReady, bool validateOnly) noexcept;
    bool IsReady() const noexcept;
    bool RestoreConflictLatched() const noexcept;
    StatusSnapshot Status() const noexcept;
    void PublishSelection(const SelectionInput& selection) noexcept;
    bool Arm(const SelectionInput& selection, std::uint64_t nowTick) noexcept;
    PendingClaim Claim(std::uint64_t nowTick) noexcept;
    void Cancel(CancelReason reason) noexcept;
    void Tick(std::uint64_t nowTick) noexcept;
    void PublishTransaction(
        std::uint64_t generation, const TransactionOutcome& outcome) noexcept;
    void Stop() noexcept;
    void ResetAfterDrain() noexcept;

private:
    static std::uint64_t NextGeneration(std::uint64_t current) noexcept;
    void Publish(StatusCode code,
                 std::uint64_t generation,
                 TransactionResult result) noexcept;

    bool ready_ = false;
    bool restoreConflictLatched_ = false;
    std::uint64_t generationCounter_ = 0u;
    PendingRequest pending_{};
    StatusSnapshot status_{};
};

struct CarrierQueueResult {
    bool callSucceeded = false;
    std::int32_t returnValue = 0;
    bool queueArmed = false;
};

using QueueCarrierFn = CarrierQueueResult (*)(void* context) noexcept;
using ArmRequestFn = bool (*)(
    void* context, const SelectionInput& selection, std::uint64_t nowTick) noexcept;
using CancelRequestFn = void (*)(void* context, CancelReason reason) noexcept;

struct LaunchIo {
    void* context = nullptr;
    QueueCarrierFn queueCarrier = nullptr;
    ArmRequestFn armRequest = nullptr;
    CancelRequestFn cancelRequest = nullptr;
    bool (*prepareSelection)(void*, const SelectionInput&) = nullptr;
};

enum class LaunchCode : std::uint8_t {
    QueuedAndArmed = 0,
    InvalidSelection,
    InvalidIo,
    QueueFailed,
    ArmFailed,
    PreparationFailed,
};

struct LaunchOutcome {
    LaunchCode code = LaunchCode::InvalidIo;
    CarrierQueueResult queue{};
    bool carrierQueued = false;
    bool requestArmed = false;
};

LaunchOutcome QueueThenArm(
    const SelectionInput& selection,
    std::uint64_t nowTick,
    const LaunchIo& io) noexcept;

enum class EditorLaunchDisposition : std::uint8_t {
    ReopenPreservingSelection = 0,
    CloseWithArmedRequest,
    CloseWithVanillaCarrier,
};

struct EditorLaunchOutcome {
    LaunchOutcome launch{};
    EditorLaunchDisposition disposition =
        EditorLaunchDisposition::ReopenPreservingSelection;
    SelectionInput selection{};
};

// This value-only adapter owns the UI consequences of the queue-before-arm
// transaction so menu code cannot accidentally reopen or cancel the wrong branch.
EditorLaunchOutcome LaunchEditorSelection(
    const SelectionInput& selection,
    std::uint64_t nowTick,
    const LaunchIo& io) noexcept;

using MaterializeCarrierFn = bool (*)(
    void* context, CarrierView* carrierOut) noexcept;
using InvokeNestedInitSceneFn = bool (*)(
    void* context, int* originalResultOut);

struct CarrierMaterializeIo {
    void* context = nullptr;
    MaterializeCarrierFn materialize = nullptr;
};

struct NestedInitSceneIo {
    void* context = nullptr;
    InvokeNestedInitSceneFn invoke = nullptr;
};

struct BattleCompositionIo {
    CarrierMaterializeIo carrier{};
    NestedInitSceneIo nested{};
};

struct BattleCompositionOutcome {
    TransactionOutcome transaction{};
    int originalResult = 0;
    bool originalResultAvailable = false;
    bool carrierProbeAttempted = false;
    bool carrierProbeSucceeded = false;
    bool normalProgramUsed = false;
    const char* programSource = nullptr;
};

// ExecuteTransaction remains the sole mutation owner. Its bool callback wraps the
// complete SharedBattleRuntime run and carries the independent game int result out.
BattleCompositionOutcome RunBattleComposition(
    PendingRequest* request,
    const Observation& observation,
    const BattleCompositionIo& io) noexcept;

// Production lifecycle. Start accepts only F7 Difficulty's already-proved exact
// profile result; this module never creates or registers a hook or composer.
void StartProduction(
    std::uintptr_t imageBase,
    bool exactF7ProfileReady,
    bool validateOnly) noexcept;
bool ProductionOperational() noexcept;
bool ProductionPrepareSelection(const SelectionInput& selection) noexcept;
void ProductionClearPositionBattle() noexcept;
// Nonbattle InitScene transition: restore only the still-owned scenery selector.
// Compose OFF cancels future launches; a battle already launched retains its scene.
void ProductionLeaveBattle() noexcept;
void ProductionPositionSet(std::uintptr_t actor, int slot) noexcept;
// Called only after the native six-argument accessor; preserves its return and Y/W.
bool ProductionPositionRead(int originalResult, int setMode, std::uintptr_t actor,
                            int area, int role, int slot, float* output) noexcept;
StatusSnapshot ProductionStatus() noexcept;
void ProductionPublishSelection(const SelectionInput& selection) noexcept;
bool ProductionArmSelection(
    const SelectionInput& selection, std::uint64_t nowTick) noexcept;
void ProductionCancel(CancelReason reason) noexcept;
void ProductionTick(std::uint64_t nowTick) noexcept;
void ProductionRequestStop() noexcept;
void ProductionResetAfterDrain() noexcept;
BattleCompositionOutcome RunProductionBattle(
    const NestedInitSceneIo& nested, std::uint64_t nowTick) noexcept;

#if defined(FFXHOOKS_TESTING)
namespace Testing {

struct ProbeEvidence {
    WindowsAdapter::CarrierViewOutcome outcome{};
    std::uint32_t virtualQueryCalls = 0u;
};

// Runs the exact production typed-read + one-RegionInfo path against isolated x86
// allocations. No generic read callback or arbitrary RVA/width is exposed.
ProbeEvidence ProbeCarrier(std::uint64_t imageBase) noexcept;

}  // namespace Testing
#endif

}  // namespace FfxHooks::CustomMixUltra::Runtime
