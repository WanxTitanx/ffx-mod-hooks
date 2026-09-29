#pragma once
#include "ArcanaCore.h"
#include <atomic>

namespace FfxHooks::Arcana::Elemental {
inline constexpr unsigned Poison=1,Gravity=2,All=Poison|Gravity;
struct Snapshot {
    unsigned strikes=0,wards=0;
    std::uint64_t battle=0,revision=0;
    bool operator==(const Snapshot& other) const noexcept {
        return strikes==other.strikes&&wards==other.wards&&battle==other.battle&&revision==other.revision;
    }
};
using Provider=bool(*)(unsigned,Snapshot&) noexcept;
inline std::atomic<Provider> provider{nullptr};
inline bool Register(Provider value) noexcept {
    if(!value)return false;
    Provider expected=nullptr;
    return provider.compare_exchange_strong(expected,value)||expected==value;
}
inline void Unregister(Provider value) noexcept {provider.compare_exchange_strong(value,nullptr);}
inline bool Read(unsigned actor,Snapshot& output) noexcept {
    output={};if(actor>=kActorCount)return false;
    const auto current=provider.load(std::memory_order_acquire);Snapshot result{};
    if(!current||!current(actor,result)||!result.battle||((result.strikes|result.wards)&~All)||
       provider.load(std::memory_order_acquire)!=current)return false;
    output=result;return true;
}
inline Snapshot Collect(const Effects& effects,std::uint64_t battle,std::uint64_t revision) noexcept {
    if(!battle)return {};
    Snapshot result{};result.battle=battle;result.revision=revision;
    if(effects.Get(EffectKind::StrikeBio)>0)result.strikes|=Poison;
    if(effects.Get(EffectKind::StrikeGravity)>0)result.strikes|=Gravity;
    if(effects.Get(EffectKind::WardBio)>0)result.wards|=Poison;
    if(effects.Get(EffectKind::WardGravity)>0)result.wards|=Gravity;
    return result;
}
inline bool EligibleWeapon(const unsigned char* row,std::size_t size,unsigned formula) noexcept {
    if(!row||size<96||!(row[0x1E]&4)||(row[0x23]&7)!=1||(row[0x20]&0x10))return false;
    // These are the normal scalable weapon formulas admitted by Arcana's
    // existing classifier. Fractional HP, fixed damage and unknown formulas
    // cannot become a new gravity attack simply by equipping a card.
    switch(formula){
    case 1:case 2:case 3:case 4:case 7:case 14:case 15:case 17:case 18:case 19:case 20:return true;
    default:return false;
    }
}
inline std::int32_t WardExposure(std::int32_t value,bool ward,bool locked) noexcept {
    return ward&&!locked&&value>0?value/2:value;
}
} // namespace FfxHooks::Arcana::Elemental
