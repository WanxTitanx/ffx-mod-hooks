#pragma once
#include "ElementRegistry.h"
#include <cstring>
#include <limits>

namespace FfxHooks::ElementalDominion {
enum class MixPolicy : std::uint8_t {NativeExact,HighestExposure,SplitWeighted,LowestExposure};
struct AffinitySources {
    std::int32_t base=10000,equipment=0;
    unsigned imperil=0,ward=0;
    bool locked=false;
    std::int32_t lockedValue=10000;
};
struct AffinityValue {Error error=Error::Ok;std::int32_t value=10000;bool locked=false,clamped=false;};
struct AffinityPart {std::int32_t value=10000;unsigned weight=1;};
struct DamageResult {Error error=Error::Ok;std::int32_t damage=0;bool saturated=false;};
struct NativeMasks {std::uint8_t weak=0,resist=0,nulls=0,absorb=0;};
inline bool ValidTier(std::int32_t value) noexcept {return value>=-10000&&value<=25000&&value%2500==0;}
inline AffinityValue Effective(const AffinitySources& source) noexcept {
    if(!ValidTier(source.base)||!ValidTier(source.lockedValue)||source.equipment%2500!=0||source.imperil>4||source.ward>4)
        return {Error::InvalidTier};
    if(source.locked)return {Error::Ok,source.lockedValue,true,false};
    const auto exposed=static_cast<std::int64_t>(source.base)+source.equipment+static_cast<std::int64_t>(source.imperil)*2500;
    const auto requested=static_cast<std::int64_t>(source.ward)*2500;
    // Ward removes only positive exposure. It cannot turn damage into healing
    // or deepen existing absorption, including when equipment changes the base.
    const auto reduction=exposed<=0?0:exposed<requested?exposed:requested;
    const auto raw=exposed-reduction;
    const auto bounded=raw<-10000?-10000:raw>25000?25000:raw;
    return {Error::Ok,static_cast<std::int32_t>(bounded),false,bounded!=raw};
}
inline DamageResult BoundedDamage(std::int64_t value) noexcept {
    constexpr auto low=std::numeric_limits<std::int32_t>::min();
    constexpr auto high=std::numeric_limits<std::int32_t>::max();
    if(value<low)return {Error::Ok,low,true};
    if(value>high)return {Error::Ok,high,true};
    return {Error::Ok,static_cast<std::int32_t>(value),false};
}
inline DamageResult Resolve(std::int32_t damage,const AffinityPart* parts,unsigned count,MixPolicy policy) noexcept {
    if(count>ElementLimit)return {Error::Capacity,damage};
    if(count&&!parts)return {Error::InvalidInput,damage};
    if(policy!=MixPolicy::HighestExposure&&policy!=MixPolicy::LowestExposure&&policy!=MixPolicy::SplitWeighted)
        return {Error::UnsupportedPolicy,damage};
    if(!count)return {Error::Ok,damage};
    // This bound includes abs(INT32_MIN). No intermediate can overflow even
    // when all 32 parts use the largest multiplier and weight.
    static_assert(std::numeric_limits<std::int64_t>::max()/ElementLimit/1000/25000>=
                  static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())+1);
    std::int64_t weighted=0,total=0;
    std::int32_t selected=parts[0].value;
    for(unsigned i=0;i<count;++i){
        const auto& part=parts[i];
        if(part.value<-10000||part.value>25000)return {Error::InvalidTier,damage};
        if(!part.weight||part.weight>1000)return {Error::InvalidWeight,damage};
        weighted+=static_cast<std::int64_t>(part.value)*part.weight;
        total+=part.weight;
        if(policy==MixPolicy::HighestExposure&&part.value>selected)selected=part.value;
        if(policy==MixPolicy::LowestExposure&&part.value<selected)selected=part.value;
    }
    const auto numerator=static_cast<std::int64_t>(damage)*(policy==MixPolicy::SplitWeighted?weighted:selected);
    const auto denominator=policy==MixPolicy::SplitWeighted?total*10000:10000;
    // Signed division truncates once, after weighted portions have combined.
    // The result is data; the native writer remains the only owner of HP/healing.
    return BoundedDamage(numerator/denominator);
}
inline AffinityValue NativeBase(unsigned bit,const NativeMasks& masks) noexcept {
    if(!bit||bit>0x80||(bit&(bit-1)))return {Error::InvalidInput};
    if(masks.weak&bit)return {Error::Ok,15000};
    if(masks.absorb&bit)return {Error::Ok,-10000};
    if(masks.nulls&bit)return {Error::Ok,0};
    if(masks.resist&bit)return {Error::Ok,5000};
    return {};
}
inline std::int32_t NativeSignedBits(std::uint32_t bits) noexcept {
    std::int32_t value=0;std::memcpy(&value,&bits,sizeof(value));return value;
}
inline DamageResult ResolveNativeExact(std::int32_t damage,unsigned mask,const NativeMasks& masks) noexcept {
    if(mask>0xFF)return {Error::InvalidInput,damage};
    const unsigned weak=mask&masks.weak;
    if(weak){
        // FFX.exe SHA 78ce3439..., RVA 0x38A420, PE32/i386: every matching
        // weakness executes LEA (wrapping 32-bit times three), then signed /2.
        // Keep this compatibility path separate from the new saturating policy.
        for(unsigned bit=1;bit<256;bit<<=1)if(weak&bit)
            damage=NativeSignedBits(static_cast<std::uint32_t>(damage)*3u)/2;
        return {Error::Ok,damage};
    }
    const unsigned absorbed=masks.absorb,nulls=masks.nulls,resisted=masks.resist;
    if(mask&~(resisted|nulls|absorbed))return {Error::Ok,damage};
    if(mask&resisted&~(nulls|absorbed))return {Error::Ok,damage/2};
    if(mask&nulls&~absorbed)return {Error::Ok,0};
    if(mask&absorbed)return {Error::Ok,NativeSignedBits(0u-static_cast<std::uint32_t>(damage))};
    return {Error::Ok,damage};
}
}
