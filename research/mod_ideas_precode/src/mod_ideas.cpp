#include "mod_ideas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mod_ideas {
namespace {

void CheckGauge(std::int32_t charge, std::int32_t maximum) {
    if (maximum <= 0 || charge < 0 || charge > maximum)
        throw std::invalid_argument("invalid Overdrive gauge");
}

void CheckElements(std::uint8_t elements, AffinityMasks target) {
    const auto unknown = static_cast<std::uint8_t>(
        elements | target.weak | target.resist | target.null | target.absorb);
    if ((unknown & static_cast<std::uint8_t>(~KnownElements)) != 0)
        throw std::invalid_argument("unsupported elemental bit");
}

int CountMatches(std::uint8_t left, std::uint8_t right) {
    int count = 0;
    for (std::uint8_t bit : {Fire, Ice, Thunder, Water, Holy})
        count += (left & right & bit) != 0;
    return count;
}

std::uint16_t ReadLe16(const MasterGearBytes& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(
        bytes[at] | (static_cast<std::uint16_t>(bytes[at + 1]) << 8));
}

void WriteLe16(MasterGearBytes& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void CheckEquipment(const Equipment& equipment) {
    if (equipment.capacity > 4)
        throw std::invalid_argument("equipment capacity exceeds four");
    if (equipment.kind != GearKind::Weapon && equipment.kind != GearKind::Armor)
        throw std::invalid_argument("unknown equipment kind");
}

}  // namespace

std::int32_t VanillaDamageCap(bool actorHasBreakDamageLimit,
                              std::uint8_t commandCapFlags) {
    std::int32_t cap = actorHasBreakDamageLimit ? 99999 : 9999;
    if ((commandCapFlags & 0x80) != 0)
        cap = 99999;
    else if ((commandCapFlags & 0x40) != 0)
        cap = 9999;
    return cap;
}

std::int32_t CapPositiveDamage(std::int64_t damage, bool hasBreakDamageLimit,
                               DamageCaps caps) {
    if (damage < 0 || caps.normal <= 0 || caps.breakDamageLimit <= 0)
        throw std::invalid_argument("positive HP damage and positive caps required");
    const auto cap = hasBreakDamageLimit ? caps.breakDamageLimit : caps.normal;
    return static_cast<std::int32_t>(std::min(damage, static_cast<std::int64_t>(cap)));
}

bool WouldOverflowSigned32Total(std::int32_t damagePerHit, std::int32_t hitCount) {
    if (damagePerHit < 0 || hitCount < 0)
        throw std::invalid_argument("negative hit inputs");
    return static_cast<std::int64_t>(damagePerHit) * hitCount >
           std::numeric_limits<std::int32_t>::max();
}

double DefenseMultiplier(std::int32_t defense) {
    if (defense < 0)
        throw std::invalid_argument("negative defense");
    return 1.0 / (1.0 + static_cast<double>(defense) * 0.05);
}

double DamageWithBreakContribution(double baseDamage, std::int32_t defense,
                                   bool armorOrMentalBreak) {
    if (!std::isfinite(baseDamage) || baseDamage < 0)
        throw std::invalid_argument("invalid base damage");
    const double mitigated = baseDamage * DefenseMultiplier(defense);
    return mitigated + (armorOrMentalBreak ? baseDamage * 0.25 : 0.0);
}

std::int32_t MagicBonusFromMp(std::int32_t currentMp, std::int32_t maxMp,
                             std::int32_t focusStacks, MpZeroPolicy policy) {
    if (currentMp < 0 || maxMp < 0 || currentMp > maxMp || focusStacks < 0)
        throw std::invalid_argument("invalid MP or Focus");
    if (policy != MpZeroPolicy::UseCurrentMp &&
        policy != MpZeroPolicy::UseMaxMpPlusThree)
        throw std::invalid_argument("unknown MP-zero policy");
    const std::int32_t effectiveMp =
        policy == MpZeroPolicy::UseMaxMpPlusThree ? maxMp : currentMp;
    std::int32_t bonus = 0;
    while (2LL * (bonus + 1) * (bonus + 1) <= effectiveMp)
        ++bonus;
    if (policy == MpZeroPolicy::UseMaxMpPlusThree)
        bonus += 3;
    if (focusStacks > std::numeric_limits<std::int32_t>::max() - bonus)
        throw std::overflow_error("Magic bonus exceeds int32");
    return bonus + focusStacks;
}

double OutgoingEnergyMultiplier(std::int32_t minimumGaugeDuringAction,
                                std::int32_t gaugeMax, bool energyBoost,
                                bool energyBurst) {
    CheckGauge(minimumGaugeDuringAction, gaugeMax);
    double multiplier = 1.0;
    if (energyBoost && 100LL * minimumGaugeDuringAction > 50LL * gaugeMax)
        multiplier += 0.25;
    if (energyBurst && 100LL * minimumGaugeDuringAction > 75LL * gaugeMax)
        multiplier += 0.40;
    return multiplier;
}

double IncomingEnergyMultiplier(std::int32_t minimumGaugeDuringAction,
                                std::int32_t gaugeMax, bool energyWall,
                                bool energyBarrier, bool ordinaryHpDamage) {
    CheckGauge(minimumGaugeDuringAction, gaugeMax);
    if (!ordinaryHpDamage)
        return 1.0;
    double multiplier = 1.0;
    if (energyWall && 100LL * minimumGaugeDuringAction > 50LL * gaugeMax)
        multiplier -= 0.20;
    if (energyBarrier && 100LL * minimumGaugeDuringAction > 75LL * gaugeMax)
        multiplier -= 0.30;
    return multiplier;
}

std::int32_t PercentOfMaximum(std::int32_t maximum, std::int32_t percent,
                              RoundingPolicy rounding) {
    if (maximum < 0 || percent < 0 || percent > 100)
        throw std::invalid_argument("invalid percentage input");
    const std::int64_t numerator = static_cast<std::int64_t>(maximum) * percent;
    std::int64_t result = numerator / 100;
    const std::int64_t remainder = numerator % 100;
    if (rounding == RoundingPolicy::Ceil && remainder != 0)
        ++result;
    else if (rounding == RoundingPolicy::HalfToEven &&
             (remainder > 50 || (remainder == 50 && result % 2 != 0)))
        ++result;
    else if (rounding != RoundingPolicy::Floor &&
             rounding != RoundingPolicy::Ceil &&
             rounding != RoundingPolicy::HalfToEven)
        throw std::invalid_argument("unknown rounding policy");
    return static_cast<std::int32_t>(result);
}

std::int32_t PlanEfficiencyCost(std::int32_t baseCost, bool efficiency,
                                bool halfMpCost, bool isMpCost,
                                RoundingPolicy rounding) {
    if (baseCost < 0 || baseCost > 255)
        throw std::invalid_argument("source command cost is not a byte");
    const std::int32_t reduction =
        (efficiency ? 25 : 0) + (isMpCost && halfMpCost ? 50 : 0);
    return PercentOfMaximum(baseCost, 100 - reduction, rounding);
}

std::int32_t PlanVampirismHeal(std::int32_t slainTargetMaxHp,
                               RoundingPolicy rounding) {
    return PercentOfMaximum(slainTargetMaxHp, 25, rounding);
}

std::int32_t PlanMpRegen(std::int32_t actorMaxMp, RoundingPolicy rounding) {
    return PercentOfMaximum(actorMaxMp, 2, rounding);
}

std::optional<double> TradeDamageMultiplier(TradeMode mode,
                                             DamageChannel channel) {
    if (mode == TradeMode::None)
        return 1.0;
    if (mode != TradeMode::PTrade && mode != TradeMode::MTrade)
        throw std::invalid_argument("unknown trade mode");
    if (channel == DamageChannel::Physical)
        return mode == TradeMode::PTrade ? 0.8 : 1.2;
    if (channel == DamageChannel::Magical)
        return mode == TradeMode::PTrade ? 1.2 : 0.8;
    return std::nullopt;
}

EvasionPlan PlanElude(std::int32_t baseEvasion, bool defending) {
    if (baseEvasion < 0 || baseEvasion > 255)
        throw std::invalid_argument("base Evasion is not a byte");
    const std::int32_t effective = baseEvasion + (defending ? 50 : 0);
    return {effective, effective > 255};
}

CritChancePlan PlanHeroBraveryCrit(std::int32_t baseChancePercent,
                                   bool equipped, CritBonusPolicy policy) {
    if (baseChancePercent < 0 || baseChancePercent > 100)
        throw std::invalid_argument("invalid base critical chance");
    if (policy != CritBonusPolicy::PercentagePoints &&
        policy != CritBonusPolicy::RelativePercent)
        throw std::invalid_argument("unknown critical bonus interpretation");
    double value = static_cast<double>(baseChancePercent);
    if (equipped)
        value = policy == CritBonusPolicy::PercentagePoints
                    ? value + 25.0
                    : value * 1.25;
    return {value, value > 100.0};
}

std::optional<double> ElementBoostMultiplier(std::uint8_t attackElements,
                                              std::uint8_t boostedElement,
                                              bool dealingDamage) {
    CheckElements(attackElements, AffinityMasks{boostedElement, 0, 0, 0});
    if (CountMatches(boostedElement, KnownElements) != 1)
        throw std::invalid_argument("boost must select one known element");
    if (!dealingDamage || (attackElements & boostedElement) == 0)
        return 1.0;
    if (CountMatches(attackElements, KnownElements) > 1)
        return std::nullopt;
    return 1.2;
}

std::int64_t RoundHalfToEven(double value) {
    if (!std::isfinite(value) ||
        value <= static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        value >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("value out of rounding range");
    const double lower = std::floor(value);
    const double fraction = value - lower;
    auto result = static_cast<std::int64_t>(lower);
    if (fraction > 0.5 || (fraction == 0.5 && result % 2 != 0))
        ++result;
    return result;
}

std::int64_t ElementalDamage(std::int64_t damage, std::uint8_t elements,
                             AffinityMasks target, AffinityMode mode) {
    CheckElements(elements, target);
    if (damage < 0 || damage > std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument("model accepts bounded positive HP damage only");
    if (elements == 0)
        return damage;
    if (mode == AffinityMode::Original)
        throw std::invalid_argument("vanilla result must come from original game function");
    const int weak = CountMatches(elements, target.weak);
    const int resist = CountMatches(elements, target.resist);
    const int absorbed = CountMatches(elements, target.absorb);
    const bool nulled = (elements & target.null) != 0;
    double result = static_cast<double>(damage);

    if (mode == AffinityMode::Balanced) {
        const int count = CountMatches(elements, KnownElements);
        result = 0.0;
        for (std::uint8_t bit : {Fire, Ice, Thunder, Water, Holy}) {
            if ((elements & bit) == 0)
                continue;
            const double part = static_cast<double>(damage) / count;
            if ((target.weak & bit) != 0)
                result += part * 1.5;
            else if ((target.resist & bit) != 0)
                result += part * 0.5;
            else if ((target.null & bit) != 0)
                continue;
            else if ((target.absorb & bit) != 0)
                result -= part;
            else
                result += part;
        }
        return RoundHalfToEven(result);
    }

    if (mode == AffinityMode::Favorable) {
        if (weak > 0) {
            for (int i = 0; i < weak; ++i)
                result *= 1.5;
            return RoundHalfToEven(result);
        }
        const auto adverse = static_cast<std::uint8_t>(
            target.resist | target.null | target.absorb);
        if ((elements & static_cast<std::uint8_t>(~adverse)) != 0)
            return damage;
        if (resist > 0)
            return RoundHalfToEven(result * 0.5);
        if (nulled)
            return 0;
        return absorbed > 0 ? -damage : damage;
    }

    if (mode == AffinityMode::Unfavorable) {
        if (absorbed > 0)
            return -damage;
        if (nulled)
            return 0;
        if (resist > 0) {
            for (int i = 0; i < resist; ++i)
                result *= 0.5;
            return RoundHalfToEven(result);
        }
        return weak > 0 ? RoundHalfToEven(result * 1.5) : damage;
    }

    if (mode == AffinityMode::ExtraMean) {
        if (absorbed > 0) {
            result = -result;
            for (int i = 0; i < weak; ++i)
                result *= 1.5;
            return RoundHalfToEven(result);
        }
        if (nulled)
            return 0;
        if (resist > 0) {
            for (int i = 0; i < resist; ++i)
                result *= 0.5;
            return RoundHalfToEven(result);
        }
        return weak > 0 ? RoundHalfToEven(result * 1.5) : damage;
    }
    throw std::invalid_argument("unknown affinity mode");
}

bool HasOppositeElementWeakness(std::uint8_t attackingElements,
                                std::uint8_t targetWeakness) {
    CheckElements(attackingElements, AffinityMasks{targetWeakness, 0, 0, 0});
    return ((attackingElements & Fire) && (targetWeakness & Ice)) ||
           ((attackingElements & Ice) && (targetWeakness & Fire)) ||
           ((attackingElements & Thunder) && (targetWeakness & Water)) ||
           ((attackingElements & Water) && (targetWeakness & Thunder));
}

OverdriveGate InspectOverdriveGate(std::int32_t charge, std::int32_t maximum,
                                  std::int32_t commandCost, bool readyMenuFlag) {
    CheckGauge(charge, maximum);
    if (commandCost < 0 || commandCost > maximum)
        throw std::invalid_argument("invalid command OD cost");
    const bool affordable = charge >= commandCost;
    return {affordable, readyMenuFlag, affordable && readyMenuFlag};
}

QuickcastPlan PlanQuickcast(std::int32_t originalRank,
                           std::int32_t originalMpCost, RankPolicy policy) {
    if (originalRank < 1 || originalMpCost < 0 || originalMpCost > 255)
        throw std::invalid_argument("invalid source command");
    std::int32_t rank = 2;
    if (policy == RankPolicy::HalveFloorMinTwo)
        rank = std::max(2, originalRank / 2);
    else if (policy == RankPolicy::HalveCeilMinTwo)
        rank = std::max(2, (originalRank + 1) / 2);
    else if (policy != RankPolicy::FixedTwo)
        throw std::invalid_argument("unknown Quickcast rank policy");
    const std::int32_t cost = originalMpCost * 2;
    return {rank, cost, cost > 255};
}

FuryBudget PlanFuryBudget(std::int32_t currentMp, std::int32_t maxMp,
                         std::int32_t rotations, std::int32_t mpPerRotation) {
    if (currentMp < 0 || maxMp < 0 || currentMp > maxMp ||
        rotations < 0 || mpPerRotation < 0)
        throw std::invalid_argument("invalid Fury input");
    const std::int64_t contribution =
        static_cast<std::int64_t>(rotations) * mpPerRotation;
    const std::int64_t nominal = currentMp + contribution;
    if (nominal > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("Fury nominal MP exceeds int32");
    return {currentMp, static_cast<std::int32_t>(contribution),
            static_cast<std::int32_t>(nominal)};
}

std::int32_t DurationAfterResistance(std::int32_t inflictedTurns,
                                     std::int32_t durationResistance) {
    if (inflictedTurns <= 0 || durationResistance < 0)
        throw std::invalid_argument("invalid status duration");
    return static_cast<std::int32_t>(
        std::max<std::int64_t>(1, static_cast<std::int64_t>(inflictedTurns) -
                                     durationResistance));
}

std::int32_t RefreshStatusDuration(std::int32_t existingTurns,
                                   std::int32_t newTurns, RefreshPolicy policy) {
    if (existingTurns < 0 || newTurns <= 0)
        throw std::invalid_argument("invalid refresh duration");
    if (policy == RefreshPolicy::Replace)
        return newTurns;
    if (policy == RefreshPolicy::KeepLonger)
        return std::max(existingTurns, newTurns);
    throw std::invalid_argument("unknown refresh policy");
}

bool CanThreatenAgain(bool alreadyInflictedOnTarget) {
    return !alreadyInflictedOnTarget;
}

Equipment DecodeMasterGear(const MasterGearBytes& bytes) {
    Equipment result;
    result.flags = bytes[0x03];
    result.owner = bytes[0x04];
    result.kind = static_cast<GearKind>(bytes[0x05]);
    result.capacity = bytes[0x0B];
    result.modelId = ReadLe16(bytes, 0x0C);
    for (std::size_t i = 0; i < 4; ++i)
        result.abilities[i] = ReadLe16(bytes, 0x0E + 2 * i);
    CheckEquipment(result);
    return result;
}

MasterGearBytes EncodeMasterGear(const MasterGearBytes& original,
                                 const Equipment& equipment) {
    CheckEquipment(equipment);
    MasterGearBytes bytes = original;
    bytes[0x03] = equipment.flags;
    bytes[0x04] = equipment.owner;
    bytes[0x05] = static_cast<std::uint8_t>(equipment.kind);
    bytes[0x0B] = equipment.capacity;
    WriteLe16(bytes, 0x0C, equipment.modelId);
    for (std::size_t i = 0; i < 4; ++i)
        WriteLe16(bytes, 0x0E + 2 * i, equipment.abilities[i]);
    return bytes;
}

std::optional<Equipment> PlanReforge(const Equipment& original,
                                     std::uint8_t newOwner, GearKind newKind,
                                     std::uint16_t newModelId,
                                     bool modelBelongsToTargetCatalog) {
    CheckEquipment(original);
    if (!modelBelongsToTargetCatalog ||
        (newKind != GearKind::Weapon && newKind != GearKind::Armor))
        return std::nullopt;
    Equipment result = original;
    result.owner = newOwner;
    result.kind = newKind;
    result.modelId = newModelId;
    return result;
}

std::optional<MergePlan> PlanMerge(const Equipment& destination,
                                  const Equipment& source,
                                  const std::vector<AbilityTransfer>& transfers,
                                  bool sameInventoryItem,
                                  bool allowDuplicateAbilities) {
    CheckEquipment(destination);
    CheckEquipment(source);
    if (sameInventoryItem || transfers.empty() || transfers.size() > 2 ||
        (source.flags & 0x0C) != 0)
        return std::nullopt;
    Equipment result = destination;
    std::array<bool, 4> usedDestinations{};
    std::array<bool, 4> usedSources{};
    for (AbilityTransfer transfer : transfers) {
        const auto src = transfer.sourceIndex;
        const auto dst = transfer.destinationIndex;
        if (src >= source.capacity || dst >= destination.capacity ||
            usedDestinations[dst] || usedSources[src] ||
            source.abilities[src] == EmptyAbility)
            return std::nullopt;
        const auto incoming = source.abilities[src];
        if (!allowDuplicateAbilities) {
            for (std::size_t i = 0; i < destination.capacity; ++i) {
                if (i != dst && result.abilities[i] == incoming)
                    return std::nullopt;
            }
        }
        result.abilities[dst] = incoming;
        usedDestinations[dst] = true;
        usedSources[src] = true;
    }
    return MergePlan{result, true};
}

std::optional<ExpandPlan> PlanExpandSlots(const Equipment& original,
                                          std::uint8_t targetCapacity,
                                          SlotCostPolicy costPolicy) {
    CheckEquipment(original);
    if (targetCapacity < original.capacity || targetCapacity > 4)
        return std::nullopt;
    if (costPolicy != SlotCostPolicy::OneSpherePerSlot &&
        costPolicy != SlotCostPolicy::ProgressiveQuantity)
        throw std::invalid_argument("unknown slot-cost policy");
    ExpandPlan result{original, {0, 0, 0, 0}};
    for (std::int32_t slot = original.capacity + 1; slot <= targetCapacity; ++slot)
        result.keySphereQuantities[slot - 1] =
            costPolicy == SlotCostPolicy::OneSpherePerSlot ? 1 : slot;
    result.result.capacity = targetCapacity;
    return result;
}

std::optional<ClearPlan> PlanClearAbility(const Equipment& original,
                                          std::uint8_t slot,
                                          std::int32_t clearSphereCost) {
    CheckEquipment(original);
    if (slot >= original.capacity || clearSphereCost <= 0 ||
        original.abilities[slot] == EmptyAbility)
        return std::nullopt;
    Equipment result = original;
    result.abilities[slot] = EmptyAbility;
    return ClearPlan{result, clearSphereCost};
}

std::optional<Equipment> PlanEvolveAbility(const Equipment& original,
                                           std::uint8_t slot,
                                           std::uint16_t expectedOld,
                                           std::uint16_t replacement,
                                           bool allowDuplicateAbilities) {
    CheckEquipment(original);
    if (slot >= original.capacity || expectedOld == EmptyAbility ||
        replacement == EmptyAbility || expectedOld == replacement ||
        original.abilities[slot] != expectedOld)
        return std::nullopt;
    if (!allowDuplicateAbilities) {
        for (std::size_t i = 0; i < original.capacity; ++i) {
            if (i != slot && original.abilities[i] == replacement)
                return std::nullopt;
        }
    }
    Equipment result = original;
    result.abilities[slot] = replacement;
    return result;
}

bool EligibleForGuaranteedAp(const PartyMember& member) {
    return member.available && !member.blocked && !member.noAp;
}

std::optional<std::size_t> FirstAvailableBackline(
    const std::vector<PartyMember>& party, bool switchingAllowed) {
    if (!switchingAllowed)
        return std::nullopt;
    for (std::size_t i = 0; i < party.size(); ++i) {
        const PartyMember& member = party[i];
        if (member.available && !member.blocked && !member.inFront &&
            member.eligibleForReplacement)
            return i;
    }
    return std::nullopt;
}

}  // namespace mod_ideas
