#include "mod_ideas.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace mod_ideas;

namespace {

class Suite {
public:
    void Check(bool condition, std::string_view name) {
        ++count_;
        if (!condition)
            throw std::runtime_error(std::string(name));
    }

    void Near(double actual, double expected, std::string_view name) {
        Check(std::abs(actual - expected) < 0.000000001, name);
    }

    void ThrowsInvalid(const std::function<void()>& action,
                       std::string_view name) {
        try {
            action();
        } catch (const std::invalid_argument&) {
            Check(true, name);
            return;
        }
        Check(false, name);
    }

    int Count() const { return count_; }

private:
    int count_ = 0;
};

void TestDamage(Suite& t) {
    const DamageCaps caps{99999, 9999999};
    t.Check(VanillaDamageCap(false, 0) == 9999, "vanilla ordinary cap");
    t.Check(VanillaDamageCap(true, 0) == 99999, "vanilla BDL cap");
    t.Check(VanillaDamageCap(false, 0x80) == 99999,
            "command force-high cap");
    t.Check(VanillaDamageCap(true, 0x40) == 9999,
            "command force-low cap");
    t.Check(VanillaDamageCap(true, 0xC0) == 99999,
            "force-high takes precedence over force-low");
    t.Check(CapPositiveDamage(99998, false, caps) == 99998, "below normal cap");
    t.Check(CapPositiveDamage(100000, false, caps) == 99999, "normal cap");
    t.Check(CapPositiveDamage(100000, true, caps) == 100000, "BDL below cap");
    t.Check(CapPositiveDamage(20000000, true, caps) == 9999999, "BDL cap");
    t.ThrowsInvalid([&] { (void)CapPositiveDamage(-1, true, caps); },
                    "healing is outside positive cap model");
    t.Check(!WouldOverflowSigned32Total(9999999, 200), "200 capped hits fit int32");
    t.Check(WouldOverflowSigned32Total(9999999, 250), "250 capped hits overflow int32");
    t.Near(DefenseMultiplier(0), 1.0, "zero defense");
    t.Near(DefenseMultiplier(20), 0.5, "20 defense doubles EHP");
    t.Near(DefenseMultiplier(60), 0.25, "60 defense quarters damage");
    t.Near(DamageWithBreakContribution(100, 60, true), 50.0,
           "60 defense plus break contribution");
    t.Near(DamageWithBreakContribution(100, 20, true), 75.0,
           "20 defense plus break contribution");
    t.Near(DamageWithBreakContribution(100, 0, true), 125.0,
           "zero defense plus break contribution");
}

void TestMpAndEnergy(Suite& t) {
    t.Check(MagicBonusFromMp(0, 255, 0, MpZeroPolicy::UseCurrentMp) == 0,
            "zero MP bonus");
    t.Check(MagicBonusFromMp(7, 255, 0, MpZeroPolicy::UseCurrentMp) == 1,
            "MP 7 bonus");
    t.Check(MagicBonusFromMp(8, 255, 0, MpZeroPolicy::UseCurrentMp) == 2,
            "MP 8 bonus");
    t.Check(MagicBonusFromMp(241, 255, 0, MpZeroPolicy::UseCurrentMp) == 10,
            "MP 241 bonus");
    t.Check(MagicBonusFromMp(242, 255, 0, MpZeroPolicy::UseCurrentMp) == 11,
            "MP 242 bonus");
    t.Check(MagicBonusFromMp(2, 200, 1, MpZeroPolicy::UseMaxMpPlusThree) == 14,
            "MP-zero max MP plus three interpretation");
    t.Check(MagicBonusFromMp(200, 200, 0, MpZeroPolicy::UseMaxMpPlusThree) == 13,
            "MP-zero adds three atop the max-MP bonus");
    try {
        (void)MagicBonusFromMp(200, 200, 2147483647,
                               MpZeroPolicy::UseMaxMpPlusThree);
        t.Check(false, "MP bonus must reject int32 overflow");
    } catch (const std::overflow_error&) {
        t.Check(true, "MP bonus must reject int32 overflow");
    }
    t.ThrowsInvalid(
        [] { (void)MagicBonusFromMp(201, 200, 0, MpZeroPolicy::UseCurrentMp); },
        "current MP cannot exceed maximum");
    t.ThrowsInvalid(
        [] { (void)MagicBonusFromMp(2, 200, 0, static_cast<MpZeroPolicy>(99)); },
        "unknown MP-zero policy rejected");
    t.Near(OutgoingEnergyMultiplier(50, 100, true, true), 1.0,
           "strict 50 percent threshold");
    t.Near(OutgoingEnergyMultiplier(51, 100, true, true), 1.25,
           "Energy Boost threshold");
    t.Near(OutgoingEnergyMultiplier(75, 100, true, true), 1.25,
           "strict 75 percent threshold");
    t.Near(OutgoingEnergyMultiplier(76, 100, true, true), 1.65,
           "Boost and Burst additive");
    t.Near(IncomingEnergyMultiplier(76, 100, true, true, true), 0.5,
           "Wall and Barrier additive");
    t.Near(IncomingEnergyMultiplier(76, 100, true, true, false), 1.0,
           "fixed or healing hit excluded");
}

void TestEquipmentAutoAbilities(Suite& t) {
    t.Check(PercentOfMaximum(99, 2, RoundingPolicy::Floor) == 1,
            "2 percent floor");
    t.Check(PercentOfMaximum(99, 2, RoundingPolicy::Ceil) == 2,
            "2 percent ceil");
    t.Check(PercentOfMaximum(10, 25, RoundingPolicy::HalfToEven) == 2,
            "2.5 rounds to even 2");
    t.Check(PercentOfMaximum(14, 25, RoundingPolicy::HalfToEven) == 4,
            "3.5 rounds to even 4");
    t.Check(PlanEfficiencyCost(4, true, false, true, RoundingPolicy::Floor) == 3,
            "Efficiency leaves 75 percent MP cost");
    t.Check(PlanEfficiencyCost(4, true, true, true, RoundingPolicy::Floor) == 1,
            "Efficiency plus Half MP Cost leaves 25 percent");
    t.Check(PlanEfficiencyCost(5, true, true, true, RoundingPolicy::Ceil) == 2,
            "cost rounding policy remains explicit");
    t.Check(PlanEfficiencyCost(5, true, true, false, RoundingPolicy::Floor) == 3,
            "Half MP Cost does not reduce OD cost");
    t.Check(PlanVampirismHeal(101, RoundingPolicy::Floor) == 25,
            "Vampirism 25 percent floor");
    t.Check(PlanVampirismHeal(101, RoundingPolicy::Ceil) == 26,
            "Vampirism 25 percent ceil");
    t.Check(PlanMpRegen(1000, RoundingPolicy::Floor) == 20,
            "MP Regen two percent");
    t.ThrowsInvalid(
        [] { (void)PercentOfMaximum(10, 101, RoundingPolicy::Floor); },
        "invalid percent rejected");

    const auto pPhysical =
        TradeDamageMultiplier(TradeMode::PTrade, DamageChannel::Physical);
    const auto pMagical =
        TradeDamageMultiplier(TradeMode::PTrade, DamageChannel::Magical);
    const auto mPhysical =
        TradeDamageMultiplier(TradeMode::MTrade, DamageChannel::Physical);
    const auto mMagical =
        TradeDamageMultiplier(TradeMode::MTrade, DamageChannel::Magical);
    t.Check(pPhysical.has_value() && pMagical.has_value() &&
                mPhysical.has_value() && mMagical.has_value(),
            "P/M Trade classifies physical and magical hits");
    t.Near(*pPhysical, 0.8, "P-Trade physical");
    t.Near(*pMagical, 1.2, "P-Trade magical");
    t.Near(*mPhysical, 1.2, "M-Trade physical");
    t.Near(*mMagical, 0.8, "M-Trade magical");
    t.Check(!TradeDamageMultiplier(TradeMode::PTrade, DamageChannel::Fixed),
            "fixed hit remains unclassified for Trade");

    const EvasionPlan elude = PlanElude(220, true);
    t.Check(elude.effectiveEvasion == 270 && elude.exceedsStatByte,
            "Elude exposes u8 overflow risk");
    t.Check(PlanElude(220, false).effectiveEvasion == 220,
            "Elude inactive outside Defend");
    const CritChancePlan points =
        PlanHeroBraveryCrit(20, true, CritBonusPolicy::PercentagePoints);
    const CritChancePlan relative =
        PlanHeroBraveryCrit(20, true, CritBonusPolicy::RelativePercent);
    t.Near(points.rawChancePercent, 45.0,
           "Hero's Bravery percentage-point interpretation");
    t.Near(relative.rawChancePercent, 25.0,
           "Hero's Bravery relative-percent interpretation");
    t.Check(PlanHeroBraveryCrit(90, true, CritBonusPolicy::PercentagePoints)
                .exceedsHundredPercent,
            "Hero's Bravery can exceed 100 percent before engine cap");

    const auto fireDamage = ElementBoostMultiplier(Fire, Fire, true);
    const auto fireHealing = ElementBoostMultiplier(Fire, Fire, false);
    const auto wrongElement = ElementBoostMultiplier(Ice, Fire, true);
    t.Check(fireDamage.has_value() && fireHealing.has_value() &&
                wrongElement.has_value(),
            "single-element boost plans have explicit values");
    t.Near(*fireDamage, 1.2, "Element Boost damage x1.2");
    t.Near(*fireHealing, 1.0, "Element Boost original line does not cover healing");
    t.Near(*wrongElement, 1.0, "different element does not receive boost");
    t.Check(!ElementBoostMultiplier(
                static_cast<std::uint8_t>(Fire | Ice), Fire, true),
            "multielemental boost awaits allocation rule");
}

void TestElements(Suite& t) {
    const AffinityMasks mixed{Fire, Ice, 0, 0};
    const auto pair = static_cast<std::uint8_t>(Fire | Ice);
    t.Check(ElementalDamage(100, pair, mixed, AffinityMode::Balanced) == 100,
            "balanced divides two elemental parts");
    t.Check(ElementalDamage(100, pair, mixed, AffinityMode::Favorable) == 150,
            "favorable weakness short-circuits resistance");
    t.Check(ElementalDamage(100, pair, mixed, AffinityMode::Unfavorable) == 50,
            "unfavorable resistance short-circuits weakness");
    t.Check(ElementalDamage(100, pair, mixed, AffinityMode::ExtraMean) == 50,
            "extra mean resistance");
    const AffinityMasks absorbAndWeak{Ice, 0, 0, Fire};
    t.Check(ElementalDamage(100, pair, absorbAndWeak, AffinityMode::Balanced) == 25,
            "balanced absorption and weakness");
    t.Check(ElementalDamage(100, pair, absorbAndWeak, AffinityMode::Unfavorable) == -100,
            "unfavorable absorption");
    t.Check(ElementalDamage(100, pair, absorbAndWeak, AffinityMode::ExtraMean) == -150,
            "extra mean absorbed hit amplifies weakness");
    t.Check(ElementalDamage(100, pair, AffinityMasks{0, Ice, 0, 0},
                            AffinityMode::Favorable) == 100,
            "neutral element protects favorable hit from resistance");
    t.Check(ElementalDamage(3, Ice, AffinityMasks{0, Ice, 0, 0},
                            AffinityMode::Balanced) == 2,
            "half-to-even rounds 1.5 to 2");
    t.Check(ElementalDamage(1, Ice, AffinityMasks{0, Ice, 0, 0},
                            AffinityMode::Balanced) == 0,
            "half-to-even rounds 0.5 to 0");
    t.Check(ElementalDamage(10, 0, mixed, AffinityMode::Original) == 10,
            "empty mask needs no original call");
    t.ThrowsInvalid(
        [&] { (void)ElementalDamage(10, Fire, mixed, AffinityMode::Original); },
        "original game function is not modeled");
    t.ThrowsInvalid(
        [&] { (void)ElementalDamage(10, 0x20, mixed, AffinityMode::Balanced); },
        "unverified Earth bit rejected");
    t.Check(HasOppositeElementWeakness(Fire, Ice), "Fire against Ice weakness");
    t.Check(HasOppositeElementWeakness(Water, Thunder), "Water against Thunder weakness");
    t.Check(!HasOppositeElementWeakness(Holy, Ice), "Holy has no asserted opposite");
    t.Check(RoundHalfToEven(-1.5) == -2, "negative half-to-even");
}

void TestCommandsAndStatus(Suite& t) {
    const OverdriveGate affordable = InspectOverdriveGate(50, 100, 40, false);
    t.Check(affordable.affordable && !affordable.readyMenuFlag &&
                !affordable.accessibleAndAffordable,
            "command cost and menu flag are separate");
    const OverdriveGate unavailable = InspectOverdriveGate(39, 100, 40, true);
    t.Check(!unavailable.affordable && unavailable.readyMenuFlag &&
                !unavailable.accessibleAndAffordable,
            "ready ring cannot pay command cost");
    t.Check(PlanQuickcast(5, 20, RankPolicy::FixedTwo).rank == 2,
            "Kari fixed-rank Quickcast");
    t.Check(PlanQuickcast(5, 20, RankPolicy::HalveFloorMinTwo).rank == 2,
            "Fantasia floor-half variant");
    t.Check(PlanQuickcast(5, 20, RankPolicy::HalveCeilMinTwo).rank == 3,
            "Fantasia ceil-half variant");
    const QuickcastPlan wide = PlanQuickcast(4, 200, RankPolicy::FixedTwo);
    t.Check(wide.mpCost == 400 && wide.exceedsCommandCostByte,
            "double MP can exceed command byte");
    const FuryBudget fury = PlanFuryBudget(50, 200, 3, 4);
    t.Check(fury.mpConsumed == 50 && fury.rotationContribution == 12 &&
                fury.nominalMpForPower == 62,
            "Fury budget separates consumption from power");
    t.Check(DurationAfterResistance(2, 10) == 1,
            "duration resistance floors at one turn");
    t.Check(RefreshStatusDuration(6, 3, RefreshPolicy::Replace) == 3,
            "refresh replace policy");
    t.Check(RefreshStatusDuration(6, 3, RefreshPolicy::KeepLonger) == 6,
            "refresh keep-longer policy");
    t.Check(CanThreatenAgain(false) && !CanThreatenAgain(true),
            "Threaten one-time state");
}

void TestEquipment(Suite& t) {
    MasterGearBytes raw{};
    raw[0] = 0x5A;
    raw[3] = 0;
    raw[4] = 1;
    raw[5] = 0;
    raw[7] = 0xA5;
    raw[11] = 2;
    raw[12] = 0x34;
    raw[13] = 0x12;
    for (std::size_t i = 0; i < 4; ++i) {
        raw[14 + i * 2] = 0xFF;
        raw[15 + i * 2] = 0;
    }
    raw[14] = 0x19;
    Equipment base = DecodeMasterGear(raw);
    t.Check(base.owner == 1 && base.kind == GearKind::Weapon &&
                base.capacity == 2 && base.modelId == 0x1234 &&
                base.abilities[0] == 0x0019,
            "decode 22-byte weapon.bin record");
    t.Check(EncodeMasterGear(raw, base) == raw,
            "master record byte-identical round trip");

    const auto reforged = PlanReforge(base, 3, GearKind::Armor, 0x4567, true);
    t.Check(reforged.has_value() && reforged->owner == 3 &&
                reforged->kind == GearKind::Armor && reforged->modelId == 0x4567 &&
                base.owner == 1,
            "reforge plan requires catalog approval and leaves input unchanged");
    t.Check(!PlanReforge(base, 3, GearKind::Armor, 0x4567, false),
            "reforge refuses unverified model mapping");

    Equipment source = base;
    source.abilities = {0x20, 0x30, EmptyAbility, EmptyAbility};
    const std::vector<AbilityTransfer> one{{0, 1}};
    const auto merged = PlanMerge(base, source, one, false, false);
    t.Check(merged.has_value() && merged->consumeSource &&
                merged->destinationAfter.abilities[1] == 0x20 &&
                source.abilities[0] == 0x20 && base.abilities[1] == EmptyAbility,
            "one transfer is an atomic plan");
    const std::vector<AbilityTransfer> two{{0, 1}, {1, 0}};
    const auto mergedTwo = PlanMerge(base, source, two, false, false);
    t.Check(mergedTwo.has_value() &&
                mergedTwo->destinationAfter.abilities[0] == 0x30 &&
                mergedTwo->destinationAfter.abilities[1] == 0x20,
            "two transfers can overwrite filled slots");
    const std::vector<AbilityTransfer> duplicateSource{{0, 0}, {0, 1}};
    t.Check(!PlanMerge(base, source, duplicateSource, false, true),
            "one source slot cannot be copied twice");
    t.Check(!PlanMerge(base, source, one, true, false),
            "cannot consume and merge same item");
    Equipment celestialSource = source;
    celestialSource.flags = 0x04;
    t.Check(!PlanMerge(base, celestialSource, one, false, false),
            "Celestial weapon cannot be consumed as source");
    Equipment celestialDestination = base;
    celestialDestination.flags = 0x04;
    t.Check(PlanMerge(celestialDestination, source, one, false, false).has_value(),
            "Celestial weapon may receive abilities");
    const std::vector<AbilityTransfer> invalid{{0, 3}};
    t.Check(!PlanMerge(base, source, invalid, false, false) &&
                base.abilities[1] == EmptyAbility,
            "invalid destination rolls back");
    Equipment duplicateBase = base;
    duplicateBase.abilities[0] = 0x20;
    t.Check(!PlanMerge(duplicateBase, source, one, false, false),
            "duplicate ability requires policy");
    t.Check(PlanMerge(duplicateBase, source, one, false, true).has_value(),
            "duplicate policy is explicit");

    const auto kari = PlanExpandSlots(base, 4, SlotCostPolicy::OneSpherePerSlot);
    const auto dawn = PlanExpandSlots(base, 4, SlotCostPolicy::ProgressiveQuantity);
    t.Check(kari.has_value() && kari->result.capacity == 4 &&
                kari->keySphereQuantities == std::array<std::int32_t, 4>{0, 0, 1, 1},
            "Kari slot-cost schedule");
    t.Check(dawn.has_value() &&
                dawn->keySphereQuantities == std::array<std::int32_t, 4>{0, 0, 3, 4},
            "Dawn slot-cost schedule");
    t.Check(!PlanExpandSlots(base, 5, SlotCostPolicy::OneSpherePerSlot),
            "four-slot physical limit");
    t.ThrowsInvalid(
        [&] { (void)PlanExpandSlots(base, 4, static_cast<SlotCostPolicy>(99)); },
        "unknown Key Sphere schedule rejected");

    const auto cleared = PlanClearAbility(base, 0, 1);
    t.Check(cleared.has_value() &&
                cleared->result.abilities[0] == EmptyAbility &&
                cleared->clearSpheresRequired == 1 &&
                base.abilities[0] == 0x19,
            "Clear Sphere plan");
    t.Check(!PlanClearAbility(base, 1, 1),
            "cannot clear an empty slot");
    const auto evolved = PlanEvolveAbility(base, 0, 0x19, 0x1A, false);
    t.Check(evolved.has_value() && evolved->abilities[0] == 0x1A &&
                base.abilities[0] == 0x19,
            "evolution replaces lower ability instead of stacking");
    t.Check(!PlanEvolveAbility(base, 0, 0x99, 0x1A, false),
            "evolution requires expected old ability");
}

void TestRoster(Suite& t) {
    PartyMember koAvailable{true, false, false, true, false, true};
    PartyMember noAp{true, false, true, false, false, true};
    PartyMember blocked{true, true, false, false, false, true};
    t.Check(EligibleForGuaranteedAp(koAvailable), "KO does not exclude available AP member");
    t.Check(!EligibleForGuaranteedAp(noAp), "No AP excludes member");
    t.Check(!EligibleForGuaranteedAp(blocked), "blocked member cannot gain AP");
    std::vector<PartyMember> party{
        PartyMember{true, false, false, false, true, false},
        PartyMember{true, true, false, false, false, true},
        koAvailable,
    };
    t.Check(FirstAvailableBackline(party, true) == std::size_t{2},
            "first eligible backline index");
    t.Check(!FirstAvailableBackline(party, false),
            "no replacement when switching is disallowed");
}

}  // namespace

int main() {
    Suite suite;
    try {
        TestDamage(suite);
        TestMpAndEnergy(suite);
        TestEquipmentAutoAbilities(suite);
        TestElements(suite);
        TestCommandsAndStatus(suite);
        TestEquipment(suite);
        TestRoster(suite);
    } catch (const std::exception& ex) {
        std::cerr << "FAIL after " << suite.Count() << " checks: " << ex.what()
                  << '\n';
        return 1;
    }
    std::cout << "MODEL_PASS " << suite.Count() << "/" << suite.Count()
              << " checks; runtime behavior remains unverified\n";
    return 0;
}
