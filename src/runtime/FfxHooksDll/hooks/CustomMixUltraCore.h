#pragma once

#include "ArenaPositionLayout.h"
#include "ArenaScenery.h"
#include "ArenaMonsterCatalog.h"
#include "ArenaSoundtrack.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::CustomMixUltra {

constexpr std::size_t kMonsterSlotCount = 8u;
constexpr std::size_t kFormationSlotBytes = 16u;
constexpr std::uint16_t kEmptyMonsterId = 0xFFFFu;

struct SelectionInput {
    std::uint8_t activationCount = 0u;
    std::array<MonsterChoice, kMonsterSlotCount> activations{};
    ArenaPositions::Layout positions{};
    ArenaScenery::Choice scenery = ArenaScenery::Choice::Carrier;
    ArenaScenery::Camera camera = ArenaScenery::Camera::Arena;
    std::uint16_t musicTrack = 145u;
};

struct ExpandedSelection {
    std::uint8_t monsterCount = 0u;
    std::array<std::uint16_t, kMonsterSlotCount> monsterIds{};
};

enum class SelectionResult : std::uint8_t {
    Ready = 0,
    Empty,
    TooManyActivations,
    InvalidChoice,
    ExpandedCapacityExceeded,
};

struct SelectionOutcome {
    SelectionResult result = SelectionResult::Empty;
    ExpandedSelection expanded{};
};

// Pure closed-model expansion. Repeated choices are emitted contiguously while the
// order in which each distinct choice first appeared remains stable. ExecuteTransaction
// rebuilds this value and never accepts caller-supplied expanded IDs as writer input.
SelectionOutcome BuildSelection(const SelectionInput& input) noexcept;

// Offline RE profile: FFX.exe PE32/I386, SHA-256
// 78CE34397DA5E6F49B72C2AEBADEDAF4CD3F6720E1949D46A1B8ED67D3DB5CED.
// KEY: dome02_00 is a vanilla, one-area carrier with eight live monster positions.
// The runtime adapter must still signature/profile-gate the executable before using this core.
constexpr std::uint32_t kCarrierEncounterToken = 0x02050000u;
constexpr std::size_t kCarrierSize = 0x4428u;
constexpr std::size_t kCarrierChunk2Offset = 0x3F6Cu;
constexpr std::size_t kCarrierChunk2Length = 0x1Cu;
constexpr std::size_t kCarrierChunk3Offset = 0x3F88u;
constexpr std::size_t kCarrierChunk3Length = 0x360u;
constexpr std::size_t kFormationSlotOffset = kCarrierChunk2Offset + 0x0Cu;

struct PendingRequest {
    // OFF is the zero-value contract. Arming and enabling are separate so a stale
    // producer cannot make a partially initialized request mutation-capable.
    bool enabled = false;
    bool armed = false;
    bool cancelled = false;
    std::uint64_t generation = 0u;
    std::uint64_t consumedGeneration = 0u;
    std::uint64_t deadlineTick = 0u;
    SelectionInput selection{};
    // Set only by ExecuteTransaction around the admitted original call. Runtime
    // adapters use this synchronous marker; rejected passthrough gains no authority.
    bool composing = false;
};

struct Observation {
    // Both values must use the adapter's same monotonic clock/generation domain.
    // The core never waits; it only rejects work that is already stale.
    std::uint64_t generation = 0u;
    std::uint64_t nowTick = 0u;
};

// Portable classification supplied by the future Windows adapter after its guarded
// page query. The pure core never probes OS memory and fails closed on the zero value.
enum class CarrierAccess : std::uint8_t {
    Invalid = 0,
    ReadWrite,
    ReadOnly,
    Guarded,
};

// The adapter supplies a borrowed view of the already loaded encounter buffer.
// The core neither owns nor replaces this pointer and performs no allocation or I/O.
struct CarrierView {
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0u;
    std::uint32_t encounterToken = 0u;
    const char* encounterName = nullptr;
    std::size_t encounterNameLength = 0u;
    CarrierAccess access = CarrierAccess::Invalid;
    // Optional, separately page/context-validated native uint16 selector. The adjacent
    // field-routing word is never owned. Null is mandatory for an unproved context.
    std::uint16_t* battlefieldId = nullptr;
};

enum class TransactionResult : std::uint8_t {
    NotArmed = 0,
    InvalidGeneration,
    GenerationReplay,
    Disabled,
    Cancelled,
    GenerationMismatch,
    InvalidDeadline,
    Expired,
    EmptySelection,
    TooManyActivations,
    InvalidSelectionChoice,
    ExpandedSelectionOverflow,
    OriginalUnavailable,
    InvalidCarrierPointer,
    InvalidCarrierAccess,
    CarrierGuarded,
    CarrierReadOnly,
    InvalidCarrierIdentity,
    InvalidCarrierSize,
    InvalidCarrierMetadata,
    CarrierSlotConflict,
    OriginalRejected,
    OriginalThrew,
    Restored,
    RestoreConflict,
    InvalidPositions,
    PositionReferenceMismatch,
    InvalidScenery,
    BattlefieldUnavailable,
};

using InvokeOriginal = bool (*)(void* context);

struct BattlefieldLease {
    bool active = false;
    std::uint16_t before = 0u;
    std::uint16_t applied = 0u;
};
// Caller supplies the same validated selector, after draining native users. Foreign
// values survive; a native new-battle overwrite relinquishes ownership without restore.
bool RestoreBattlefield(BattlefieldLease* lease, std::uint16_t* field) noexcept;

struct TransactionOutcome {
    TransactionResult result = TransactionResult::NotArmed;
    bool patchApplied = false;
    std::uint8_t positionSlotsApplied = 0u;
    bool originalInvoked = false;
    bool originalAccepted = false;
    bool originalThrew = false;
    bool restored = false;
    BattlefieldLease battlefield{};
};

// ExecuteTransaction is deliberately the only mutation operation. A non-null original
// is called exactly once on every path: admitted calls observe the sixteen-byte
// selected formation and, only after explicit opt-in, up to eight X/Z float pairs.
// Rejected calls pass through with untouched carrier bytes.
// A null original is the sole uncallable, fail-closed exception. The runtime adapter must
// serialize PendingRequest access on its battle thread; this core owns no threads.
TransactionOutcome ExecuteTransaction(
    PendingRequest* request,
    const Observation& observation,
    const CarrierView& carrier,
    InvokeOriginal original,
    void* originalContext) noexcept;

}  // namespace FfxHooks::CustomMixUltra
