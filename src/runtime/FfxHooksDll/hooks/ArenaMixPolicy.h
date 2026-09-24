#pragma once

#include "CustomMixUltraCore.h"

namespace FfxHooks::ArenaMix {

struct Rules {
    std::uint16_t defeatedMask = 0u;
    bool bypass = false;
    // Zero is the Ultra editor; fixed mixes require exactly 3, 4, or 5 positions.
    std::uint8_t requiredSlots = 0u;
    std::uint64_t arenaKnownMask = 0u;
    std::uint64_t arenaUnlockedMask = 0u;
};

inline bool ValidCapacity(const Rules& rules) noexcept {
    return rules.requiredSlots == 0u ||
           (rules.requiredSlots >= 3u && rules.requiredSlots <= 5u);
}

inline bool ChoiceUnlocked(CustomMixUltra::MonsterChoice choice, const Rules& rules) noexcept {
    const unsigned index = static_cast<unsigned>(choice);
    const auto* entry = ArenaMonsters::Get(choice);
    if (!entry || !entry->count) return false;
    if (index < 8u) return rules.bypass || (rules.defeatedMask & (1u << index)) != 0u;
    // Normal Mix keeps its established Dark-only roster. Ultra shares the Mix
    // unlock; ordinary fiends do not acquire an additional capture requirement.
    if (rules.requiredSlots != 0u) return false;
    if (rules.bypass) return true;
    if (rules.defeatedMask == 0u) return false;
    if (entry->unlockIndex < 0) return true;
    const auto bit = std::uint64_t{1} << entry->unlockIndex;
    return (rules.arenaKnownMask & bit) != 0u && (rules.arenaUnlockedMask & bit) != 0u;
}

inline bool ChoicesUnlocked(const CustomMixUltra::SelectionInput& selection,
                            const Rules& rules) noexcept {
    if (selection.activationCount > selection.activations.size()) return false;
    for (std::uint8_t i = 0; i < selection.activationCount; ++i) {
        if (!ChoiceUnlocked(selection.activations[i], rules)) return false;
    }
    return true;
}

inline bool CanLaunch(const CustomMixUltra::SelectionInput& selection,
                      const Rules& rules) noexcept {
    if (!ValidCapacity(rules) || !ChoicesUnlocked(selection, rules) ||
        !ArenaScenery::Get(selection.scenery) || !ArenaScenery::ValidCamera(selection.camera)) return false;
    const auto expanded = CustomMixUltra::BuildSelection(selection);
    return expanded.result == CustomMixUltra::SelectionResult::Ready &&
           ArenaPositions::Validate(selection.positions, expanded.expanded.monsterCount) == ArenaPositions::Issue::None &&
           (rules.requiredSlots == 0u || expanded.expanded.monsterCount == rules.requiredSlots);
}

inline bool CanAdd(const CustomMixUltra::SelectionInput& selection,
                   CustomMixUltra::MonsterChoice choice, const Rules& rules) noexcept {
    if (!ValidCapacity(rules) || !ChoiceUnlocked(choice, rules) ||
        !ChoicesUnlocked(selection, rules) ||
        selection.activationCount >= selection.activations.size()) return false;
    auto candidate = selection;
    candidate.activations[candidate.activationCount++] = choice;
    const auto expanded = CustomMixUltra::BuildSelection(candidate);
    return expanded.result == CustomMixUltra::SelectionResult::Ready &&
           (rules.requiredSlots == 0u || expanded.expanded.monsterCount <= rules.requiredSlots);
}

} // namespace FfxHooks::ArenaMix
