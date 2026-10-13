#pragma once
#include "OmegaLastSealCore.h"
#include "ElementPackJson.h"
#include <array>
#include <cstring>
#include <string_view>

namespace FfxHooks::OmegaLastSeal {
using CommandRow=std::array<unsigned char,92>;
inline constexpr std::array<unsigned,21> RequiredCommands{{Attack,CoreEnergy,Rain,0x4067,AntiFlare,HolyFlare,Hastega,0x61D6,Rupture,Reprimand,Reconstitution,Concentration,LastSeal,Supernova,Protectga,Shellga,Dispelga,HolyAttack,DarkAttack,HolyFury,DarkFury}};
struct ResourceProof {unsigned id=0,size=0;std::uint64_t hash=0;};
struct CommandProof {unsigned id=0;CommandRow row{};};
struct Bindings {
    std::array<ResourceProof,5> monsters{};ResourceProof formation{};
    std::array<CommandProof,RequiredCommands.size()> commands{};
    const CommandProof* Find(unsigned id) const noexcept {for(const auto& row:commands)if(row.id==id)return &row;return nullptr;}
    bool Matches(unsigned id,const CommandRow& row) const noexcept {const auto* proof=Find(id);return proof&&!std::memcmp(proof->row.data()+16,row.data()+16,76);}
};
inline bool ParseBindings(std::string_view bytes,Bindings& output) noexcept {
    namespace J=ElementalDominion::Json;output={};J::Value root;J::Problem problem;J::Limits limits;
    limits.bytes=32768;limits.nodes=256;limits.depth=4;limits.members=8;limits.arrayItems=21;limits.stringBytes=200;
    if(!J::Parse(bytes,root,problem,limits))return false;
    const auto* schema=root.Find("schema");const auto* enabled=root.Find("enabled");const auto* monsters=root.Find("monsters");const auto* formation=root.Find("formation");const auto* commands=root.Find("commands");
    if(!schema||schema->type!=J::Type::String||schema->text!="ffx.spira.omega-last-seal.v1"||!enabled||enabled->type!=J::Type::Boolean||!enabled->boolean||
       !monsters||monsters->type!=J::Type::Array||monsters->children.size()!=5||!formation||!commands||commands->type!=J::Type::Array||commands->children.size()!=21)return false;
    const auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    const auto resource=[&](const J::Value& value,ResourceProof& out,unsigned maximum){
        const auto* size=value.Find("size");const auto* hash=value.Find("hash");
        if(!size||size->type!=J::Type::Integer||size->integer<36||size->integer>maximum||!hash||hash->type!=J::Type::String||hash->text.size()!=16)return false;
        out.size=static_cast<unsigned>(size->integer);for(char ch:hash->text){const int digit=hex(ch);if(digit<0)return false;out.hash=(out.hash<<4)|static_cast<unsigned>(digit);}return out.hash!=0;
    };
    Bindings next;constexpr unsigned ids[]={100,911,912,913,914};
    for(unsigned i=0;i<5;++i){const auto& row=monsters->children[i];const auto* id=row.Find("id");if(!id||id->type!=J::Type::Integer||id->integer!=ids[i]||!resource(row,next.monsters[i],1024*1024))return false;next.monsters[i].id=ids[i];}
    if(!resource(*formation,next.formation,65535))return false;
    for(unsigned i=0;i<21;++i){const auto& row=commands->children[i];const auto* id=row.Find("id");const auto* data=row.Find("gameplay_hex");
        if(!id||id->type!=J::Type::Integer||id->integer!=RequiredCommands[i]||!data||data->type!=J::Type::String||data->text.size()!=152)return false;
        next.commands[i].id=RequiredCommands[i];for(unsigned n=0;n<76;++n){const auto a=hex(data->text[2*n]),b=hex(data->text[2*n+1]);if(a<0||b<0)return false;next.commands[i].row[16+n]=static_cast<unsigned char>((a<<4)|b);}
    }
    output=next;return true;
}
inline std::uint64_t Hash(const void* source,std::size_t size) noexcept {
    const auto* bytes=static_cast<const unsigned char*>(source);std::uint64_t hash=0xCBF29CE484222325ULL;for(std::size_t i=0;i<size;++i)hash=(hash^bytes[i])*0x100000001B3ULL;return hash;
}
}
