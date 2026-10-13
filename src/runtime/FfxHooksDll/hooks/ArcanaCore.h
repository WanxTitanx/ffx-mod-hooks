#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace FfxHooks::Arcana {
inline constexpr unsigned kStandardCardCount=78,kOmegaWeaponCard=78,kUltimaWeaponCard=79;
inline constexpr unsigned kDarkAeonFirstCard=80,kDarkAeonCardCount=10;
inline constexpr unsigned kCardCount=90,kActorCount=7,kMaximumSlots=3;
inline constexpr unsigned kTarotIconResource=kCardCount,kCardBackResource=kCardCount+1,kImageResourceCount=kCardCount+2;
inline constexpr unsigned kMaximumCardEffects=12;
inline constexpr std::int16_t kEmpty=-1;
enum class Mode : std::uint8_t { Twin=0, Constellation=1 };
enum class Error : std::uint8_t {
    None, InvalidState, InvalidCard, InvalidActor, InvalidSlot, NotAcquired,
    Capacity, Stale, TransferRequired, ResolutionRequired, RevisionExhausted
};
enum class EffectKind : std::uint8_t {
    None, HpPercent, MpPercent, StrengthPercent, MagicPercent, DefensePercent,
    MagicDefensePercent, AccuracyFlat, EvasionFlat, LuckFlat, Sensor, Piercing,
    AutoHaste, AutoProtect, AutoShell, AutoRegen, AutoReflect, SosRegen, FirstStrike,
    Counter, MagicCounter, EvadeCounter, MasterThief, AutoPotion, AutoPhoenix,
    BreakHp, BreakDamage, ProofDark, ProofSilence, ProofSleep, ProofPoison,
    ProofConfuse, ProofStone, StrikeFire, StrikeIce, StrikeLightning, StrikeWater, StrikeHoly,
    WardFire, WardIce, WardLightning, WardWater, TouchDark, TouchSilence, TouchSleep,
    TouchSlow, TouchPoison, TouchDeath, TouchArmorBreak, TouchMentalBreak,
    HalfMp, HalfBlackMp, HalfWhiteMp, MpReduction, WhiteMpReduction, WhiteHealing,
    Healing, ItemHealing, FirstCtbReduction, CtbIncrease, FocusOnStart, MpPerTurn,
    LoversHealing, LoversCapHp, DefendMp, KillHp, KillMp, OutgoingDamage,
    IncomingDamage, ElementDamageFireIce, ElementDamageHoly, SurviveOnce, SurviveHeal, OverdriveDamage,
    CriticalChance, GilBonus, ApBonus, DropMultiplier, EncounterReduction,
    OpeningOverdrive, ProofDeath, ProofSlow, TouchStone, TouchConfuse, WardHoly,
    ElementDamageLightningWater, CtbReduction, DeathImmuneDamage,
    StrikeShadow, WardShadow, StrikeEarth, WardEarth, StrikeWind, WardWind,
    StrikeBio, WardBio, StrikeGravity, WardGravity, Count
};
struct Effect { EffectKind kind=EffectKind::None; std::int16_t value=0; };
struct Card {
    std::uint8_t id=0; bool major=false;
    const char* key=nullptr; const char* name=nullptr; const char* description=nullptr; const char* asset=nullptr;
    std::array<Effect,kMaximumCardEffects> effects{};
};
using Slots=std::array<std::array<std::int16_t,kMaximumSlots>,kActorCount>;
struct State {
    std::uint64_t revision=0;
    Mode mode=Mode::Twin;
    std::array<std::uint8_t,kCardCount> acquired{};
    Slots slots{{{{kEmpty,kEmpty,kEmpty}},{{kEmpty,kEmpty,kEmpty}},{{kEmpty,kEmpty,kEmpty}},
                 {{kEmpty,kEmpty,kEmpty}},{{kEmpty,kEmpty,kEmpty}},{{kEmpty,kEmpty,kEmpty}},
                 {{kEmpty,kEmpty,kEmpty}}}};
};
struct Effects {
    std::array<int,static_cast<std::size_t>(EffectKind::Count)> values{};
    int Get(EffectKind kind) const noexcept {return values[static_cast<std::size_t>(kind)];}
};
struct DamageContext {
    std::int64_t amount=0;
    std::uint32_t cap=9999;
    std::uint8_t elements=0;
    bool hp=true,healing=false,fixed=false,fractional=false,overdrive=false,whiteMagic=false,deathImmune=false;
};

const Card* FindCard(unsigned id) noexcept;
Error Validate(const State&) noexcept;
Error Equip(State&,std::uint64_t expected,unsigned actor,unsigned slot,std::int16_t card,bool transfer=false) noexcept;
Error ChangeMode(State&,std::uint64_t expected,Mode,const Slots* resolved=nullptr) noexcept;
Error Award(State&,unsigned card) noexcept;
Error AwardAll(State&,std::uint64_t expected) noexcept;
// Privileged grant: only the admitted Omega story-victory controller may call this.
Error AwardOmegaWeapons(State&,std::uint64_t expected) noexcept;
// Privileged grant for the matching completed original Dark Aeon challenge.
Error AwardDarkAeon(State&,std::uint64_t expected,unsigned card) noexcept;
int Owner(const State&,unsigned card) noexcept;
Effects Aggregate(const State&,unsigned actor) noexcept;
std::uint32_t BoostStat(std::uint32_t base,int percent,int flat,std::uint32_t cap) noexcept;
std::int64_t Damage(const DamageContext&,const Effects& source,const Effects& target) noexcept;
std::uint32_t MpCost(std::uint32_t base,const Effects&,bool black,bool white,bool nativeFree,bool nativeOne) noexcept;
std::uint32_t CtbDelay(std::uint32_t nativeDelay,const Effects&,bool firstEligibleAction) noexcept;
unsigned RewardRate(unsigned nativePercent,const Effects&,EffectKind) noexcept;
}
