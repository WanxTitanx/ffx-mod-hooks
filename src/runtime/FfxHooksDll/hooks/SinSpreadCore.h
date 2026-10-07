#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include "SinSpreadCatalog.generated.h"

// Natural encounter authority is explicit per field/monster pair.
// The authored UNI compatibility matrix is generated from tools/sin_profiles/catalog.json.
namespace FfxHooks::SinSpread {
enum class Distribution : unsigned { Random=0, Few=20, Half=50, Most=80, All=100 };
inline const MonsterEntry* FindMonster(unsigned id,unsigned area=0) noexcept {
    for(const auto& value:kMonsters)if(value.id==id&&(!area||value.field==area))return &value;
    return nullptr;
}
inline const CurseEntry& Curse(unsigned id) noexcept {for(const auto& value:kCurses)if(value.id==id)return value;return kCurses[0];}
inline constexpr unsigned MaximumThreatForField(unsigned field) noexcept {
    for(const auto& value:kAreaAliases)if(value.field==field)return value.maxThreat;
    return 0;
}
inline constexpr bool NaturalMonsterPair(unsigned field,unsigned monster) noexcept {
    for(const auto& value:kNaturalPairs)if(value.field==field&&value.monster==monster)return true;
    return false;
}
inline bool ValidDistribution(unsigned value) noexcept {return value==0 || value==20 || value==50 || value==80 || value==100;}
inline bool ParseSeed(const char* text,std::uint32_t* output) noexcept {
    if(!text || !*text || !output)return false;
    std::uint64_t value=0;unsigned count=0;
    for(;*text;++text){if(*text<'0' || *text>'9' || ++count>10)return false;value=value*10u+static_cast<unsigned>(*text-'0');if(value>0xFFFFFFFFull)return false;}
    *output=static_cast<std::uint32_t>(value);return true;
}
inline const char* DistributionName(Distribution value) noexcept {
    switch(value){case Distribution::All:return "100%";case Distribution::Few:return "20%";case Distribution::Half:return "50%";
    case Distribution::Most:return "80%";default:return "Random";}
}
inline const char* AreaName(unsigned field) noexcept {
    unsigned area=field;for(const auto& alias:kAreaAliases)if(alias.field==field){area=alias.area;break;}
    for(const auto& value:kAreas)if(value.id==area)return value.name;
    return "Outside the current region";
}
inline unsigned NextArea(unsigned field,bool backwards=false) noexcept {
    unsigned index=0,count=static_cast<unsigned>(sizeof(kAreas)/sizeof(kAreas[0]));
    for(unsigned i=0;i<count;++i)if(kAreas[i].id==field){index=i;break;}
    return kAreas[(index+(backwards?count-1:1))%count].id;
}
struct PreviewPage {unsigned page=0,pages=1,first=0,count=0;};
inline PreviewPage Page(unsigned total,unsigned requested) noexcept {
    constexpr unsigned rows=6;const unsigned pages=(std::max)(1u,(total+rows-1)/rows);
    const unsigned page=requested%pages,first=page*rows;
    return {page,pages,first,(std::min)(rows,total-first)};
}
inline std::uint32_t Mix(std::uint32_t value) noexcept {
    value^=value>>16;value*=0x7FEB352Du;value^=value>>15;value*=0x846CA68Bu;return value^(value>>16);
}
inline unsigned Quota(unsigned count,Distribution mode) noexcept {
    if(mode==Distribution::All)return count;
    const unsigned cap=count*80u/100u;
    return (std::min)(cap,(count*static_cast<unsigned>(mode)+99u)/100u);
}
struct AssignedCurse {std::uint16_t monster=0;unsigned curse=0,threat=0,scalePercent=100;};
inline constexpr std::uint16_t NativeMonsterId(std::uint16_t model) noexcept {
    return model>0 && model<0x400u?static_cast<std::uint16_t>(0x1000u+model):0xFFFFu;
}
inline constexpr std::uint16_t ModelFromNative(std::uint16_t native) noexcept {
    return native>0x1000u && native<0x1400u?static_cast<std::uint16_t>(native-0x1000u):0xFFFFu;
}
inline constexpr std::uint16_t AreaForField(std::uint16_t field) noexcept {
    for(const auto& value:kAreaAliases)if(value.field==field)return value.area;
    return 0;
}
struct AreaAssignment {
    bool active=false,supported=false;
    std::uint16_t field=0;
    std::uint32_t seed=0,visit=0;
    unsigned count=0,cursed=0;
    std::array<AssignedCurse,kMaximumAreaMonsters> monsters{};
    const AssignedCurse* Find(unsigned monster) const noexcept {
        for(unsigned i=0;i<count;++i)if(monsters[i].monster==monster)return &monsters[i];
        return nullptr;
    }
};
inline AreaAssignment BuildAssignment(std::uint16_t field,std::uint32_t seed,std::uint32_t visit,
    Distribution mode,bool enabled) noexcept {
    AreaAssignment result{};result.field=field;result.seed=seed;result.visit=visit;
    const auto area=AreaForField(field);
    if(!area || !ValidDistribution(static_cast<unsigned>(mode)))return result;
    result.supported=true;result.active=enabled;
    for(const auto& entry:kMonsters)if(entry.field==area)result.monsters[result.count++].monster=entry.id;
    if(!enabled)return result;
    const std::uint32_t entropy=Mix(seed^Mix(visit+0x9E3779B9u)^Mix(area));
    result.cursed=mode==Distribution::Random?Mix(entropy)%(result.count*80u/100u+1u):Quota(result.count,mode);
    std::array<unsigned,kMaximumAreaMonsters> order{};
    for(unsigned i=0;i<result.count;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+result.count,[&](unsigned a,unsigned b){
        const auto x=Mix(entropy^result.monsters[a].monster),y=Mix(entropy^result.monsters[b].monster);
        return x==y?a<b:x<y;
    });
    for(unsigned rank=0;rank<result.cursed;++rank){
        auto& assignment=result.monsters[order[rank]];
        const auto* entry=FindMonster(assignment.monster,area);
        std::array<unsigned,kMaximumCurse> choices{};unsigned count=0;
        for(unsigned id=1;id<=kMaximumCurse;++id)if(entry->allowedMask&(std::uint64_t(1)<<(id-1u)))choices[count++]=id;
        assignment.curse=choices[Mix(entropy^Mix(assignment.monster)^0x434F5253u)%count];
        assignment.threat=Curse(assignment.curse).threat;
        assignment.scalePercent=100+assignment.threat*10;
    }
    return result;
}
} // namespace FfxHooks::SinSpread
