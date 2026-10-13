#pragma once
#include "../../../../research/equipment_workshop/include/acquisition.h"
#include "../shared/ExecutableProfile.h"
#include <vector>
#include <cstring>
inline std::vector<unsigned char> WorkshopRecipes(bool spira=false){
    std::vector<unsigned char> bytes(20);unsigned count=0;
    const auto word=[&](std::size_t at,unsigned value){bytes[at]=static_cast<unsigned char>(value);bytes[at+1]=static_cast<unsigned char>(value>>8);};
    for(unsigned id=0;id<131;++id){const auto r=workshop::CustomizeRecipes[id];if(!r.quantity)continue;
        const auto at=bytes.size();bytes.resize(at+8);word(at,r.kind);word(at+2,spira&&(id==13||id==17)?0:0x8000+(spira&&id==24?23:id));word(at+4,0x2000+r.item);word(at+6,r.quantity);++count;}
    word(0,1);word(10,count-1);word(12,8);word(14,count*8);word(16,20);return bytes;
}
inline void BindWorkshopRecipes(std::uintptr_t image,const std::vector<unsigned char>& bytes){
    const auto pointer=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bytes.data()));
    std::memcpy(reinterpret_cast<void*>(image+FfxHooks::ExecutableProfile::Rva<0xD2A964>()),&pointer,4);
}
