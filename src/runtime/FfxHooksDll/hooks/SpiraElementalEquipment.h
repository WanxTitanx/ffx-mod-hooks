#pragma once
#include "SpiraElementalEquipment.generated.h"
#include "SpiraCapturePayload.h"
#include <cstddef>
#include <cstring>
namespace FfxHooks::SpiraElemental::Equipment {
inline constexpr unsigned Ids[]={176,181,182,183,184,185,186};
inline constexpr unsigned Elements[]={0x40,1,2,4,8,0x10,0x80};
inline constexpr const char* Labels[]={"Aero Touch","Fire Touch","Ice Touch","Lightning Touch","Water Touch","Holy Touch","Shadow Touch"};
inline unsigned Word(const unsigned char* p) noexcept {return p[0]|(unsigned(p[1])<<8);}
inline unsigned Dword(const unsigned char* p) noexcept {return Word(p)|(Word(p+2)<<16);}
inline unsigned Touch(unsigned word) noexcept {
    for(unsigned i=0;i<7;++i)if(word==0x8000+Ids[i])return Elements[i];return 0;
}
inline unsigned Ward(unsigned word) noexcept {
    switch(word){case 0x801F:return 1;case 0x8023:return 2;case 0x8027:return 4;case 0x802B:return 8;case 0x80B1:return 0x40;default:return 0;}
}
inline bool Payload(const unsigned char* row,unsigned id) noexcept {
    if(!row||!Touch(0x8000+id))return false;
    for(unsigned at=16;at<108;++at){const unsigned expected=at==0x68?20u:at==0x69?35u:0u;
        if(!SpiraCapturePayload::Matches(id,at,row[at],expected))return false;}
    return true;
}
inline bool Validate(const unsigned char* data,std::size_t size) noexcept {
    if(!data||size<20||size>65535||Dword(data)!=1||Word(data+8)||Word(data+10)<186||
       Word(data+10)>4095||Word(data+12)!=108||Dword(data+16)!=20)return false;
    const unsigned body=(Word(data+10)+1u)*108u,pool=20+body;
    if(body!=Word(data+14)||pool>=size)return false;
    for(unsigned id:Ids){const auto* row=data+20+id*108;if(!Payload(row,id))return false;
        const unsigned offset=Word(row);if(offset>=size-pool)return false;
        bool named=false;
        for(const auto& proof:Names)if(proof.id==id&&proof.size<=size-pool-offset&&
            !std::memcmp(data+pool+offset,proof.bytes.data(),proof.size)){named=true;break;}
        if(!named)return false;
    }
    return true;
}
}
