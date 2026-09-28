#pragma once
#include "VanguardCatalog.h"
#include "AutoAbilitySlots.h"
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>

// Jarvis-HOOK: portable game-rule contract; no process, patch or file access.
namespace FfxHooks::Vanguard {
struct MappingValidation {
    std::array<bool, AbilityCount> valid{};
    bool AllValid() const { for(bool value:valid) if(!value) return false; return true; }
};
inline unsigned Word(const unsigned char* bytes) noexcept {
    return unsigned(bytes[0]) | (unsigned(bytes[1]) << 8);
}
inline std::uint32_t Dword(const unsigned char* bytes) noexcept {
    return std::uint32_t(Word(bytes))|(std::uint32_t(Word(bytes+2))<<16);
}
inline bool ValidAbilityTable(const unsigned char* bytes,std::size_t size) noexcept {
    if(!bytes||size<20||Word(bytes)!=1||Word(bytes+8)!=0||Dword(bytes+16)!=20)return false;
    const unsigned last=Word(bytes+10),stride=Word(bytes+12),declared=Word(bytes+14);
    return last<=4095&&stride==108&&declared==(last+1)*108&&declared<=size-20;
}
inline MappingValidation ValidateMapping(const unsigned char* bytes,std::size_t size,const Mapping& ids) noexcept {
    MappingValidation result{};
    if(!ValidAbilityTable(bytes,size))return result;
    const unsigned last=Word(bytes+10);
    for(unsigned i=0;i<AbilityCount;++i){
        if(!AutoAbilitySlots::Vanguard(ids[i])||ids[i]>last) continue;
        bool valid=true;
        for(unsigned j=0;j<AbilityCount;++j) if(i!=j&&ids[i]==ids[j]) valid=false;
        const auto* row=bytes+20+ids[i]*108;
        for(unsigned at=16;at<108;++at) if(row[at]) valid=false;
        result.valid[i]=valid;
    }
    return result;
}
inline int Saturate(std::int64_t amount) noexcept {
    return static_cast<int>((std::max)(std::int64_t(INT32_MIN),(std::min)(std::int64_t(INT32_MAX),amount)));
}
inline int Scale(int amount,unsigned numerator,unsigned denominator) noexcept {
    return denominator?Saturate(std::int64_t(amount)*numerator/denominator):amount;
}
inline int BreakDamage(int mitigated,int unmitigated,bool active) noexcept {
    return active&&mitigated>0&&unmitigated>0?Saturate(std::int64_t(mitigated)+unmitigated/4):mitigated;
}
inline unsigned IntegerSqrt(unsigned value) noexcept {
    unsigned result=0,bit=1u<<30;
    while(bit>value) bit>>=2;
    while(bit){
        if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    return result;
}
inline int MagicFromMp(int magic,unsigned mp,bool zeroMp,bool capZeroMp) noexcept {
    const unsigned bonus=IntegerSqrt(mp/2);
    return Saturate(std::int64_t(magic)+(zeroMp&&capZeroMp?(std::min)(bonus,3u):bonus));
}
inline unsigned MpCost(unsigned amount,bool half,bool zero,bool one,bool efficiency) noexcept {
    if(!amount||zero) return 0;
    if(one) return 1;
    const unsigned percent=100u-(half?50u:0u)-(efficiency?25u:0u);
    // A positive command cannot become free merely through integer truncation.
    return (std::max)(1u,static_cast<unsigned>(std::uint64_t(amount)*percent/100));
}
inline bool Above(unsigned charge,unsigned maximum,unsigned percent) noexcept {
    return maximum&&charge<=maximum&&std::uint64_t(charge)*100>std::uint64_t(maximum)*percent;
}
inline int EnergyAttack(int amount,bool elemental,unsigned charge,unsigned maximum,bool boost,bool burst) noexcept {
    const bool b=boost&&elemental&&Above(charge,maximum,50),r=burst&&Above(charge,maximum,75);
    // Explicit balance ruling: the requested 1.65 combination includes 5% synergy.
    return Scale(amount,b&&r?165u:b?120u:r?140u:100u,100);
}
inline int EnergyDefense(int amount,unsigned charge,unsigned maximum,bool wall,bool barrier,bool fixedFraction) noexcept {
    if(amount<=0||fixedFraction) return amount;
    const unsigned reduction=(wall&&Above(charge,maximum,50)?20u:0u)+(barrier&&Above(charge,maximum,75)?30u:0u);
    return Scale(amount,100u-reduction,100);
}
inline int Trade(int amount,unsigned kind,bool physical,bool magical) noexcept {
    if(amount<=0||physical==magical||kind<1||kind>2) return amount;
    return Scale(amount,(physical==(kind==1))?80u:120u,100);
}
inline unsigned CriticalChance(int base,bool sourceBravery,bool targetBravery,bool sourceCaution,bool targetCaution) noexcept {
    if(sourceCaution||targetCaution) return 0;
    const auto chance=std::int64_t(base)+(sourceBravery?25:0)+(targetBravery?25:0);
    return static_cast<unsigned>((std::max)(std::int64_t(0),(std::min)(std::int64_t(100),chance)));
}
inline unsigned Duration(unsigned old,unsigned incoming,unsigned resistance,bool refresh) noexcept {
    if(old==255) return 255;
    if(!incoming||incoming>255||resistance>100) return old;
    if(old&&!refresh) return old;
    if(incoming==255) return 255;
    return (std::max)(1u,incoming*(100-resistance)/100);
}
inline unsigned OppositeMask(unsigned mask) noexcept {
    return ((mask&1)<<1)|((mask&2)>>1)|((mask&4)<<1)|((mask&8)>>1);
}
}
