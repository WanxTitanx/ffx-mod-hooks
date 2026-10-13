#pragma once
// Jarvis-HOOK: approved Omega rules. No engine addresses, RNG or native writes.
#include <array>
#include <cstdint>
#include "OmegaEncounter.h"

namespace FfxHooks::OmegaT7 {
inline constexpr unsigned None=48;
enum class Skill : unsigned {Darkja,Holy,Aeroja,Firaja,Blizzaja,Thundaja,Waterja,AntiFlare,Meteor,Ultima,Bio,Silence,Slowga,Darkness,Zombie,MentalBreak,ArmorBreak,Reflect,MightyGuard,Regen,Hastega,Focus,Cheer,Luck,HolyFlare,Flare,Count};
enum Buff : unsigned {Shell=1,Protect=2,Reflect=4,Regen=8,Haste=16,NulBlaze=32,NulFrost=64,NulShock=128,NulTide=256};
// Semantic element bits; the adapter maps non-basic native extension bits.
enum Element : unsigned {Fire=1,Ice=2,Thunder=4,Water=8,Holy=16,Dark=32};
enum Stack : unsigned {Cheer=0,Focus=1,Luck=2,Reflex=3};
struct Definition {unsigned id;const char* key;const char* name;Skill skill;unsigned species;};
// Species zero means generic. 0xFFFF is the four approved revealed Mimic forms.
inline constexpr std::array<Definition,48> Catalog{{
    {0,"abyss","Abyss",Skill::Darkja,0},
    {1,"aurora","Aurora",Skill::Holy,0},
    {2,"gale","Gale",Skill::Aeroja,0},
    {3,"furnace","Furnace",Skill::Firaja,0},
    {4,"frost","Frost",Skill::Blizzaja,0},
    {5,"storm","Storm",Skill::Thundaja,0},
    {6,"flood","Flood",Skill::Waterja,0},
    {7,"arcane_rupture","Arcane Rupture",Skill::AntiFlare,0},
    {8,"disaster","Disaster",Skill::Meteor,0},
    {9,"cataclysm","Cataclysm",Skill::Ultima,0},
    {10,"contagion","Contagion",Skill::Bio,0},
    {11,"silencer","Silencer",Skill::Silence,0},
    {12,"lethargy","Lethargy",Skill::Slowga,0},
    {13,"eclipse","Eclipse",Skill::Darkness,0},
    {14,"profanation","Profanation",Skill::Zombie,0},
    {15,"exhaustion","Exhaustion",Skill::MentalBreak,0},
    {16,"attrition","Attrition",Skill::ArmorBreak,0},
    {17,"mirror","Mirror",Skill::Reflect,0},
    {18,"fortress","Fortress",Skill::MightyGuard,0},
    {19,"renewal","Renewal",Skill::Regen,0},
    {20,"impetus","Impetus",Skill::Hastega,0},
    {21,"concentration","Concentration",Skill::Focus,0},
    {22,"bravery","Bravery",Skill::Cheer,0},
    {23,"omen","Omen",Skill::Luck,0},
    {24,"predatory_nerve","Predatory Nerve",Skill::Aeroja,239},
    {25,"split_step","Split Step",Skill::Slowga,239},
    {26,"storm_eye","Storm Eye",Skill::Aeroja,242},
    {27,"empty_eye","Empty Eye",Skill::Darkja,242},
    {28,"unstable_prism","Unstable Prism",Skill::HolyFlare,83},
    {29,"black_void","Black Void",Skill::Darkja,83},
    {30,"resonant_shell","Resonant Shell",Skill::MightyGuard,240},
    {31,"broken_shell","Broken Shell",Skill::HolyFlare,240},
    {32,"unstable_fusion","Unstable Fusion",Skill::Firaja,218},
    {33,"ruptured_valve","Ruptured Valve",Skill::Aeroja,218},
    {34,"conductive_soul","Conductive Soul",Skill::Thundaja,219},
    {35,"shadow_feast","Shadow Feast",Skill::Darkja,219},
    {36,"executor","Executor",Skill::Meteor,244},
    {37,"lone_hunter","Lone Hunter",Skill::AntiFlare,244},
    {38,"arcane_feline","Arcane Feline",Skill::Holy,90},
    {39,"echo_hunter","Echo Hunter",Skill::Thundaja,90},
    {40,"ancient_debt","Ancient Debt",Skill::Darkja,224},
    {41,"last_step","Last Step",Skill::MightyGuard,224},
    {42,"abyss_lord","Abyss Lord",Skill::Ultima,188},
    {43,"ruin_archmage","Ruin Archmage",Skill::Darkja,188},
    {44,"unbroken_guard","Unbroken Guard",Skill::MightyGuard,49},
    {45,"end_echo","End Echo",Skill::Meteor,910},
    {46,"false_relic","False Relic",Skill::Flare,0xFFFF},
    {47,"cursed_treasure","Cursed Treasure",Skill::MightyGuard,0xFFFF},
}};
inline constexpr bool Mimic(unsigned species) noexcept {return species==251||species==252||species==253||species==256;}
inline constexpr bool Combatant(unsigned species) noexcept {return species>0&&species<1024&&species!=99&&species!=100&&species!=211;}
inline bool Compatible(unsigned profile,unsigned species) noexcept {
    if(profile>=48||!Combatant(species))return false;
    const auto wanted=Catalog[profile].species;return wanted==0||wanted==species||(wanted==0xFFFF&&Mimic(species));
}
inline std::uint64_t Mix(std::uint64_t value) noexcept {
    value+=0x9E3779B97F4A7C15ULL;value=(value^(value>>30))*0xBF58476D1CE4E5B9ULL;
    value=(value^(value>>27))*0x94D049BB133111EBULL;return value^(value>>31);
}
inline unsigned Choose(unsigned species,unsigned ticket,std::uint64_t roll,std::uint64_t used) noexcept {
    if(!Combatant(species)||ticket>=100)return None;
    std::array<unsigned,24> generic{},specific{};unsigned g=0,s=0;
    for(unsigned i=0;i<48;++i)if(!(used&(1ull<<i))&&Compatible(i,species)){
        if(i<24)generic[g++]=i;else specific[s++]=i;
    }
    // Preferred 75/25 pools; exhaustion falls back to a nonrepeating compatible
    // pool, rather than duplicating a mark or assigning another species' rule.
    if(ticket>=75&&s)return specific[roll%s];
    if(g)return generic[roll%g];
    return s?specific[roll%s]:None;
}
using Roster=std::array<unsigned,8>;
using Assignment=std::array<unsigned,8>;
inline Assignment Assign(std::uint64_t seed,unsigned field,unsigned formation,const Roster& roster) noexcept {
    Assignment result;result.fill(None);if(!seed||!OmegaEncounter::Eligible(field,formation))return result;
    auto key=Mix(seed^Mix((std::uint64_t(field)<<32)|formation));
    for(unsigned i=0;i<8;++i)key=Mix(key^Mix((std::uint64_t(i)<<32)|roster[i]));
    std::uint64_t used=0;
    for(unsigned i=0;i<8;++i){const auto draw=Mix(key^Mix(i));const auto profile=Choose(roster[i],static_cast<unsigned>(draw%100),Mix(draw),used);
        result[i]=profile;if(profile<48)used|=1ull<<profile;}
    return result;
}
struct Vitals {std::uint32_t hp=0,maximum=0;unsigned buffs=0;std::array<unsigned,4> stacks{};bool active=true,zombie=false;};
enum class EventKind {Hit,Received,Dealt,ExtraFinished,AllyDeath,PartyHaste,OwnTurn,Walk,FinalApproach,CoreEnergy,StealAttempt,VitalsChanged};
struct Event {
    EventKind kind=EventKind::VitalsChanged;Vitals before{},after{};
    unsigned element=0,removedBuffs=0,loss=0,target=31;
    bool physical=false,magic=false,landed=false,evaded=false,critical=false,absorbed=false;
    bool armorBreakAdded=false,mentalBreakAdded=false,poisonApplied=false,negativeApplied=false,nativeAi=false;
    bool targetBlind=false,targetArmorBreak=false,targetAlive=false,dispel=false;
};
struct Effect {
    unsigned self=0,group=0,healPercent=0,ctbPercent=0,absorb=0;
    std::array<unsigned,4> stacks{};unsigned slowTarget=31;bool restoreAbsorb=false;
    bool Empty() const noexcept {return !self&&!group&&!healPercent&&!ctbPercent&&!absorb&&!restoreAbsorb&&slowTarget==31&&!stacks[0]&&!stacks[1]&&!stacks[2]&&!stacks[3];}
};
struct State {
    bool consumed=false,turnConsumed=false;unsigned counter=0,chargePercent=0,absorbElement=0,absorbTurns=0;
    std::uint32_t slowTargets=0;
};
inline bool Cross(const Event& e,unsigned percent) noexcept {
    return (e.kind==EventKind::Received||e.kind==EventKind::Hit)&&e.before.maximum==e.after.maximum&&e.after.hp<e.before.hp&&
        std::uint64_t(e.before.hp)*100>std::uint64_t(e.after.maximum)*percent&&
        std::uint64_t(e.after.hp)*100<=std::uint64_t(e.after.maximum)*percent;
}
inline bool Once(State& state,bool trigger) noexcept {if(state.consumed||!trigger)return false;state.consumed=true;return true;}
inline bool PerTurn(State& state,bool trigger) noexcept {if(state.turnConsumed||!trigger)return false;state.turnConsumed=true;return true;}
inline unsigned TakeCharge(State& state) noexcept {const auto value=state.chargePercent;state.chargePercent=0;return value;}
inline Effect Observe(unsigned profile,State& s,const Event& e) noexcept {
    Effect r;if(profile>=48||!e.after.active||!e.after.hp||!e.after.maximum)return r;
    if(e.kind==EventKind::OwnTurn){s.turnConsumed=false;
        if(s.absorbTurns&&!--s.absorbTurns){s.absorbElement=0;r.restoreAbsorb=true;}return r;
    }
    const bool receive=e.kind==EventKind::Received,hit=e.kind==EventKind::Hit,deal=e.kind==EventKind::Dealt;
    const bool dodge=receive&&e.physical&&e.evaded;
    const bool magicHit=receive&&e.magic&&e.landed&&e.loss>0;
    const bool critical=(receive||hit)&&e.landed&&e.critical;
    const bool ally=e.kind==EventKind::AllyDeath;
    const auto damage=[&](unsigned element){return receive&&e.landed&&e.loss>0&&(e.element&element);};
    const auto absorb=[&](unsigned element){return receive&&e.landed&&e.absorbed&&(e.element&element);};
    switch(profile){
        case 0:if(Once(s,Cross(e,50))){r.self=Shell;r.stacks[Focus]=1;}break;
        case 1:case 33:if(Once(s,Cross(e,50)))r.group=Haste;break;
        case 2:if(dodge&&!s.chargePercent)s.chargePercent=30;break;
        case 3:if(Once(s,damage(Ice)))r.self=NulFrost;break;
        case 4:if(Once(s,damage(Fire)))r.self=NulBlaze;break;
        case 5:if(Once(s,damage(Water)))r.self=NulTide;break;
        case 6:if(Once(s,damage(Thunder)))r.self=NulShock;break;
        case 7:if(Once(s,receive&&e.mentalBreakAdded))r.stacks[Focus]=1;break;
        case 8:case 37:if(Once(s,ally))r.group=Haste;break;
        case 9:if(e.kind==EventKind::ExtraFinished&&++s.counter==3){s.counter=0;r.stacks[Focus]=1;}break;
        case 10:if(deal&&e.poisonApplied&&e.targetAlive&&e.target<31&&!(s.slowTargets&(1u<<e.target))){s.slowTargets|=1u<<e.target;r.slowTarget=e.target;}break;
        case 11:case 39:if(!s.consumed&&magicHit&&++s.counter>=(profile==11?3u:2u)){s.consumed=true;if(profile==11)r.self=Reflect;else r.group=Haste;}break;
        case 12:if(PerTurn(s,e.kind==EventKind::PartyHaste))r.ctbPercent=20;break;
        case 13:if(PerTurn(s,deal&&e.landed&&e.targetBlind))r.stacks[Cheer]=1;break;
        case 14:if(Once(s,Cross(e,50)))r.self=Regen;break;
        case 15:if(PerTurn(s,deal&&e.mentalBreakAdded))r.stacks[Focus]=1;break;
        case 16:if(Once(s,receive&&critical))r.self=Protect;break;
        case 17:if(Once(s,receive&&e.dispel&&(e.removedBuffs&Reflect)))r.self=Shell;break;
        case 18:if(Once(s,hit&&e.loss&&std::uint64_t(e.loss)*100>=std::uint64_t(e.after.maximum)*20))r.healPercent=15;break;
        case 19:if(Once(s,Cross(e,25))){r.self=Shell;r.group=Haste;}break;
        case 20:if(Once(s,ally)){r.stacks[Cheer]=r.stacks[Focus]=1;}break;
        case 21:if(Once(s,e.before.stacks[Focus]<5&&e.after.stacks[Focus]>=5))r.self=Shell;break;
        case 22:if(Once(s,e.before.stacks[Cheer]<5&&e.after.stacks[Cheer]>=5))r.self=Protect;break;
        case 23:if(Once(s,receive&&critical))r.group=Haste;break;
        case 24:if(Once(s,Cross(e,50))){r.group=Haste;r.stacks[Reflex]=1;}break;
        case 25:if(dodge&&++s.counter==2){s.counter=0;r.stacks[Reflex]=1;}break;
        case 26:if(Once(s,damage(Holy)))r.self=Shell;break;
        case 27:if(Once(s,dodge))r.stacks[Focus]=2;break;
        case 28:if(Once(s,receive&&e.landed&&(e.element&15))){s.absorbElement=(e.element&15)&(~(e.element&15)+1u);s.absorbTurns=2;r.absorb=s.absorbElement;}break;
        case 29:if(Once(s,absorb(Dark))){r.self=Shell;r.stacks[Focus]=1;}break;
        case 30:if(Once(s,receive&&critical&&e.physical))r.healPercent=10;break;
        case 31:if(Once(s,receive&&e.armorBreakAdded))r.group=Haste;break;
        case 32:if(absorb(Fire)&&!s.chargePercent&&++s.counter==3){s.counter=0;s.chargePercent=50;}break;
        case 34:if(Once(s,absorb(Thunder)))r.self=Regen;break;
        case 35:if(PerTurn(s,absorb(Dark)))r.stacks[Focus]=1;break;
        case 36:if(PerTurn(s,deal&&e.landed&&e.targetArmorBreak))r.stacks[Cheer]=1;break;
        case 38:if(Once(s,deal&&e.nativeAi&&e.negativeApplied))r.stacks[Focus]=2;break;
        case 40:if(e.kind==EventKind::Walk)r.stacks[Cheer]=r.stacks[Focus]=1;break;
        case 41:if(Once(s,e.kind==EventKind::FinalApproach))r.group=Haste;break;
        case 42:if(Once(s,receive&&e.dispel&&e.removedBuffs))r.stacks[Focus]=2;break;
        case 43:if(Once(s,Cross(e,50))){r.group=Haste;r.self=Shell;}break;
        case 44:if(Once(s,receive&&e.armorBreakAdded))r.healPercent=20;break;
        case 45:if(e.kind==EventKind::CoreEnergy)r.stacks[Focus]=1;break;
        case 46:if(Once(s,e.kind==EventKind::StealAttempt))r.group=Haste;break;
        case 47:if(Once(s,Cross(e,25)))r.healPercent=20;break;
        default:break;
    }
    r.self&=~e.after.buffs;
    for(unsigned i=0;i<4;++i){const auto current=e.after.stacks[i];const auto room=current<5?5-current:0;if(r.stacks[i]>room)r.stacks[i]=room;}
    return r;
}
} // namespace FfxHooks::OmegaT7
