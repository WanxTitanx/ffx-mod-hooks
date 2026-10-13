#pragma once
// Jarvis-HOOK: selected encounter slots and BTL asset suffixes are different.
#include "SinNaturalCore.h"
#include <cstddef>
#include <limits>

namespace FfxHooks::SinNatural {
struct FormationReader {
    void* context=nullptr;
    bool (*read)(void*,std::uintptr_t,void*,std::size_t)=nullptr;
};
// Shared by the supported Steam and legacy images; callers retain their normal
// executable-profile and game-thread admission before reading this pointer.
inline constexpr std::uintptr_t kTablePointerRva=0xD2A9C4u;
inline bool ResolveFormation(const FormationReader& io,std::uintptr_t table,
                             const Evidence& selected,std::uint8_t& asset) noexcept {
    asset=0xFF;
    if(!io.read||!Admitted(selected)||table<0x10000u||table>UINT32_MAX-16u)return false;
    std::array<std::uint8_t,16> header{};
    if(!io.read(io.context,table,header.data(),header.size()))return false;
    const auto word=[](const std::uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    const auto dword=[&](const std::uint8_t* p){return word(p)|(std::uint32_t(word(p+2))<<16);};
    const auto first=dword(header.data()+4),payload=dword(header.data()+8),size=dword(header.data()+12);
    if(dword(header.data())!=2||first<16||payload<=first||size<=payload||size>65535||
       (payload-first)%14||selected.nativeFieldRow>=(payload-first)/14||table>UINT32_MAX-size)return false;
    const auto read=[&](unsigned offset,void* bytes,unsigned count){
        return offset<=size&&count<=size-offset&&io.read(io.context,table+offset,bytes,count);
    };
    std::array<std::uint8_t,14> field{};
    if(!read(first+14u*selected.nativeFieldRow,field.data(),field.size())||word(field.data())!=unsigned(selected.field))return false;
    unsigned at=payload+word(field.data()+2);
    std::array<std::uint8_t,2> record{};
    if(!read(at,record.data(),record.size())||unsigned(selected.group)>=record[1])return false;
    at+=2;
    for(unsigned group=0;group<=unsigned(selected.group);++group){
        std::array<std::uint8_t,5> entry{};
        if(!read(at,entry.data(),entry.size()))return false;
        const unsigned count=entry[0],bytes=5+2*count;
        if(at>size||bytes>size-at)return false;
        if(group==unsigned(selected.group)){
            if(selected.formation>=count||!read(at+5+2u*selected.formation,record.data(),record.size())||record[0]==0xFF)return false;
            asset=record[0];return true;
        }
        at+=bytes;
    }
    return false;
}
}
