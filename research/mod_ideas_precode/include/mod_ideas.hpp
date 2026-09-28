#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mod_ideas {

// Pure reference rules. None of these functions installs a hook or writes a game file.
constexpr std::uint16_t EmptyAbility = 0x00FF;
constexpr std::uint8_t KnownElements = 0x1F;
constexpr std::uint8_t Fire = 0x01;
constexpr std::uint8_t Ice = 0x02;
constexpr std::uint8_t Thunder = 0x04;
constexpr std::uint8_t Water = 0x08;
constexpr std::uint8_t Holy = 0x10;

struct DamageCaps {
    std::int32_t normal;
    std::int32_t breakDamageLimit;
};

// The current PE also has command low-byte overrides: 0x80 forces the high
// vanilla cap and takes precedence over 0x40, which forces the low cap.
std::int32_t VanillaDamageCap(bool actorHasBreakDamageLimit,
                              std::uint8_t commandCapFlags);
std::int32_t CapPositiveDamage(std::int64_t damage, bool hasBreakDamageLimit,
                               DamageCaps caps);
bool WouldOverflowSigned32Total(std::int32_t damagePerHit, std::int32_t hitCount);
double DefenseMultiplier(std::int32_t defense);
double DamageWithBreakContribution(double baseDamage, std::int32_t defense,
                                   bool armorOrMentalBreak);

enum class MpZeroPolicy { UseCurrentMp, UseMaxMpPlusThree };
std::int32_t MagicBonusFromMp(std::int32_t currentMp, std::int32_t maxMp,
                             std::int32_t focusStacks, MpZeroPolicy policy);

double OutgoingEnergyMultiplier(std::int32_t minimumGaugeDuringAction,
                                std::int32_t gaugeMax, bool energyBoost,
                                bool energyBurst);
double IncomingEnergyMultiplier(std::int32_t minimumGaugeDuringAction,
                                std::int32_t gaugeMax, bool energyWall,
                                bool energyBarrier, bool ordinaryHpDamage);

enum class RoundingPolicy { Floor, Ceil, HalfToEven };
std::int32_t PercentOfMaximum(std::int32_t maximum, std::int32_t percent,
                              RoundingPolicy rounding);
std::int32_t PlanEfficiencyCost(std::int32_t baseCost, bool efficiency,
                                bool halfMpCost, bool isMpCost,
                                RoundingPolicy rounding);
std::int32_t PlanVampirismHeal(std::int32_t slainTargetMaxHp,
                               RoundingPolicy rounding);
std::int32_t PlanMpRegen(std::int32_t actorMaxMp, RoundingPolicy rounding);

enum class DamageChannel { Physical, Magical, Fixed, Fractional, Healing };
enum class TradeMode { None, PTrade, MTrade };
std::optional<double> TradeDamageMultiplier(TradeMode mode,
                                             DamageChannel channel);

struct EvasionPlan {
    std::int32_t effectiveEvasion;
    bool exceedsStatByte;
};
EvasionPlan PlanElude(std::int32_t baseEvasion, bool defending);

enum class CritBonusPolicy { PercentagePoints, RelativePercent };
struct CritChancePlan {
    double rawChancePercent;
    bool exceedsHundredPercent;
};
CritChancePlan PlanHeroBraveryCrit(std::int32_t baseChancePercent,
                                   bool equipped, CritBonusPolicy policy);

// The original forum line specifies elemental damage, not elemental healing.
// A multielemental hit matching the boost returns nullopt until its allocation
// rule is selected.
std::optional<double> ElementBoostMultiplier(std::uint8_t attackElements,
                                              std::uint8_t boostedElement,
                                              bool dealingDamage);

struct AffinityMasks {
    std::uint8_t weak = 0;
    std::uint8_t resist = 0;
    std::uint8_t null = 0;
    std::uint8_t absorb = 0;
};

enum class AffinityMode { Original, Favorable, Balanced, Unfavorable, ExtraMean };
std::int64_t RoundHalfToEven(double value);
std::int64_t ElementalDamage(std::int64_t damage, std::uint8_t elements,
                             AffinityMasks target, AffinityMode mode);
bool HasOppositeElementWeakness(std::uint8_t attackingElements,
                                std::uint8_t targetWeakness);

struct OverdriveGate {
    bool affordable;
    bool readyMenuFlag;
    bool accessibleAndAffordable;
};
OverdriveGate InspectOverdriveGate(std::int32_t charge, std::int32_t maximum,
                                  std::int32_t commandCost, bool readyMenuFlag);

enum class RankPolicy { FixedTwo, HalveFloorMinTwo, HalveCeilMinTwo };
struct QuickcastPlan {
    std::int32_t rank;
    std::int32_t mpCost;
    bool exceedsCommandCostByte;
};
QuickcastPlan PlanQuickcast(std::int32_t originalRank, std::int32_t originalMpCost,
                           RankPolicy policy);

struct FuryBudget {
    std::int32_t mpConsumed;
    std::int32_t rotationContribution;
    std::int32_t nominalMpForPower;
};
FuryBudget PlanFuryBudget(std::int32_t currentMp, std::int32_t maxMp,
                         std::int32_t rotations, std::int32_t mpPerRotation);

enum class RefreshPolicy { Replace, KeepLonger };
std::int32_t DurationAfterResistance(std::int32_t inflictedTurns,
                                     std::int32_t durationResistance);
std::int32_t RefreshStatusDuration(std::int32_t existingTurns,
                                   std::int32_t newTurns, RefreshPolicy policy);
bool CanThreatenAgain(bool alreadyInflictedOnTarget);

enum class GearKind : std::uint8_t { Weapon = 0, Armor = 1 };
struct Equipment {
    std::uint8_t owner = 0;
    GearKind kind = GearKind::Weapon;
    std::uint8_t flags = 0;
    std::uint8_t capacity = 0;
    std::uint16_t modelId = 0;
    std::array<std::uint16_t, 4> abilities{
        EmptyAbility, EmptyAbility, EmptyAbility, EmptyAbility};
};

using MasterGearBytes = std::array<std::uint8_t, 22>;
Equipment DecodeMasterGear(const MasterGearBytes& bytes);
MasterGearBytes EncodeMasterGear(const MasterGearBytes& original,
                                 const Equipment& equipment);

std::optional<Equipment> PlanReforge(const Equipment& original,
                                     std::uint8_t newOwner, GearKind newKind,
                                     std::uint16_t newModelId,
                                     bool modelBelongsToTargetCatalog);

struct AbilityTransfer {
    std::uint8_t sourceIndex;
    std::uint8_t destinationIndex;
};
struct MergePlan {
    Equipment destinationAfter;
    bool consumeSource;
};
std::optional<MergePlan> PlanMerge(const Equipment& destination,
                                  const Equipment& source,
                                  const std::vector<AbilityTransfer>& transfers,
                                  bool sameInventoryItem,
                                  bool allowDuplicateAbilities);

enum class SlotCostPolicy { OneSpherePerSlot, ProgressiveQuantity };
struct ExpandPlan {
    Equipment result;
    std::array<std::int32_t, 4> keySphereQuantities;
};
std::optional<ExpandPlan> PlanExpandSlots(const Equipment& original,
                                          std::uint8_t targetCapacity,
                                          SlotCostPolicy costPolicy);

struct ClearPlan {
    Equipment result;
    std::int32_t clearSpheresRequired;
};
std::optional<ClearPlan> PlanClearAbility(const Equipment& original,
                                          std::uint8_t slot,
                                          std::int32_t clearSphereCost);
std::optional<Equipment> PlanEvolveAbility(const Equipment& original,
                                           std::uint8_t slot,
                                           std::uint16_t expectedOld,
                                           std::uint16_t replacement,
                                           bool allowDuplicateAbilities);

struct PartyMember {
    bool available = false;
    bool blocked = false;
    bool noAp = false;
    bool ko = false;
    bool inFront = false;
    bool eligibleForReplacement = false;
};
bool EligibleForGuaranteedAp(const PartyMember& member);
std::optional<std::size_t> FirstAvailableBackline(
    const std::vector<PartyMember>& party, bool switchingAllowed);

}  // namespace mod_ideas
