#include "CustomMixUltraCore.h"

#include <array>
#include <cstring>
#include <cmath>

namespace FfxHooks::CustomMixUltra {
namespace {

constexpr std::array<char, 9> kCarrierName = {{
    'd', 'o', 'm', 'e', '0', '2', '_', '0', '0',
}};

constexpr std::array<std::uint32_t, 9> kCarrierHeader = {{
    8u,
    0x30u,
    0x3790u,
    static_cast<std::uint32_t>(kCarrierChunk2Offset),
    static_cast<std::uint32_t>(kCarrierChunk3Offset),
    0x42E8u,
    0u,
    0x4388u,
    static_cast<std::uint32_t>(kCarrierSize),
}};

constexpr std::array<std::uint8_t, 12> kChunk2Prefix = {{
    0x00u, 0x00u, 0x07u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
}};

constexpr std::array<std::uint16_t, kMonsterSlotCount> kVanillaFormation = {{
    0x1081u, 0x1092u, 0x1092u, 0x1092u,
    0x1092u, 0x1092u, 0x1092u, 0x1092u,
}};

using ChoiceExpansion = ArenaMonsters::Entry;

constexpr std::array<std::uint8_t, 16> kChunk3Prefix = {{
    0x00u, 0x01u, 0x01u, 0x04u, 0x07u, 0x07u, 0x08u, 0x00u,
    0x01u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
}};

constexpr std::array<std::uint32_t, 8> kChunk3Offsets = {{
    0x70u, 0xE0u, 0x150u, 0x1C0u,
    0x230u, 0x2B0u, 0x60u, 0x350u,
}};

static_assert(kCarrierChunk3Offset - kCarrierChunk2Offset == kCarrierChunk2Length,
              "the exact carrier header must bound chunk2 to 0x1C bytes");
static_assert(0x42E8u - kCarrierChunk3Offset == kCarrierChunk3Length,
              "the exact carrier header must bound chunk3 to 0x360 bytes");
static_assert(kFormationSlotOffset + kFormationSlotBytes == kCarrierChunk3Offset,
              "the sixteen-byte formation window must end exactly at chunk3");

std::uint16_t ReadU16(const std::uint8_t* bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
}

std::uint32_t ReadU32(const std::uint8_t* bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

void PutU16(std::array<std::uint8_t, kFormationSlotBytes>* bytes,
            std::size_t offset,
            std::uint16_t value) noexcept {
    (*bytes)[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

template <std::size_t Size>
bool MatchesBytes(const std::uint8_t* bytes,
                  std::size_t offset,
                  const std::array<std::uint8_t, Size>& expected) noexcept {
    return std::memcmp(bytes + offset, expected.data(), expected.size()) == 0;
}

bool HasExactIdentity(const CarrierView& carrier) noexcept {
    return carrier.encounterToken == kCarrierEncounterToken &&
           carrier.encounterName != nullptr &&
           carrier.encounterNameLength == kCarrierName.size() &&
           std::memcmp(carrier.encounterName, kCarrierName.data(), kCarrierName.size()) == 0;
}

bool HasExactHeader(const CarrierView& carrier) noexcept {
    for (std::size_t index = 0u; index < kCarrierHeader.size(); ++index) {
        if (ReadU32(carrier.bytes, index * sizeof(std::uint32_t)) !=
            kCarrierHeader[index]) {
            return false;
        }
    }
    return true;
}

bool HasExactCarrierMetadata(const CarrierView& carrier) noexcept {
    if (!HasExactHeader(carrier)) return false;
    if (!MatchesBytes(carrier.bytes, kCarrierChunk2Offset, kChunk2Prefix)) return false;
    if (!MatchesBytes(carrier.bytes, kCarrierChunk3Offset, kChunk3Prefix)) return false;

    for (std::size_t index = 0u; index < kChunk3Offsets.size(); ++index) {
        if (ReadU32(carrier.bytes,
                    kCarrierChunk3Offset + 0x10u + index * sizeof(std::uint32_t)) !=
            kChunk3Offsets[index]) {
            return false;
        }
    }
    return true;
}

bool HasVanillaFormation(const CarrierView& carrier) noexcept {
    for (std::size_t slot = 0u; slot < kVanillaFormation.size(); ++slot) {
        if (ReadU16(carrier.bytes,
                    kFormationSlotOffset + slot * sizeof(std::uint16_t)) !=
            kVanillaFormation[slot]) {
            return false;
        }
    }
    return true;
}

std::array<std::uint8_t, kFormationSlotBytes> BuildCandidate(
    const ExpandedSelection& selection) noexcept {
    std::array<std::uint8_t, kFormationSlotBytes> candidate{};
    candidate.fill(0xFFu);
    for (std::size_t slot = 0u; slot < selection.monsterCount; ++slot) {
        PutU16(&candidate, slot * sizeof(std::uint16_t), selection.monsterIds[slot]);
    }
    return candidate;
}

constexpr std::size_t kMonsterPositions = kCarrierChunk3Offset + 0x230u;
constexpr std::size_t kPartyPositions = kCarrierChunk3Offset + 0x70u;
static_assert(sizeof(float) == 4u, "the supported PE32 carrier stores IEEE float32");
static_assert(kMonsterPositions + 8u * 16u == kCarrierChunk3Offset + 0x2B0u,
              "the monster anchor span must end at the staging array");

bool PositionReferenceMatches(const CarrierView& carrier, std::uint8_t count) noexcept {
    // The preview uses this exact vanilla party frame. Reject a changed frame instead
    // of applying a plausible-looking layout in an unrelated coordinate system.
    for (std::size_t i = 0; i < ArenaPositions::kPartyReference.size(); ++i) {
        float x = 0, z = 0;
        std::memcpy(&x, carrier.bytes + kPartyPositions + i * 16u, 4u);
        std::memcpy(&z, carrier.bytes + kPartyPositions + i * 16u + 8u, 4u);
        if (x != ArenaPositions::kPartyReference[i].x || z != ArenaPositions::kPartyReference[i].z)
            return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t component = 0; component < 4u; ++component) {
            float value = 0;
            std::memcpy(&value, carrier.bytes + kMonsterPositions + i * 16u + component * 4u, 4u);
            if (!std::isfinite(value) || std::fabs(value) > 4096.0f) return false;
        }
    }
    return true;
}

TransactionOutcome Reject(TransactionResult result) noexcept {
    TransactionOutcome outcome{};
    outcome.result = result;
    return outcome;
}

}  // namespace

bool RestoreBattlefield(BattlefieldLease* lease, std::uint16_t* field) noexcept {
    if (!lease || !lease->active) return true;
    if (!field) return false;
    const bool owned = *field == lease->applied;
    if (owned) *field = lease->before;
    *lease = {};
    return owned;
}

SelectionOutcome BuildSelection(const SelectionInput& input) noexcept {
    SelectionOutcome outcome{};
    if (input.activationCount == 0u) {
        outcome.result = SelectionResult::Empty;
        return outcome;
    }
    if (input.activationCount > input.activations.size()) {
        outcome.result = SelectionResult::TooManyActivations;
        return outcome;
    }

    std::array<MonsterChoice, kMonsterSlotCount> firstActivationOrder{};
    std::array<std::uint8_t, kMonsterSlotCount> repeatCounts{};
    std::size_t distinctCount = 0u;
    std::size_t expandedCount = 0u;

    for (std::size_t activation = 0u;
         activation < input.activationCount;
         ++activation) {
        const MonsterChoice choice = input.activations[activation];
        const auto* entry = ArenaMonsters::Get(choice);
        if (!entry || !entry->count || !ArenaSoundtrack::Get(input.musicTrack)) {
            outcome.result = SelectionResult::InvalidChoice;
            return outcome;
        }

        const ChoiceExpansion& expansion = *entry;
        if (expandedCount + expansion.count > kMonsterSlotCount) {
            outcome.result = SelectionResult::ExpandedCapacityExceeded;
            return outcome;
        }
        expandedCount += expansion.count;

        std::size_t group = 0u;
        while (group < distinctCount && firstActivationOrder[group] != choice) {
            ++group;
        }
        if (group == distinctCount) {
            firstActivationOrder[distinctCount] = choice;
            ++distinctCount;
        }
        ++repeatCounts[group];
    }

    outcome.expanded.monsterIds.fill(kEmptyMonsterId);
    std::size_t outputSlot = 0u;
    for (std::size_t group = 0u; group < distinctCount; ++group) {
        const ChoiceExpansion& expansion =
            *ArenaMonsters::Get(firstActivationOrder[group]);
        for (std::size_t repeat = 0u; repeat < repeatCounts[group]; ++repeat) {
            for (std::size_t member = 0u; member < expansion.count; ++member) {
                outcome.expanded.monsterIds[outputSlot++] = expansion.monsterIds[member];
            }
        }
    }

    outcome.expanded.monsterCount = static_cast<std::uint8_t>(outputSlot);
    outcome.result = SelectionResult::Ready;
    return outcome;
}

TransactionOutcome ExecuteTransaction(
    PendingRequest* request,
    const Observation& observation,
    const CarrierView& carrier,
    InvokeOriginal original,
    void* originalContext) noexcept {
    if (request) request->composing = false;
    // WHY: without a callable original there is no safe vanilla continuation. Consume
    // any armed generation immediately so a later call cannot apply a stale patch.
    if (original == nullptr) {
        if (request != nullptr && request->armed) {
            request->armed = false;
            request->consumedGeneration = request->generation;
        }
        return Reject(TransactionResult::OriginalUnavailable);
    }

    // Every non-admitted path is a vanilla passthrough, not a swallowed battle call.
    // Keep the rejection reason primary while the orthogonal original fields report
    // false returns or exceptions. The lambda is stack-only and performs no allocation.
    const auto passthrough = [original, originalContext](
                                 TransactionResult reason) noexcept {
        TransactionOutcome outcome = Reject(reason);
        outcome.originalInvoked = true;
        try {
            outcome.originalAccepted = original(originalContext);
        } catch (...) {
            outcome.originalThrew = true;
        }
        return outcome;
    };

    if (request == nullptr || !request->armed) {
        return passthrough(TransactionResult::NotArmed);
    }

    // WHY: consumption happens before every admission branch. A malformed, cancelled,
    // expired, or mismatched request therefore cannot leak into a later battle call.
    request->armed = false;
    if (request->generation == 0u) {
        return passthrough(TransactionResult::InvalidGeneration);
    }
    if (request->generation == request->consumedGeneration) {
        return passthrough(TransactionResult::GenerationReplay);
    }
    request->consumedGeneration = request->generation;

    if (!request->enabled) return passthrough(TransactionResult::Disabled);
    if (request->cancelled) return passthrough(TransactionResult::Cancelled);
    if (observation.generation != request->generation) {
        return passthrough(TransactionResult::GenerationMismatch);
    }
    if (request->deadlineTick == 0u) {
        return passthrough(TransactionResult::InvalidDeadline);
    }
    if (observation.nowTick >= request->deadlineTick) {
        return passthrough(TransactionResult::Expired);
    }
    const SelectionOutcome selection = BuildSelection(request->selection);
    switch (selection.result) {
    case SelectionResult::Ready:
        break;
    case SelectionResult::Empty:
        return passthrough(TransactionResult::EmptySelection);
    case SelectionResult::TooManyActivations:
        return passthrough(TransactionResult::TooManyActivations);
    case SelectionResult::InvalidChoice:
        return passthrough(TransactionResult::InvalidSelectionChoice);
    case SelectionResult::ExpandedCapacityExceeded:
        return passthrough(TransactionResult::ExpandedSelectionOverflow);
    }
    if (ArenaPositions::Validate(request->selection.positions, selection.expanded.monsterCount) !=
        ArenaPositions::Issue::None) return passthrough(TransactionResult::InvalidPositions);
    const auto* scenery = ArenaScenery::Get(request->selection.scenery);
    if (!scenery || !ArenaScenery::ValidCamera(request->selection.camera))
        return passthrough(TransactionResult::InvalidScenery);
    if (scenery->battlefieldId && !carrier.battlefieldId)
        return passthrough(TransactionResult::BattlefieldUnavailable);
    if (carrier.bytes == nullptr) {
        return passthrough(TransactionResult::InvalidCarrierPointer);
    }
    switch (carrier.access) {
    case CarrierAccess::ReadWrite:
        break;
    case CarrierAccess::Invalid:
        return passthrough(TransactionResult::InvalidCarrierAccess);
    case CarrierAccess::Guarded:
        return passthrough(TransactionResult::CarrierGuarded);
    case CarrierAccess::ReadOnly:
        return passthrough(TransactionResult::CarrierReadOnly);
    default:
        return passthrough(TransactionResult::InvalidCarrierAccess);
    }
    if (!HasExactIdentity(carrier)) {
        return passthrough(TransactionResult::InvalidCarrierIdentity);
    }
    if (carrier.size != kCarrierSize) {
        return passthrough(TransactionResult::InvalidCarrierSize);
    }

    // KEY: exact size is checked before any fixed-offset read. Header endpoints then prove
    // both chunk lengths and keep chunk2+0x0C..+0x1B inside the borrowed buffer.
    if (!HasExactCarrierMetadata(carrier)) {
        return passthrough(TransactionResult::InvalidCarrierMetadata);
    }
    if (!HasVanillaFormation(carrier)) {
        return passthrough(TransactionResult::CarrierSlotConflict);
    }

    const auto& positions = request->selection.positions;
    if (positions.enabled && !PositionReferenceMatches(carrier, positions.count))
        return passthrough(TransactionResult::PositionReferenceMismatch);
    std::array<float, 16u> positionBefore{};
    std::array<float, 16u> positionCandidate{};
    const std::size_t positionCount = positions.enabled ? positions.count : 0u;
    for (std::size_t i = 0; i < positionCount; ++i) {
        std::memcpy(&positionBefore[i * 2u], carrier.bytes + kMonsterPositions + i * 16u, 4u);
        std::memcpy(&positionBefore[i * 2u + 1u], carrier.bytes + kMonsterPositions + i * 16u + 8u, 4u);
        positionCandidate[i * 2u] = positions.points[i].x;
        positionCandidate[i * 2u + 1u] = positions.points[i].z;
    }

    std::array<std::uint8_t, kFormationSlotBytes> snapshot{};
    std::memcpy(snapshot.data(), carrier.bytes + kFormationSlotOffset, snapshot.size());
    const std::array<std::uint8_t, kFormationSlotBytes> candidate =
        BuildCandidate(selection.expanded);
    std::memcpy(carrier.bytes + kFormationSlotOffset, candidate.data(), candidate.size());

    // X/Z only. Heights, rotations, party, camera and inactive slots are not owned.
    for (std::size_t i = 0; i < positionCount; ++i) {
        std::memcpy(carrier.bytes + kMonsterPositions + i * 16u, &positionCandidate[i * 2u], 4u);
        std::memcpy(carrier.bytes + kMonsterPositions + i * 16u + 8u, &positionCandidate[i * 2u + 1u], 4u);
    }
    TransactionOutcome outcome{};
    outcome.patchApplied = true;
    outcome.positionSlotsApplied = static_cast<std::uint8_t>(positionCount);
    if (scenery->battlefieldId) {
        outcome.battlefield = {true, *carrier.battlefieldId, scenery->battlefieldId};
        *carrier.battlefieldId = scenery->battlefieldId;
    }
    outcome.originalInvoked = true;
    request->composing = true;
    try {
        outcome.originalAccepted = original(originalContext);
    } catch (...) {
        outcome.originalThrew = true;
    }
    request->composing = false;

    // Restore every still-owned field even if a different field was changed by
    // another writer. A conflict remains sticky in RequestState; foreign bytes stay.
    bool conflict = std::memcmp(carrier.bytes + kFormationSlotOffset,
                                candidate.data(), candidate.size()) != 0;
    if (!conflict)
        std::memcpy(carrier.bytes + kFormationSlotOffset, snapshot.data(), snapshot.size());
    for (std::size_t i = 0; i < positionCount * 2u; ++i) {
        auto* field = carrier.bytes + kMonsterPositions + (i / 2u) * 16u + (i % 2u) * 8u;
        if (std::memcmp(field, &positionCandidate[i], 4u) == 0)
            std::memcpy(field, &positionBefore[i], 4u);
        else
            conflict = true;
    }
    if (outcome.battlefield.active) {
        if (*carrier.battlefieldId != outcome.battlefield.applied) {
            outcome.battlefield = {};
            conflict = true;
        } else if (conflict || outcome.originalThrew || !outcome.originalAccepted) {
            RestoreBattlefield(&outcome.battlefield, carrier.battlefieldId);
        }
        // Native InitScene reads this ID for its scene request, then its caller
        // reads it again for the resource load. Returning restores the formation,
        // but must leave this owned selector alive through that second consumer.
    }
    if (conflict) {
        outcome.result = TransactionResult::RestoreConflict;
        return outcome;
    }

    outcome.restored = true;
    if (outcome.originalThrew) {
        outcome.result = TransactionResult::OriginalThrew;
    } else if (!outcome.originalAccepted) {
        outcome.result = TransactionResult::OriginalRejected;
    } else {
        outcome.result = TransactionResult::Restored;
    }
    return outcome;
}

}  // namespace FfxHooks::CustomMixUltra
