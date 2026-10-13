#pragma once
#include "OmegaUltimaCore.h"
#include "ElementPackJson.h"
#include <string_view>

namespace FfxHooks::OmegaUltima {
struct Manifest {
    unsigned cloneSize=0;std::uint64_t cloneHash=0;
    std::array<FormationProof,FormationCount> formations{};
};
enum class ManifestCode {Invalid,Disabled,Unresolved,Ready};
inline bool JsonUnsigned(const ElementalDominion::Json::Value& row,const char* key,unsigned maximum,unsigned& output) noexcept {
    const auto* v=row.Find(key);if(!v||v->type!=ElementalDominion::Json::Type::Integer||v->integer<0||
       static_cast<std::uint64_t>(v->integer)>maximum)return false;
    output=static_cast<unsigned>(v->integer);return true;
}
inline bool JsonHash(const ElementalDominion::Json::Value& row,const char* key,std::uint64_t& output) noexcept {
    const auto* v=row.Find(key);if(!v||v->type!=ElementalDominion::Json::Type::String||v->text.size()!=16)return false;
    output=0;for(char c:v->text){const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(n<0)return false;
        output=(output<<4)|static_cast<unsigned>(n);}
    return output!=0;
}
inline ManifestCode ParseManifest(std::string_view text,Manifest& output) noexcept {
    namespace J=ElementalDominion::Json;output={};J::Value root;J::Problem problem;J::Limits limits;
    limits.bytes=32768;limits.nodes=512;limits.depth=4;limits.arrayItems=FormationCount;limits.members=8;limits.stringBytes=96;
    if(!J::Parse(text,root,problem,limits))return ManifestCode::Invalid;
    const auto* schema=root.Find("schema");const auto* flag=root.Find("enabled");
    if(!schema||schema->type!=J::Type::String||schema->text!="ffx.spira.omega-ultima.v1"||!flag||flag->type!=J::Type::Boolean)return ManifestCode::Invalid;
    if(!flag->boolean)return ManifestCode::Disabled;
    const auto* clone=root.Find("clone");const auto* formations=root.Find("formations");
    if(!clone||clone->type==J::Type::Null||!formations||formations->type!=J::Type::Array||formations->children.size()!=FormationCount)return ManifestCode::Unresolved;
    Manifest candidate;unsigned model=0;
    if(!JsonUnsigned(*clone,"id",Clone,model)||model!=Clone||!JsonUnsigned(*clone,"size",1024*1024,candidate.cloneSize)||candidate.cloneSize<36||
       !JsonHash(*clone,"hash",candidate.cloneHash))return ManifestCode::Invalid;
    std::uint64_t seen=0;
    for(unsigned i=0;i<FormationCount;++i){const auto& row=formations->children[i];auto& proof=candidate.formations[i];
        if(!JsonUnsigned(row,"field",1077,proof.field)||!JsonUnsigned(row,"index",18,proof.index)||!OmegaEncounter::PackageEligible(proof.field,proof.index)||
           !JsonUnsigned(row,"size",65535,proof.size)||proof.size<0x34||!JsonHash(row,"hash",proof.hash))return ManifestCode::Invalid;
        const auto bit=std::uint64_t(1)<<((proof.field-1076)*32+proof.index);if(seen&bit)return ManifestCode::Invalid;seen|=bit;
    }
    output=candidate;return ManifestCode::Ready;
}
} // namespace FfxHooks::OmegaUltima
