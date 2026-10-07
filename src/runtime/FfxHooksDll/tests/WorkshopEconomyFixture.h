#pragma once
#include "../shared/ExecutableProfile.h"
#include "../hooks/RonsoPoolSave.h"
#include "../shared/Config.h"
#include <cstdint>
#include <cstring>
namespace WorkshopEconomyFixture {
// Only changes a harness-owned array after reading the original private input.
inline void Seed(FfxHooks::RonsoPool::SaveImage& image,std::uint32_t gil=1000000){
    for(unsigned i=0;i<256;++i){const unsigned word=i<112?0x2000+i:0;
        image[0x3F0C+2*i]=static_cast<unsigned char>(word);image[0x3F0D+2*i]=static_cast<unsigned char>(word>>8);
        image[0x410C+i]=i<112?255:0;
    }
    std::memcpy(image.data()+0x3D88,&gil,4);
    const std::uint16_t story=0x448;std::memcpy(image.data()+0xC2C,&story,2);
    FfxHooks::RonsoPool::SealSave(image);
}
inline std::uint32_t Gil(std::uintptr_t base){std::uint32_t value=0;std::memcpy(&value,reinterpret_cast<void*>(base+(::FfxHooks::ExecutableProfile::Rva<0xD307D8>())),4);return value;}
inline void Mode(unsigned mode){
    FfxHooks::Config::LoadTextForTests(mode==1?"[equipment_workshop]\nrefinement_mode=1\n":"[equipment_workshop]\nrefinement_mode=2\n","C:\\private-workshop-economy.ini");
}
}
