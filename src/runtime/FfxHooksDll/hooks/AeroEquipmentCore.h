#pragma once
#include "AeroEquipmentPayloads.generated.h"
#include "UiAbilityNameProof.h"
#include "SpiraCapturePayload.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace FfxHooks::AeroEquipment {
inline constexpr unsigned First=175,Last=180,Count=6,Wind=0x40,Valefor=8,Aero=367;
inline constexpr const char* Names[]={"Aero Strike","Aero Touch","Aero Ward","Aeroproof","Aero Eater","SOS NulAero"};
inline unsigned Word(const unsigned char* p) noexcept {return p[0]|(unsigned(p[1])<<8);}
inline unsigned Dword(const unsigned char* p) noexcept {return Word(p)|(Word(p+2)<<16);}
inline unsigned Glyph(char c) noexcept {
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z'))return static_cast<unsigned char>(c)+15u;
    if(c>='0'&&c<='9')return static_cast<unsigned char>(c);
    return c==' '?58u:256u;
}
inline bool Named(const unsigned char* bytes,std::size_t length,unsigned effect) noexcept {
    if(!bytes||effect>=Count)return false;
    const auto* name=Names[effect];std::size_t n=0;
    for(;name[n]&&n<length;++n)if(bytes[n]!=Glyph(name[n]))break;
    if(!name[n]&&n<length&&!bytes[n])return true;
    if(UiAbilityNameProof::Matches(First+effect,bytes,length))return true;
    char digits[8]{};const int count=std::snprintf(digits,sizeof(digits),"%u",First+effect);
    return count>0&&static_cast<std::size_t>(count)<length&&
        !std::memcmp(bytes,digits,static_cast<std::size_t>(count))&&!bytes[count];
}
inline bool Validate(const unsigned char* bytes,std::size_t size) noexcept {
    if(!bytes||size<20||Word(bytes)!=1||Word(bytes+8)||Word(bytes+10)<Last||
       Word(bytes+10)>4095||Word(bytes+12)!=108||Dword(bytes+16)!=20)return false;
    const auto body=(std::size_t(Word(bytes+10))+1)*108;
    if(body!=Word(bytes+14)||body>=size-20)return false;
    const auto pool=20+body;
    for(unsigned effect=0;effect<Count;++effect){
        const auto* row=bytes+20+(First+effect)*108;
        for(unsigned i=0;i<Payloads[effect].size();++i)
            if(!SpiraCapturePayload::Matches(First+effect,16+i,row[16+i],Payloads[effect][i]))return false;
        const auto offset=Word(row);
        if(offset>=size-pool||!Named(bytes+pool+offset,size-pool-offset,effect))return false;
    }
    return true;
}
inline bool Touch(bool physicalWeapon,bool equipped,unsigned nativeMask,unsigned roll) noexcept {
    return physicalWeapon&&equipped&&!(nativeMask&Wind)&&roll<50;
}
inline unsigned Sos(bool equipped,bool alive,unsigned nativeCondition) noexcept {
    return equipped&&alive&&(nativeCondition==1||nativeCondition==2)?Wind:0;
}
}
