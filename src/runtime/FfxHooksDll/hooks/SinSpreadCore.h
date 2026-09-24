#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// Curated metadata from the user's Spira Reforge Macalania UNI catalog. The
// natural roster is the same nine exact field/monster pairs already admitted by
// SinRamScalingCore. Boss/scripted encounters do not gain authority here.
namespace FfxHooks::SinSpread {
enum class Distribution : unsigned { Random=0, Few=20, Half=50, Most=80 };
struct CurseEntry { unsigned id,threat;const char* name;const char* description; };
inline constexpr CurseEntry kCurses[]={
    {0,0,"No curse","Original monster behavior and size."},
    {1,1,"Opening Veil","Haste, Protect and Shell on the first turn."},
    {2,2,"Counter March","Counterattack after taking a hit."},
    {3,2,"Low HP Rush","Gain Haste below half health."},
    {4,1,"Ward Stack","Shell, Regen, NulBlaze and NulShock at the opening."},
    {5,2,"Frost-Flood Weave","Blizzara and Watera pressure on the front line."},
    {6,2,"Shoreline Break","Watera against the front line."},
    {7,1,"Sin Salve","Self-healing below half health."},
    {8,2,"Mist Chorus","White Wind for allied monsters below half health."},
};
struct MonsterEntry { std::uint16_t id,field;unsigned allowedMask;const char* name; };
inline constexpr MonsterEntry kMonsters[]={
    {3,310,0x4D,"Murussu"},{26,310,0x0F,"Iguion"},{33,310,0x27,"Wasp"},
    {81,310,0x3D,"Blue Element"},{217,310,0x6F,"Xiphos"},{87,310,0xAD,"Chimera"},
    {4,340,0x0F,"Mafdet"},{12,340,0x2F,"Snow Wolf"},{19,340,0x3D,"Ice Flan"},{37,340,0x2F,"Evil Eye"},
};
inline const MonsterEntry* FindMonster(unsigned id) noexcept {
    for(const auto& value:kMonsters)if(value.id==id)return &value;
    return nullptr;
}
inline const CurseEntry& Curse(unsigned id) noexcept {return kCurses[id<9?id:0];}
inline bool ValidDistribution(unsigned value) noexcept {return value==0 || value==20 || value==50 || value==80;}
inline bool ParseSeed(const char* text,std::uint32_t* output) noexcept {
    if(!text || !*text || !output)return false;
    std::uint64_t value=0;unsigned count=0;
    for(;*text;++text){if(*text<'0' || *text>'9' || ++count>10)return false;value=value*10u+static_cast<unsigned>(*text-'0');if(value>0xFFFFFFFFull)return false;}
    *output=static_cast<std::uint32_t>(value);return true;
}
inline const char* DistributionName(Distribution value) noexcept {
    switch(value){case Distribution::Few:return "20%";case Distribution::Half:return "50%";
    case Distribution::Most:return "80%";default:return "Random";}
}
inline const char* AreaName(unsigned field) noexcept {
    return field==310?"Macalania Woods":field==340?"Macalania Snowfield":"Outside the current region";
}
inline std::uint32_t Mix(std::uint32_t value) noexcept {
    value^=value>>16;value*=0x7FEB352Du;value^=value>>15;value*=0x846CA68Bu;return value^(value>>16);
}
inline unsigned Quota(unsigned count,Distribution mode) noexcept {
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
    // Explicit encounter IDs, not terrain IDs or truncated field aliases.
    // mcfr00/03 share the forest roster; maca00/03 and mcyt00 share its snowy roster.
    switch(field){case 310:case 313:return 310;case 330:case 333:case 340:return 340;default:return 0;}
}
struct AreaAssignment {
    bool active=false,supported=false;
    std::uint16_t field=0;
    std::uint32_t seed=0,visit=0;
    unsigned count=0,cursed=0;
    std::array<AssignedCurse,10> monsters{};
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
    std::array<unsigned,10> order{};
    for(unsigned i=0;i<result.count;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+result.count,[&](unsigned a,unsigned b){
        const auto x=Mix(entropy^result.monsters[a].monster),y=Mix(entropy^result.monsters[b].monster);
        return x==y?a<b:x<y;
    });
    for(unsigned rank=0;rank<result.cursed;++rank){
        auto& assignment=result.monsters[order[rank]];
        const auto* entry=FindMonster(assignment.monster);
        std::array<unsigned,8> choices{};unsigned count=0;
        for(unsigned id=1;id<=8;++id)if(entry->allowedMask&(1u<<(id-1u)))choices[count++]=id;
        assignment.curse=choices[Mix(entropy^Mix(assignment.monster)^0x434F5253u)%count];
        assignment.threat=Curse(assignment.curse).threat;
        assignment.scalePercent=100+assignment.threat*10;
    }
    return result;
}
} // namespace FfxHooks::SinSpread
