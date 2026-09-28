#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Jarvis-HOOK. Stable identities from the user-provided Vanguard RT0 contract.
// This catalogue grants no native capability and never enables a feature.
namespace FfxHooks::Vanguard {
enum class Group : unsigned { Damage, Magic, Status, Formation, Weapons, Armor, Equipment, Mapping, Count };
enum class Feature : unsigned {
    UniversalStats, EhpDefense, UnhinderedHealing, AdditiveBreaks, TurnEndBuffs,
    OppositeWeakness, StatusRefresh, DurationResistance, SingleUseThreaten,
    GuaranteedHits, Quickcast, WhiteMagic, MpScaling, TurnCostSwitch,
    AutoReinforce, HitNormalization, HeroBravery, EnergyBoost, EnergyBurst,
    Efficiency, Vampirism, FollowUp, PTrade, MTrade, HeroCaution,
    MpRegen, Elude, EnergyWall, EnergyBarrier, EquipmentCommands,
    PartialOverdriveCosts, Count
};
constexpr unsigned FeatureCount=static_cast<unsigned>(Feature::Count),AbilityCount=13;
// All declared controls have native consumers. Admission still depends on the
// verified executable, runtime ownership and current loaded data, never this list.
inline constexpr bool HasNativeConsumer(unsigned index) noexcept {
    return index<FeatureCount;
}
struct FeatureSpec {const char* key;const char* label;Group group;const char* help;};
inline constexpr FeatureSpec Features[]={
    {"stat_pct_universal","Universal stat percentages",Group::Damage,"Equipment bonuses follow the actual formula's attributes."},
    {"defense_ehp_scaling","Effective-HP defense bonuses",Group::Damage,"Equipment DEF/MDF bonuses divide damage by 1 + bonus."},
    {"healing_ignore_shell","Unhindered healing",Group::Damage,"Protections do not reduce restoration. Zombie still applies."},
    {"breaks_additive_damage","Additive Armor/Mental Break",Group::Damage,"Break adds 25% of the same hit's unmitigated base damage."},
    {"auto_crit_mp0_turn_end","Consume buffs at action end",Group::Status,"All hits benefit before consumed Auto-Crit/MP-0 expire."},
    {"element_opposite_weakness","Opposite-element weakness",Group::Status,"An opposite elemental weakness grants a 1.25x multiplier."},
    {"status_refresh_duration","Refresh status duration",Group::Status,"Successful reapplication refreshes a timed status."},
    {"enemy_duration_resistance","Enemy duration resistance",Group::Status,"Duration resistance is separate from application chance."},
    {"threaten_single_use","Single-use Threaten",Group::Status,"Only one successful Threaten per enemy incarnation."},
    {"guaranteed_hits_no_miss","Guaranteed-hit policy",Group::Damage,"Native guaranteed-hit actions preserve their hit semantics."},
    {"quickcast_replace_doublecast","Quickcast",Group::Magic,"One Black Magic spell, rank 2, double MP cost."},
    {"dualcast_white_magic","White Magic in Double/Quickcast",Group::Magic,"Independent White Magic submenu in the active cast command."},
    {"magic_mp_scaling","Current-MP magic power",Group::Magic,"MAG gains floor(sqrt(MP / 2)); Focus is counted once."},
    {"party_switch_costs_turn","Switch costs a turn",Group::Formation,"A voluntary switch pays the selected CTB action rank."},
    {"eject_shatter_auto_replace","Auto-reinforce Eject/Shatter",Group::Formation,"First native-eligible reserve replaces an ejected ally."},
    {"single_multi_hit_normalization","Single/multi-hit scaling",Group::Damage,"Explicit balance rates; both default to neutral100%, not an inferred rule."},
    {"hero_bravery","Hero's Bravery",Group::Weapons,"Equipped: +25 points to critical chance dealt and received."},
    {"energy_boost","Energy Boost",Group::Weapons,"Equipped: elemental damage/healing bonus above 50% OD."},
    {"energy_burst","Energy Burst",Group::Weapons,"Equipped: damage/healing bonus above 75% OD."},
    {"efficiency","Efficiency",Group::Weapons,"Equipped: -25% MP/OD costs; Half MP reductions add."},
    {"vampirism","Vampirism",Group::Weapons,"Equipped: recover 2% of actual hostile HP lost per action."},
    {"follow_up","Follow Up",Group::Weapons,"One non-recursive follow-up to an ally's single-target action."},
    {"p_trade","P-Trade",Group::Armor,"Equipped: physical damage x0.8; magical damage x1.2."},
    {"m_trade","M-Trade",Group::Armor,"Equipped: physical damage x1.2; magical damage x0.8."},
    {"hero_caution","Hero's Caution",Group::Armor,"Explicit critical policy; forced critical effects retain priority."},
    {"mp_regen","MP Regen",Group::Armor,"Equipped: restore 2% maximum MP once at a real turn start."},
    {"elude","Elude",Group::Armor,"Equipped: +50 evasion while defending, without BYTE overflow."},
    {"energy_wall","Energy Wall",Group::Armor,"Equipped: damage x0.8 above 50% OD; no healing reduction."},
    {"energy_barrier","Energy Barrier",Group::Armor,"Equipped: damage x0.7 above 75% OD; with Wall, x0.5."},
    {"equipment_active_commands","Equipped active commands",Group::Equipment,"Only verified equipped bindings may add battle commands."},
    {"equipment_partial_overdrive","Partial Overdrive costs",Group::Equipment,"The displayed partial OD fee must equal the one-time debit."}
};
struct AbilitySpec {const char* key;const char* label;unsigned id;unsigned kind;Feature feature;};
inline constexpr AbilitySpec Abilities[]={
    {"hero_bravery","Hero's Bravery",135,0,Feature::HeroBravery},
    {"energy_boost","Energy Boost",136,0,Feature::EnergyBoost},
    {"energy_burst","Energy Burst",137,0,Feature::EnergyBurst},
    {"efficiency","Efficiency",138,0,Feature::Efficiency},
    {"vampirism","Vampirism",139,0,Feature::Vampirism},
    {"follow_up","Follow Up",140,0,Feature::FollowUp},
    {"p_trade","P-Trade",141,1,Feature::PTrade},
    {"m_trade","M-Trade",142,1,Feature::MTrade},
    {"hero_caution","Hero's Caution",143,1,Feature::HeroCaution},
    {"mp_regen","MP Regen",144,1,Feature::MpRegen},
    {"elude","Elude",145,1,Feature::Elude},
    {"energy_wall","Energy Wall",146,1,Feature::EnergyWall},
    {"energy_barrier","Energy Barrier",147,1,Feature::EnergyBarrier}
};
inline constexpr const char* GroupNames[]={"Damage & formulas","Magic","Status & affinities",
    "Formation & CTB","Weapon abilities","Armor abilities","Equipment systems","Ability ID mapping"};
static_assert(std::size(Features)==FeatureCount&&std::size(Abilities)==AbilityCount);
using Mapping=std::array<unsigned,AbilityCount>;
inline Mapping DefaultMapping(){Mapping out{};for(unsigned i=0;i<AbilityCount;++i)out[i]=Abilities[i].id;return out;}
}
