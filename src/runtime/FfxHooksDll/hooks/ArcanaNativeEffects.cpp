#include "ArcanaNativeEffects.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace FfxHooks::Arcana::NativeEffects {
namespace {
constexpr std::uint32_t callers[]={0x3868A7,0x3868CF,0x3868E1,0x3868F3,0x386905,0x386917,
    0x38692C,0x38693E,0x386950,0x386962,0x386972,0x386982};
constexpr unsigned offsets[]={0x24,0x28,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36};
constexpr EffectKind percents[]={EffectKind::HpPercent,EffectKind::MpPercent,EffectKind::StrengthPercent,
    EffectKind::DefensePercent,EffectKind::MagicPercent,EffectKind::MagicDefensePercent,
    EffectKind::None,EffectKind::None,EffectKind::None,EffectKind::None};
constexpr EffectKind flats[]={EffectKind::None,EffectKind::None,EffectKind::None,EffectKind::None,
    EffectKind::None,EffectKind::None,EffectKind::None,EffectKind::LuckFlat,EffectKind::EvasionFlat,EffectKind::AccuracyFlat};
unsigned Get(const unsigned char* p,unsigned size) noexcept {
    unsigned value=0;for(unsigned i=0;i<size;++i)value|=unsigned(p[i])<<(8*i);return value;
}
void Put(unsigned char* p,unsigned value,unsigned size) noexcept {
    for(unsigned i=0;i<size;++i)p[i]=static_cast<unsigned char>(value>>(8*i));
}
void OrWord(unsigned char* p,unsigned bits) noexcept {Put(p,Get(p,2)|bits,2);}
}
int ClampRole(std::uint32_t caller) noexcept {
    for(unsigned i=0;i<12;++i)if(callers[i]==caller)return static_cast<int>(i);
    return -1;
}
void AdjustClamp(int role,int native,int minimum,int& maximum,int& value,const Effects& effects,Shadow& shadow) noexcept {
    if(role<0||role>=10||minimum<0||maximum<minimum)return;
    const unsigned index=static_cast<unsigned>(role),width=index<2?4u:1u;
    const int baseline=(std::clamp)(native,minimum,maximum);
    Put(shadow.baseline.data()+offsets[index],static_cast<unsigned>(baseline),width);
    shadow.seen|=static_cast<std::uint16_t>(1u<<index);
    if(index==0&&effects.Get(EffectKind::BreakHp))maximum=(std::max)(maximum,99999);
    if(native<0){value=baseline;return;}
    const auto adjusted=BoostStat(static_cast<std::uint32_t>(native),effects.Get(percents[index]),
                                  effects.Get(flats[index]),static_cast<std::uint32_t>(maximum));
    value=(std::max)(minimum,static_cast<int>(adjusted));
}
void MergeFlags(unsigned char* flags,const Effects& e) noexcept {
    struct Flag {EffectKind kind;unsigned word,bit;};
    constexpr Flag mapping[]={
        {EffectKind::Sensor,0,0},{EffectKind::FirstStrike,0,1},{EffectKind::Counter,0,3},
        {EffectKind::EvadeCounter,0,4},{EffectKind::MagicCounter,0,5},{EffectKind::AutoPotion,0,10},
        {EffectKind::AutoPhoenix,0,12},{EffectKind::Piercing,0,13},{EffectKind::HalfMp,0,14},
        {EffectKind::MasterThief,1,8},{EffectKind::BreakHp,1,9},{EffectKind::BreakDamage,1,11}};
    for(const auto& flag:mapping)if(e.Get(flag.kind))OrWord(flags+flag.word*2,1u<<flag.bit);
}
void MergeBattle(Battle& data,const Effects& e) noexcept {
    const bool nativeHalf=(Get(data.data()+0x17C,2)&0x4000)!=0;
    MergeFlags(data.data()+0x17C,e);
    // Battle MP costs share one card reduction layer. Field menus still use
    // the native Half MP flag; pre-existing native battle flags remain owned.
    if(e.Get(EffectKind::HalfMp)&&!nativeHalf)Put(data.data()+0x17C,Get(data.data()+0x17C,2)&~0x4000u,2);
    struct Bit {EffectKind kind;unsigned bit;};
    constexpr Bit strikes[]={{EffectKind::StrikeFire,1},{EffectKind::StrikeIce,2},{EffectKind::StrikeLightning,4},{EffectKind::StrikeWater,8},{EffectKind::StrikeHoly,16},
        {EffectKind::StrikeShadow,0x80},{EffectKind::StrikeEarth,0x20},{EffectKind::StrikeWind,0x40}};
    constexpr Bit wards[]={{EffectKind::WardFire,1},{EffectKind::WardIce,2},{EffectKind::WardLightning,4},{EffectKind::WardWater,8},{EffectKind::WardHoly,16},
        {EffectKind::WardShadow,0x80},{EffectKind::WardEarth,0x20},{EffectKind::WardWind,0x40}};
    for(const auto& bit:strikes)if(e.Get(bit.kind))data[0x99]|=static_cast<unsigned char>(bit.bit);
    for(const auto& bit:wards)if(e.Get(bit.kind))data[0x9C]|=static_cast<unsigned char>(bit.bit);
    constexpr Bit status[]={{EffectKind::AutoShell,3},{EffectKind::AutoProtect,4},{EffectKind::AutoReflect,5},
        {EffectKind::AutoRegen,10},{EffectKind::AutoHaste,11}};
    for(const auto& bit:status)if(e.Get(bit.kind))OrWord(data.data()+0xF2,1u<<bit.bit);
    if(e.Get(EffectKind::SosRegen)){data[0xFC]=1;OrWord(data.data()+0xF8,0x400);}
    constexpr Bit proofs[]={{EffectKind::ProofDark,14},{EffectKind::ProofSilence,13},{EffectKind::ProofSleep,12},
        {EffectKind::ProofPoison,3},{EffectKind::ProofConfuse,8},{EffectKind::ProofStone,2},{EffectKind::ProofDeath,0},{EffectKind::ProofSlow,24}};
    for(const auto& proof:proofs)if(e.Get(proof.kind))data[0x101+proof.bit]=255;
    constexpr Bit attacks[]={{EffectKind::TouchDark,14},{EffectKind::TouchSilence,13},{EffectKind::TouchSleep,12},
        {EffectKind::TouchSlow,24},{EffectKind::TouchPoison,3},{EffectKind::TouchDeath,0},
        {EffectKind::TouchArmorBreak,6},{EffectKind::TouchMentalBreak,7},{EffectKind::TouchStone,2},{EffectKind::TouchConfuse,8}};
    for(const auto& attack:attacks){
        const int chance=e.Get(attack.kind);if(!chance)continue;
        auto& native=data[0x9E + attack.bit];
        native=static_cast<unsigned char>((std::max)(int(native),(std::min)(254,chance)));
        if(attack.bit>=12){
            auto& duration=data[0xB7+attack.bit-12];
            duration=static_cast<unsigned char>((std::max)(int(duration),3));
        }
    }
}
Projection Project(Player& image,const Shadow& shadow) noexcept {
    Projection result;
    if(!shadow.valid)return result;
    result.hp=Get(image.data()+0x1C,4);result.mp=Get(image.data()+0x20,4);
    for(unsigned i=0;i<11;++i){
        const unsigned offset=i<10?offsets[i]:0x4Au,width=i<2?4u:i<10?1u:6u;
        auto* field=image.data()+offset;
        if(!std::memcmp(field,shadow.applied.data()+offset,width)){
            if(std::memcmp(field,shadow.baseline.data()+offset,width)){std::memcpy(field,shadow.baseline.data()+offset,width);result.changed=true;}
        }else if(std::memcmp(field,shadow.baseline.data()+offset,width))result.conflict=true;
    }
    const unsigned hp=(std::min)(result.hp,Get(image.data()+0x24,4));
    const unsigned mp=(std::min)(result.mp,Get(image.data()+0x28,4));
    if(hp!=result.hp||mp!=result.mp)result.changed=true;
    Put(image.data()+0x1C,hp,4);Put(image.data()+0x20,mp,4);
    return result;
}
}
