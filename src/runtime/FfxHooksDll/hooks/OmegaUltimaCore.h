#pragma once
// Jarvis-HOOK: pre-load selection only. Monster data and native AI are immutable.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "OmegaEncounter.h"

namespace FfxHooks::OmegaUltima {
inline constexpr unsigned Clone=910,CloneNative=0x138E,ChestNative=0x10D3;
inline constexpr unsigned CompletionOffset=0x3BE,Chance=10,FormationCount=27;
inline constexpr bool UltimaResolved(std::uint8_t flags) noexcept {return (flags&2)!=0;}
inline constexpr bool EligibleFormation(unsigned field,unsigned index) noexcept {
    return OmegaEncounter::Eligible(field,index);
}
inline constexpr unsigned ReservedMimic(unsigned index) noexcept {
    return index==13?0x10FB:index==14?0x10FC:index==15?0x10FD:index==16?0x1100:0;
}
inline std::uint16_t Word(const unsigned char* p) noexcept {return static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));}
inline std::uint32_t Dword(const unsigned char* p) noexcept {return Word(p)|(std::uint32_t(Word(p+2))<<16);}
inline std::uint64_t Hash(const unsigned char* bytes,std::size_t size) noexcept {
    if(!bytes)return 0;
    std::uint64_t hash=0xCBF29CE484222325ULL;
    for(std::size_t i=0;i<size;++i){hash^=bytes[i];hash*=0x100000001B3ULL;}return hash;
}
struct FormationProof {unsigned field=0,index=0,size=0;std::uint64_t hash=0;};
struct Layout {unsigned slots=0,eligible=0;};
inline unsigned Count(unsigned mask) noexcept {unsigned n=0;for(;mask;mask&=mask-1)++n;return n;}
inline bool Inspect(const FormationProof& proof,const unsigned char* bytes,std::size_t size,Layout& output) noexcept {
    output={};
    if(!OmegaEncounter::PackageEligible(proof.field,proof.index)||!bytes||size<0x34||size>65535||size!=proof.size||
       !proof.hash||Hash(bytes,size)!=proof.hash||Dword(bytes)!=8||Dword(bytes+32)!=size)return false;
    const auto relative=Dword(bytes+12);
    if(relative<0x18||relative>size-28)return false;
    const unsigned slots=relative+12;
    // Header chunk offsets are relative to their own offset-table entries.
    if(Dword(bytes+16)<slots+16||Dword(bytes+16)>size)return false;
    const bool transforming=proof.field==1076&&ReservedMimic(proof.index);
    const unsigned stored=transforming?6:5;unsigned eligible=0;
    for(unsigned i=0;i<8;++i){const unsigned id=Word(bytes+slots+i*2);
        if(i>=stored){if(id!=0xFFFF)return false;continue;}
        if(id<=0x1000||id>=0x1400||id==CloneNative)return false;
        if(id!=ChestNative&&(!transforming||(i!=1&&i!=2)))eligible|=1u<<i;
    }
    if(transforming&&(Word(bytes+slots+2)!=ChestNative||Word(bytes+slots+4)!=ReservedMimic(proof.index)||eligible!=0x39))return false;
    if(!eligible)return false;
    output={slots,eligible};return true;
}
struct Random {void* context=nullptr;bool(*draw)(void*,unsigned,unsigned&) noexcept=nullptr;};
struct Selection {bool selected=false;unsigned slot=8;};
inline Selection Select(bool enabled,std::uint8_t flags,unsigned eligible,const Random& random) noexcept {
    if(!enabled||!UltimaResolved(flags)||!eligible||(eligible&~255u)||!random.draw)return {};
    unsigned roll=0;if(!random.draw(random.context,100,roll)||roll>=Chance)return {};
    const auto count=Count(eligible);unsigned ordinal=0;
    if(!random.draw(random.context,count,ordinal)||ordinal>=count)return {};
    for(unsigned i=0;i<8;++i)if(eligible&(1u<<i)){if(!ordinal--)return {true,i};}
    return {};
}
inline bool Bounded(std::uint32_t raw,unsigned bound,unsigned& output) noexcept {
    if(!bound)return false;
    const std::uint64_t span=std::uint64_t(1)<<32,limit=span-span%bound;
    // Preserve the exact distribution among valid draws without consuming an
    // extra sample for the small rejected tail. Failure retains native data.
    if(raw>=limit)return false;
    output=raw%bound;return true;
}
inline bool Build(const FormationProof& proof,const unsigned char* bytes,std::size_t size,unsigned slot,std::vector<unsigned char>& output) noexcept {
    Layout layout;if(!Inspect(proof,bytes,size,layout)||slot>=8||!(layout.eligible&(1u<<slot)))return false;
    try {std::vector<unsigned char> view(bytes,bytes+size);const auto at=layout.slots+slot*2;
        view[at]=CloneNative&255;view[at+1]=CloneNative>>8;output=std::move(view);return true;
    }catch(...){return false;}
}
} // namespace FfxHooks::OmegaUltima
