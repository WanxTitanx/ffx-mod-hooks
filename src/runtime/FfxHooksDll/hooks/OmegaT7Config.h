#pragma once
#include "OmegaT7Core.h"
#include "ElementPackJson.h"
#include <cstring>
#include <string_view>
namespace FfxHooks::OmegaT7 {
using CommandRow=std::array<unsigned char,92>;
enum class Target : unsigned {PartyRandom,PartyAll,Self,Monsters};
inline constexpr std::array<const char*,26> SkillKeys{{"darkja","holy","aeroja","firaja","blizzaja","thundaja","waterja","anti_flare","meteor","ultima","bio","silence","slowga","darkness","zombie","mental_break","armor_break","reflect","mighty_guard","regen","hastega","focus","cheer","luck","holy_flare","flare"}};
inline constexpr Target TargetFor(Skill skill) noexcept {
    return skill==Skill::MightyGuard||skill==Skill::Hastega?Target::Monsters:
        skill==Skill::Reflect||skill==Skill::Regen||skill==Skill::Focus||skill==Skill::Cheer||skill==Skill::Luck?Target::Self:
        skill==Skill::Aeroja||skill==Skill::Ultima||skill==Skill::Slowga?Target::PartyAll:Target::PartyRandom;
}
inline constexpr bool Offensive(Skill skill) noexcept {const auto t=TargetFor(skill);return t==Target::PartyRandom||t==Target::PartyAll;}
inline constexpr unsigned BuffsFor(Skill skill) noexcept {return skill==Skill::Reflect?Reflect:skill==Skill::Regen?Regen:skill==Skill::Hastega?Haste:skill==Skill::MightyGuard?Shell|Protect:0;}
inline constexpr unsigned StackFor(Skill skill) noexcept {
    if(skill==Skill::Cheer)return Cheer;
    if(skill==Skill::Focus)return Focus;
    if(skill==Skill::Luck)return Luck;
    return 4;
}
inline constexpr unsigned NativeStackIndex[4]={0,2,4,3};
inline constexpr unsigned BuffOffsets[9]={0x60B,0x60C,0x60D,0x612,0x613,0x60F,0x611,0x610,0x60E};
inline constexpr unsigned ChanceIndex[9]={15,16,17,22,23,19,21,20,18};
inline constexpr unsigned DurationIndex[9]={3,4,5,10,11,7,9,8,6};
inline constexpr unsigned PropertyIndex[9]={0x30,0x31,0x32,0x37,0x38,0x34,0x36,0x35,0x33};
inline bool DamageProtocol(Skill skill,const CommandRow& row) noexcept {
    const auto matches=[&](unsigned formula,unsigned type,unsigned flags){return row[0x28]==formula&&row[0x23]==type&&row[0x20]==flags;};
    // Existing manifests remain usable, but corrected donor protocols are
    // finite per skill. The exact 76-byte manifest witness still owns the row.
    if(matches(3,1,2))return true;
    switch(skill){
        case Skill::Bio:case Skill::Silence:case Skill::Darkness:case Skill::Zombie:
        case Skill::Reflect:case Skill::MightyGuard:case Skill::Regen:return matches(3,0,2);
        case Skill::Slowga:return matches(13,4,2);
        case Skill::Hastega:return matches(13,4,18);
        case Skill::Focus:case Skill::Cheer:case Skill::Luck:return matches(0,0,0);
        case Skill::MentalBreak:case Skill::ArmorBreak:return matches(1,1,13);
        default:return false;
    }
}
inline bool Semantics(Skill skill,const CommandRow& row) noexcept {
    if(static_cast<unsigned>(skill)>=26||row[0x25]||row[0x26]||row[0x57]||(row[0x1F]&0x42)||!DamageProtocol(skill,row)||row[0x2B]!=1)return false;
    const bool offensive=Offensive(skill);const auto target=TargetFor(skill);
    if(row[0x1A]!=(target==Target::PartyRandom?0xB3:target==Target::PartyAll?0x87:target==Target::Monsters?0xB5:0xB1))return false;
    if(offensive){
        for(unsigned i=15;i<24;++i)if(row[0x2E + i])return false;
        if(row[0x56]||row[0x58]||row[0x59]||row[0x5A]||row[0x5B])return false;
        for(unsigned i=0;i<25;++i)if(row[0x2E + i]>100)return false;
    }else{
        if(row[0x2A]||row[0x2D])return false;
        for(unsigned i=0;i<15;++i)if(row[0x2E + i])return false;
        if(row[0x2E + 24]||(target==Target::Monsters&&(row[0x1F]&4)))return false;
        const auto buffs=BuffsFor(skill);
        for(unsigned i=0;i<9;++i){const bool wanted=(buffs&(1u<<i))!=0;
            if(row[0x2E + ChanceIndex[i]]!=(wanted?255:0)||row[0x47+DurationIndex[i]]!=(wanted?254:0))return false;}
        const auto stack=StackFor(skill);const unsigned mask=stack<4?1u<<NativeStackIndex[stack]:0;
        if(row[0x56]!=mask||row[0x59]!=(mask?1:0))return false;
    }
    return true;
}
struct Binding {unsigned id=0;CommandRow row{};};
struct Bindings {
    std::array<Binding,26> rows{};
    bool Ready() const noexcept {
        for(unsigned i=0;i<rows.size();++i){const auto& r=rows[i];if((r.id>>12)!=6||!Semantics(static_cast<Skill>(i),r.row))return false;
            for(unsigned j=0;j<i;++j)if(rows[j].id==r.id)return false;}
        return true;
    }
    const Binding& Get(Skill skill) const noexcept {return rows[static_cast<unsigned>(skill)];}
    unsigned Find(unsigned id) const noexcept {for(unsigned i=0;i<rows.size();++i)if(rows[i].id==id&&id)return i;return 26;}
    bool Matches(Skill skill,const CommandRow& row) const noexcept {return Semantics(skill,row)&&!std::memcmp(Get(skill).row.data()+16,row.data()+16,76);}
};
enum class BindingCode {Invalid,Disabled,Unresolved,Ready};
inline BindingCode ParseBindings(std::string_view input,Bindings& output) noexcept {
    namespace J=ElementalDominion::Json;output={};J::Value root;J::Problem problem;J::Limits limits;
    limits.bytes=32768;limits.nodes=256;limits.depth=4;limits.arrayItems=26;limits.stringBytes=256;limits.members=8;
    if(!J::Parse(input,root,problem,limits))return BindingCode::Invalid;
    const auto* schema=root.Find("schema");const auto* enabled=root.Find("enabled");const auto* rows=root.Find("commands");
    if(!schema||schema->type!=J::Type::String||schema->text!="ffx.omega-t7.v1"||!enabled||enabled->type!=J::Type::Boolean)return BindingCode::Invalid;
    if(!enabled->boolean)return BindingCode::Disabled;
    if(!rows||rows->type!=J::Type::Array||rows->children.size()!=26)return BindingCode::Unresolved;
    Bindings next;unsigned seen=0;
    for(const auto& entry:rows->children){const auto* key=entry.Find("key");const auto* id=entry.Find("id");const auto* hex=entry.Find("gameplay_hex");
        if(!key||key->type!=J::Type::String||!id||!hex)return BindingCode::Invalid;
        unsigned index=26;for(unsigned i=0;i<26;++i)if(key->text==SkillKeys[i]){index=i;break;}
        if(index==26||(seen&(1u<<index)))return BindingCode::Invalid;
        seen|=1u<<index;
        if(id->type==J::Type::Null||hex->type==J::Type::Null)return BindingCode::Unresolved;
        if(id->type!=J::Type::Integer||id->integer<0x6000||id->integer>0x6FFF||hex->type!=J::Type::String||hex->text.size()!=152)return BindingCode::Invalid;
        auto& binding=next.rows[index];binding.id=static_cast<unsigned>(id->integer);
        const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
        for(unsigned i=0;i<76;++i){const int a=digit(hex->text[2*i]),b=digit(hex->text[2*i+1]);if(a<0||b<0)return BindingCode::Invalid;binding.row[16+i]=static_cast<unsigned char>((a<<4)|b);}
    }
    if(!next.Ready())return BindingCode::Invalid;
    output=next;return BindingCode::Ready;
}
inline bool NativeOd(unsigned id) noexcept {return (id>=0x61B9&&id<=0x61BB)||(id>=0x61DE&&id<=0x61E0);}
}
