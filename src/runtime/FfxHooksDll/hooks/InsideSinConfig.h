#pragma once
// Jarvis-HOOK: all-or-none command references. Text offsets are intentionally
// excluded; the translated kernel must retain identical gameplay/VFX bytes.
#include "InsideSinCore.h"
#include "ElementPackJson.h"
#include <cstring>
#include <string_view>

namespace FfxHooks::InsideSin {
using CommandRow=std::array<std::uint8_t,92>;
inline bool Semantics(Preset preset,const CommandRow& row) noexcept {
    const auto* d=Get(preset);if(!d)return false;
    if((row[0x20]&0x33)!=(d->physical?1:2)||row[0x23]!=1||row[0x28]!=(d->physical?1:3)||
       row[0x2A]!=d->power||row[0x2B]!=1||row[0x2D]!=d->element||row[0x25]||row[0x26]||
       row[0x2C]||row[0x1D]||(row[0x1F]&~0x15u)||(row[0x1E]&~0x33u)||row[0x21]||!(row[0x1C]&2)||
       (row[0x1A]&0x40)||(!d->all&&(row[0x1A]&4)))return false;
    // These commands cannot inherit Curse, Cheer/Focus, OD categories, bonus
    // stats or special effects from their donor. Native status chances suffice.
    for(unsigned i=0x54;i<row.size();++i)if(row[i])return false;
    std::array<std::uint8_t,25> chance{};std::array<std::uint8_t,13> duration{};
    if(preset==Preset::Oppressor){chance[24]=75;duration[12]=3;}
    if(preset==Preset::Tormentor){chance[3]=75;chance[13]=50;duration[1]=3;}
    if(preset==Preset::Breaker)chance[6]=75;
    for(unsigned i=0;i<5;++i)if(d->buffs&(1u<<i)){chance[ChanceIndices[i]]=255;duration[DurationIndices[i]]=RemovableDuration;}
    return !std::memcmp(row.data()+0x2E,chance.data(),chance.size())&&
        !std::memcmp(row.data()+0x47,duration.data(),duration.size());
}
inline void FilterExistingBuffs(CommandRow& row,unsigned present) noexcept {
    for(unsigned i=0;i<5;++i)if(present&(1u<<i)){
        row[0x2Eu+ChanceIndices[i]]=0;row[0x47+DurationIndices[i]]=0;
    }
}
struct Binding {std::uint16_t id=0;CommandRow row{};};
struct Bindings {
    std::array<Binding,7> commands{};
    bool Ready() const noexcept {
        for(unsigned i=0;i<commands.size();++i){
            const auto& c=commands[i];if((c.id>>12)!=6||!Semantics(Definitions[i].preset,c.row))return false;
            for(unsigned j=0;j<i;++j)if(c.id==commands[j].id)return false;
        }
        return true;
    }
    const Binding* GetBinding(Preset preset) const noexcept {
        const unsigned i=static_cast<unsigned>(preset);return i&&i<=commands.size()?&commands[i-1]:nullptr;
    }
    Preset Find(unsigned id) const noexcept {
        for(unsigned i=0;i<commands.size();++i)if(commands[i].id&&commands[i].id==id)return Definitions[i].preset;
        return Preset::None;
    }
    bool Matches(Preset preset,const CommandRow& row) const noexcept {
        const auto* entry=GetBinding(preset);
        return entry&&entry->id&&Semantics(preset,row)&&!std::memcmp(row.data()+16,entry->row.data()+16,row.size()-16);
    }
};
enum class BindingCode {Ready,Disabled,Invalid,Unresolved};
inline int Hex(char value) noexcept {
    return value>='0'&&value<='9'?value-'0':value>='a'&&value<='f'?value-'a'+10:
        value>='A'&&value<='F'?value-'A'+10:-1;
}
inline BindingCode ParseBindings(std::string_view input,Bindings& output,bool& enabled) noexcept {
    namespace J=ElementalDominion::Json;
    output={};enabled=false;J::Value root;J::Problem problem;
    J::Limits limits;limits.bytes=16384;limits.nodes=128;limits.depth=4;limits.arrayItems=7;limits.stringBytes=256;limits.members=8;
    if(!J::Parse(input,root,problem,limits))return BindingCode::Invalid;
    const auto* schema=root.Find("schema");const auto* flag=root.Find("enabled");const auto* rows=root.Find("commands");
    if(!schema||schema->type!=J::Type::String||schema->text!="ffx.inside-sin.v1"||!flag||flag->type!=J::Type::Boolean||
       !rows||rows->type!=J::Type::Array)return BindingCode::Invalid;
    if(!flag->boolean)return BindingCode::Disabled;
    if(rows->children.size()!=Definitions.size())return BindingCode::Unresolved;
    Bindings candidate;unsigned seen=0;
    for(const auto& row:rows->children){
        const auto* key=row.Find("key");const auto* id=row.Find("id");const auto* hex=row.Find("gameplay_hex");
        if(!key||key->type!=J::Type::String||!id||!hex)return BindingCode::Invalid;
        unsigned index=7;for(unsigned i=0;i<7;++i)if(key->text==Definitions[i].command){index=i;break;}
        if(index==7||(seen&(1u<<index)))return BindingCode::Invalid;
        seen|=1u<<index;
        if(id->type==J::Type::Null||hex->type==J::Type::Null)return BindingCode::Unresolved;
        if(id->type!=J::Type::Integer||id->integer<0x6000||id->integer>0x6FFF||hex->type!=J::Type::String||
           hex->text.size()!=152)return BindingCode::Invalid;
        auto& binding=candidate.commands[index];binding.id=static_cast<std::uint16_t>(id->integer);
        for(unsigned byte=0;byte<76;++byte){const int a=Hex(hex->text[byte*2]),b=Hex(hex->text[byte*2+1]);
            if(a<0||b<0)return BindingCode::Invalid;
            binding.row[16+byte]=static_cast<std::uint8_t>((a<<4)|b);
        }
    }
    if(!candidate.Ready())return BindingCode::Invalid;
    output=candidate;enabled=true;return BindingCode::Ready;
}
} // namespace FfxHooks::InsideSin
