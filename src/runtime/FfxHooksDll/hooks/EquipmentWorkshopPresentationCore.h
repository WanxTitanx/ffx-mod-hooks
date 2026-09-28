#pragma once
#include "../../../../research/equipment_workshop/include/workshop.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FfxHooks::EquipmentWorkshop::Presentation {
struct GearView {std::uint8_t bytes[24]{};};
inline bool BuildGearView(const workshop::Piece& p,GearView& out){
    if(!p.id||!p.native[2]||p.native[4]>6||p.native[5]>1||p.native[11]>4||p.mode>2||p.rank>10||p.fifthUnlocked>1||
       (p.fifthUnlocked&&p.native[11]!=4))return false;
    bool changed=p.fifthUnlocked!=0;
    for(unsigned i=0;i<5;++i){const auto word=workshop::Ability(p,i);const unsigned rank=workshop::AbilityRank(p,i);
        if(rank>10||(i<4&&i>=p.native[11]&&word!=workshop::Empty)||
           (i==4&&word!=workshop::Empty&&(!p.fifthUnlocked||!workshop::ValidFifthWord(word))))return false;
        changed=changed||rank!=0;
    }
    if(!changed)return false;
    GearView next{};std::memcpy(next.bytes,p.native,22);
    for(unsigned i=0;i<5;++i){const auto word=workshop::Ability(p,i);
        next.bytes[14+2*i]=static_cast<std::uint8_t>(word);next.bytes[15+2*i]=static_cast<std::uint8_t>(word>>8);}
    next.bytes[11]=static_cast<std::uint8_t>(p.native[11]+p.fifthUnlocked);out=next;return true;
}
inline unsigned NextSlot(const workshop::Piece& p,std::uint16_t word,unsigned& cursor){
    const unsigned capacity=p.native[11]+p.fifthUnlocked;
    if(capacity>5)return 5;
    while(cursor<capacity&&workshop::Ability(p,cursor)==workshop::Empty)++cursor;
    if(cursor==capacity||workshop::Ability(p,cursor)!=word)return 5;
    return cursor++;
}
inline bool AppendRank(const unsigned char* name,std::size_t length,unsigned rank,unsigned char* out,std::size_t capacity){
    if(!name||!out||rank<1||rank>10||length>180||capacity<length+4+(rank==10?1u:0u))return false;
    std::memmove(out,name,length);std::size_t i=length;
    // FFX_ATLAS: space=0x3A, plus=0x45, decimal glyphs=0x30..0x39.
    out[i++]=0x3A;out[i++]=0x45;if(rank==10)out[i++]=0x31;
    out[i++]=static_cast<unsigned char>(0x30+rank%10);out[i]=0;return true;
}
}
