#pragma once
// Jarvis-HOOK: value-only regional behavior. No stat, reward or native AI writer.
#include "SinSpreadCore.h"
#include <array>
#include <cstdint>
#include <limits>

namespace FfxHooks::InsideSin {
inline constexpr unsigned FirstMonster=20,MonsterCount=8,EndMonster=FirstMonster+MonsterCount;
enum class Preset : unsigned {None,Devastator,Oppressor,Guardian,Tormentor,Breaker,Ascendant,Sentinel};
enum Buff : unsigned {Shell=1,Protect=2,Reflect=4,Regen=8,Haste=16};
struct Definition {Preset preset;unsigned tier,weight;const char* key;const char* name;const char* command;unsigned power,element;bool physical,self,all;unsigned buffs;};
inline constexpr std::array<Definition,7> Definitions{{
    {Preset::Devastator,6,18,"devastator","Devastator","abyssal_pulse",24,0x80,false,false,true,0},
    {Preset::Oppressor,6,18,"oppressor","Oppressor","rupture",30,0,false,false,false,0},
    {Preset::Guardian,6,18,"guardian","Guardian","bastion",0,0,false,true,false,Protect|Shell|Regen},
    {Preset::Tormentor,6,18,"tormentor","Tormentor","venom_hex",24,0x80,false,false,false,0},
    {Preset::Breaker,6,18,"breaker","Breaker","shattering_blow",24,0,true,false,false,0},
    {Preset::Ascendant,7,5,"ascendant","Ascendant","darkja",60,0x80,false,false,false,0},
    {Preset::Sentinel,7,5,"sentinel","Sentinel","aegis",0,0,false,true,false,Protect|Shell|Reflect|Regen},
}};
inline const Definition* Get(Preset value) noexcept {
    const auto id=static_cast<unsigned>(value);return id&&id<=Definitions.size()?&Definitions[id-1]:nullptr;
}
inline constexpr std::array<unsigned,11> Roster{{47,65,86,95,182,184,187,188,210,216,220}};
inline constexpr bool EligibleField(unsigned field,unsigned formation) noexcept {
    return (field==1073&&formation<5)||((field==1074||field==1075)&&formation<8);
}
inline constexpr bool ModelAllowed(unsigned field,unsigned model) noexcept {
    if(field==1073)return model==86||model==182||model==184||model==187||model==216||model==220;
    if(field==1074)return model==65||model==86||model==95||model==182||model==184||model==187||model==210||model==216||model==220;
    if(field==1075)return model==47||model==65||model==86||model==95||model==188||model==210||model==220;
    return false;
}
inline Preset Choose(unsigned ticket) noexcept {
    if(ticket>=100)return Preset::None;
    for(const auto& entry:Definitions){if(ticket<entry.weight)return entry.preset;ticket-=entry.weight;}
    return Preset::None;
}
inline unsigned Count(unsigned mask) noexcept {unsigned count=0;for(;mask;mask&=mask-1)++count;return count;}
inline unsigned Infections(std::uint32_t seed,std::uint32_t visit,unsigned distribution) noexcept {
    if(!SinSpread::ValidDistribution(distribution))return 0;
    // Exactly the existing regional quota and ranking. Preset rarity does not
    // change the number of infected species selected for this visit.
    const auto entropy=SinSpread::Mix(seed^SinSpread::Mix(visit+0x9E3779B9u)^SinSpread::Mix(1073));
    const auto size=static_cast<unsigned>(Roster.size());
    const auto quota=distribution?SinSpread::Quota(size,static_cast<SinSpread::Distribution>(distribution)):
        SinSpread::Mix(entropy)%(size*80u/100u+1u);
    std::array<unsigned,Roster.size()> order{};for(unsigned i=0;i<size;++i)order[i]=i;
    std::sort(order.begin(),order.end(),[entropy](unsigned a,unsigned b){
        const auto x=SinSpread::Mix(entropy^Roster[a]),y=SinSpread::Mix(entropy^Roster[b]);return x==y?a<b:x<y;
    });
    unsigned mask=0;for(unsigned i=0;i<quota;++i)mask|=1u<<order[i];return mask;
}
inline bool Infected(unsigned mask,unsigned model) noexcept {
    for(unsigned i=0;i<Roster.size();++i)if(Roster[i]==model)return (mask&(1u<<i))!=0;
    return false;
}
inline Preset ChooseForActor(std::uint32_t seed,std::uint32_t visit,std::uint64_t battle,unsigned model,unsigned slot) noexcept {
    // The pool is shared. Species is entropy, not a preset mapping. Different
    // instances get independent latches and can receive different presets.
    const auto entropy=SinSpread::Mix(seed^SinSpread::Mix(visit)^SinSpread::Mix(model)^SinSpread::Mix(slot)^static_cast<std::uint32_t>(battle)^static_cast<std::uint32_t>(battle>>32));
    return Choose(entropy%100);
}
struct Identity {
    std::uint64_t battle=0,incarnation=0;
    std::uintptr_t address=0,resource=0;
    unsigned model=0;
    bool Valid() const noexcept {return battle&&incarnation&&address&&resource&&model;}
};
inline bool operator==(const Identity& a,const Identity& b) noexcept {
    return a.battle==b.battle&&a.incarnation==b.incarnation&&a.address==b.address&&a.resource==b.resource&&a.model==b.model;
}
struct Vitals {std::uint32_t hp=0,maximum=0;unsigned buffs=0;bool zombie=false;};
struct Reaction {bool triggered=false;unsigned buffs=0;std::uint32_t heal=0;};
class GeminiLatch {
    Identity survivor_{},partner_{};bool consumed_=false;
public:
    bool Bind(const Identity& survivor,const Identity& partner) noexcept {
        if(!survivor.Valid()||!partner.Valid()||survivor.battle!=partner.battle||
           !((survivor.model==182&&partner.model==184)||(survivor.model==184&&partner.model==182)))return false;
        if(!(survivor_==survivor))consumed_=false;
        survivor_=survivor;partner_=partner;return true;
    }
    Reaction Death(const Identity& partner,bool dead,const Vitals& v) noexcept {
        if(!survivor_.Valid()||!(partner_==partner)||!dead||consumed_||!v.hp||!v.maximum)return {};
        consumed_=true;return {true,Haste&~v.buffs,0};
    }
    void Reset() noexcept {survivor_={};partner_={};consumed_=false;}
};
class Ledger {
    struct Entry {Identity identity{};Preset preset=Preset::None;bool consumed=false;};
    std::array<Entry,8> actors_{};
    Entry* Find(unsigned actor,const Identity& identity) noexcept {
        if(actor<FirstMonster||actor>=EndMonster)return nullptr;
        auto& value=actors_[actor-FirstMonster];
        return identity.Valid()&&value.identity==identity&&Get(value.preset)?&value:nullptr;
    }
public:
    void Reset() noexcept {actors_={};}
    void Retire(unsigned actor) noexcept {if(actor>=FirstMonster&&actor<EndMonster)actors_[actor-FirstMonster]={};}
    bool Bind(unsigned actor,const Identity& identity,Preset preset) noexcept {
        if(actor<FirstMonster||actor>=EndMonster||!identity.Valid()||!Get(preset))return false;
        auto& value=actors_[actor-FirstMonster];
        if(value.identity==identity)return value.preset==preset;
        value={identity,preset,false};return true;
    }
    Reaction ReceivedAction(unsigned actor,const Identity& identity,const Vitals& v) noexcept {
        auto* entry=Find(actor,identity);
        if(!entry||entry->preset!=Preset::Ascendant||entry->consumed||!v.hp||!v.maximum||
           std::uint64_t(v.hp)*2>v.maximum)return {};
        entry->consumed=true;return {true,(Haste|Shell)&~v.buffs,0};
    }
    Reaction Hit(unsigned actor,const Identity& identity,std::uint32_t before,const Vitals& v,bool offensive) noexcept {
        auto* entry=Find(actor,identity);
        if(!entry||entry->preset!=Preset::Sentinel||entry->consumed||!offensive||!v.hp||!v.maximum||before<=v.hp||
           std::uint64_t(before-v.hp)*4<v.maximum)return {};
        entry->consumed=true;return {true,Haste&~v.buffs,static_cast<std::uint32_t>(std::uint64_t(v.maximum)*20/100)};
    }
};
inline std::int32_t HealingAmount(std::uint32_t amount,bool zombie) noexcept {
    if(amount>std::uint32_t((std::numeric_limits<std::int32_t>::max)()))return 0;
    return zombie?static_cast<std::int32_t>(amount):-static_cast<std::int32_t>(amount);
}
struct ExtraPlan {Preset preset=Preset::None;bool self=false;unsigned buffs=0;std::uint32_t targets=0;};
inline ExtraPlan Extra(Preset preset,unsigned present,std::uint32_t livingParty,unsigned roll) noexcept {
    const auto* definition=Get(preset);if(!definition)return {};
    if(definition->self){const unsigned missing=definition->buffs&~present;return missing?ExtraPlan{preset,true,missing,0}:ExtraPlan{};}
    // Only active living party actors 0..17 are eligible, including an Aeon.
    livingParty&=0x3FFFFu;if(!livingParty)return {};
    if(definition->all)return {preset,false,0,livingParty};
    unsigned selected=roll%Count(livingParty);
    for(unsigned id=0;id<18;++id)if(livingParty&(1u<<id)){if(!selected--)return {preset,false,0,1u<<id};}
    return {};
}
inline bool EligibleAction(bool completed,bool alive,bool reaction,bool overdrive,bool extra) noexcept {
    return completed&&alive&&!reaction&&!overdrive&&!extra;
}
inline constexpr unsigned BuffOffsets[5]={0x60B,0x60C,0x60D,0x612,0x613};
inline constexpr unsigned ChanceIndices[5]={15,16,17,22,23};
inline constexpr unsigned DurationIndices[5]={3,4,5,10,11};
inline constexpr std::uint8_t RemovableDuration=254;
} // namespace FfxHooks::InsideSin
